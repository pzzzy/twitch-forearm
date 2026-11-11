// Native JC3248W535 full-frame transport. Vendor register sequence and panel
// continuation protocol are preserved in components/forearm_panel.
#include "board_display.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cstring>
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_heap_caps.h"
#include "esp_attr.h"
#include "esp_intr_alloc.h"
#include "esp_memory_utils.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_axs15231b.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "jc3248_init.h"
#include "axs_touch_state.h"
#include "rgb565_transport.h"
#include "touch_frame_latency.h"
#include "touch_sample_cadence.h"

static_assert(LV_COLOR_DEPTH == 16, "JC3248W535 uses RGB565");
static_assert(LV_COLOR_16_SWAP == 0, "Transport performs the wire byte swap");

namespace {
constexpr auto TAG = "jc3248";
constexpr int STRIP_ROWS = 48;
constexpr int STRIP_PIXELS = BOARD_WIDTH * STRIP_ROWS;
// Reversible A/B experiment. False restores full-sized direct-mode composition.
// True draws logical dirty rectangles in the first internal DMA staging strip,
// copies them into the persistent PSRAM image, then phase-reuses both strips for
// the final ordered full-frame panel transfer. No additional SRAM allocation.
constexpr bool INTERNAL_STRIP_RENDER = false;
constexpr bool ASYNC_DISPLAY_TRANSPORT = true;
static_assert(!ASYNC_DISPLAY_TRANSPORT || !INTERNAL_STRIP_RENDER,
              "Async full-frame ownership requires two direct-render buffers");
constexpr gpio_num_t BL_PIN = GPIO_NUM_1;
constexpr gpio_num_t TE_PIN = GPIO_NUM_38;
constexpr i2c_port_t TOUCH_PORT = I2C_NUM_0;
constexpr uint8_t TOUCH_ADDRESS = 0x3b;
// The legacy driver's IRAM interrupt can dereference these while flash cache
// is unavailable. Keep both protocol buffers explicitly in internal DRAM.
DRAM_ATTR const uint8_t TOUCH_COMMAND[]{0xb5, 0xab, 0xa5, 0x5a, 0, 0, 0, 8, 0, 0, 0};
DRAM_ATTR uint8_t touch_packet[8]{};
constexpr TickType_t TOUCH_TRANSACTION_TIMEOUT = pdMS_TO_TICKS(10);
constexpr TickType_t DMA_TIMEOUT = pdMS_TO_TICKS(100);
constexpr uint32_t TE_TIMEOUT_MS = 20;

lv_color_t* framebuffer = nullptr;
lv_color_t* framebuffer_second = nullptr;
const lv_color_t* submitted_framebuffer = nullptr;
uint16_t* transport[2]{};
esp_lcd_panel_io_handle_t panel_io = nullptr;
esp_lcd_panel_handle_t panel = nullptr;
SemaphoreHandle_t transfer_done = nullptr;
SemaphoreHandle_t te_edge = nullptr;
lv_disp_draw_buf_t draw_buffer{};
lv_disp_drv_t display_driver{};
lv_indev_drv_t touch_driver{};
lv_disp_t* display = nullptr;
lv_indev_t* touch_device = nullptr;
BoardStats stats{};
BoardStats panel_work{};
BoardStats panel_published{};
portMUX_TYPE panel_lock = portMUX_INITIALIZER_UNLOCKED;
std::atomic<uint32_t> completed_frames{0};
std::atomic<bool> display_worker_running{false};
std::atomic<bool> display_transfer_pending{false};
std::atomic<bool> staged_transport_copy{false};
TouchFrameLatency touch_latency;
portMUX_TYPE touch_latency_lock = portMUX_INITIALIZER_UNLOCKED;
BoardStats touch_work{};       // Written only by the physical sampler.
BoardStats touch_published{};  // Short critical-section publication for readers.
portMUX_TYPE touch_lock = portMUX_INITIALIZER_UNLOCKED;
volatile uint32_t te_count = 0;
std::atomic<bool> panel_fault{false};
bool injection_enabled = false;
bool injection_pressed = false;
uint16_t injection_x = 0, injection_y = 0;
std::array<uint32_t, 128> frame_times{};
size_t frame_time_position = 0;
size_t frame_time_count = 0;
struct FrameJob { const lv_color_t* pixels; int64_t queued_at_us; uint32_t physical_touch_cutoff; };
QueueHandle_t frame_jobs = nullptr;
SemaphoreHandle_t frame_source_free = nullptr;
int64_t render_started_at_us = 0;
lv_timer_cb_t original_refresh_callback = nullptr;
void (*original_buffer_copy)(lv_draw_ctx_t*, void*, lv_coord_t, const lv_area_t*,
                             void*, lv_coord_t, const lv_area_t*) = nullptr;

struct TouchSample {
    uint16_t x, y;
    bool pressed;
    bool edge;
    bool cancel = false;
    uint32_t physical_sequence = 0;
    uint64_t physical_pressed_at_us = 0;
};
std::array<TouchSample, 64> touch_queue{};
size_t touch_queue_count = 0;
bool reader_reset_requested = false;
bool reader_block_until_release = false;
TouchSample delivered_touch{};

// Caller owns touch_lock. The original down location and release edge are never
// coalesced. Only pending move samples can be overwritten by a newer position.
void queue_touch(const TouchSample& sample) {
    if (!sample.edge && touch_queue_count && !touch_queue[touch_queue_count - 1].edge) {
        touch_queue[touch_queue_count - 1] = sample;
        ++touch_work.touch_queue_coalesced;
        return;
    }
    if (touch_queue_count == touch_queue.size()) {
        ++touch_work.touch_queue_overflows;
        size_t move = 0;
        while (move < touch_queue_count && touch_queue[move].edge) ++move;
        if (move < touch_queue_count) {
            memmove(touch_queue.data() + move, touch_queue.data() + move + 1,
                    (touch_queue_count - move - 1) * sizeof(TouchSample));
            --touch_queue_count;
        } else {
            // A stream of 64 unconsumed edges is no longer safe to replay as
            // clicks. Cancel it on the UI owner and wait for physical release.
            touch_queue_count = 0;
            reader_reset_requested = true;
            return;
        }
    }
    touch_queue[touch_queue_count++] = sample;
    ++touch_work.touch_queue_events;
    touch_work.touch_queue_peak = std::max<uint32_t>(touch_work.touch_queue_peak, touch_queue_count);
}

bool color_done(esp_lcd_panel_io_handle_t, esp_lcd_panel_io_event_data_t*, void*) {
    BaseType_t awakened = pdFALSE;
    xSemaphoreGiveFromISR(transfer_done, &awakened);
    return awakened == pdTRUE;
}

void IRAM_ATTR on_te(void*) {
    te_count = te_count + 1;
    BaseType_t awakened = pdFALSE;
    xSemaphoreGiveFromISR(te_edge, &awakened);
    if (awakened == pdTRUE) portYIELD_FROM_ISR();
}

bool await_dma(uint32_t& wait_us) {
    const int64_t started = esp_timer_get_time();
    const bool completed = xSemaphoreTake(transfer_done, DMA_TIMEOUT) == pdTRUE;
    wait_us += static_cast<uint32_t>(esp_timer_get_time() - started);
    if (completed) return true;
    // The transport buffers stay allocated and unused after uncertain DMA.
    panel_fault.store(true, std::memory_order_release);
    ++panel_work.flush_errors;
    ESP_LOGE(TAG, "Panel DMA timed out; stop transfers until restart");
    return false;
}

void record_phase(uint64_t& total, uint32_t& last, uint32_t& maximum, uint32_t duration) {
    total += duration;
    last = duration;
    maximum = std::max(maximum, duration);
}

void profiled_refresh(lv_timer_t* timer) {
    const uint32_t before = stats.render_submissions;
    const int64_t started = esp_timer_get_time();
    original_refresh_callback(timer);
    if (stats.render_submissions != before)
        record_phase(stats.lvgl_refresh_us_total, stats.lvgl_refresh_us_last,
                     stats.lvgl_refresh_us_max, static_cast<uint32_t>(esp_timer_get_time() - started));
}

void render_started(lv_disp_drv_t*) { render_started_at_us = esp_timer_get_time(); }

void profiled_buffer_copy(lv_draw_ctx_t* context, void* destination, lv_coord_t destination_stride,
                          const lv_area_t* destination_area, void* source, lv_coord_t source_stride,
                          const lv_area_t* source_area) {
    const int64_t started = esp_timer_get_time();
    original_buffer_copy(context, destination, destination_stride, destination_area,
                         source, source_stride, source_area);
    const auto duration = static_cast<uint32_t>(esp_timer_get_time() - started);
    record_phase(stats.sync_copy_us_total, stats.sync_copy_us_last, stats.sync_copy_us_max, duration);
    ++stats.sync_copy_calls;
    stats.sync_copy_pixels += lv_area_get_size(destination_area);
}

void copy_transport_strip(const uint16_t* source, uint16_t* destination, size_t pixels, bool staged) {
    if (staged) rgb565_copy_then_swap(source, destination, pixels);
    else rgb565_swap_copy(source, destination, pixels);
}

bool transmit_frame(const lv_color_t* pixels, int64_t queued_at_us) {
    const int64_t started = esp_timer_get_time();
    uint32_t convert_us = 0, dma_wait_us = 0, submit_us = 0, te_wait_us = 0;
    uint32_t payload_us = 0;
    const auto dispatch_us = static_cast<uint32_t>(started - queued_at_us);
    const bool staged = staged_transport_copy.load(std::memory_order_acquire);
    bool ok = !panel_fault.load(std::memory_order_acquire);
    if (ok) {
        // Prepare strip zero before waiting for TE, so the pixel transfer starts
        // promptly after the same falling edge used by the synchronous driver.
        int64_t phase = esp_timer_get_time();
        copy_transport_strip(reinterpret_cast<const uint16_t*>(pixels), transport[0], STRIP_PIXELS, staged);
        convert_us += static_cast<uint32_t>(esp_timer_get_time() - phase);
        xSemaphoreTake(te_edge, 0);
        phase = esp_timer_get_time();
        if (xSemaphoreTake(te_edge, pdMS_TO_TICKS(TE_TIMEOUT_MS)) != pdTRUE)
            ++panel_work.te_wait_timeouts;
        te_wait_us = static_cast<uint32_t>(esp_timer_get_time() - phase);
        const int64_t payload_started = esp_timer_get_time();
        bool in_flight = false;
        for (int y = 0, chunk = 0; y < BOARD_HEIGHT; y += STRIP_ROWS, ++chunk) {
            if (panel_fault.load(std::memory_order_acquire)) { ok = false; break; }
            const int rows = std::min(STRIP_ROWS, BOARD_HEIGHT - y);
            auto* target = transport[chunk & 1];
            if (chunk != 0) {
                phase = esp_timer_get_time();
                copy_transport_strip(reinterpret_cast<const uint16_t*>(pixels) + y * BOARD_WIDTH,
                                     target, rows * BOARD_WIDTH, staged);
                convert_us += static_cast<uint32_t>(esp_timer_get_time() - phase);
            }
            if (in_flight && !await_dma(dma_wait_us)) {
                ok = false;
                in_flight = false;
                break;
            }
            in_flight = false;
            phase = esp_timer_get_time();
            const esp_err_t error = esp_lcd_panel_draw_bitmap(
                panel, 0, y, BOARD_WIDTH, y + rows, target);
            submit_us += static_cast<uint32_t>(esp_timer_get_time() - phase);
            if (error != ESP_OK) {
                ++panel_work.flush_errors;
                panel_fault.store(true, std::memory_order_release);
                ESP_LOGE(TAG, "Panel strip failed: %s", esp_err_to_name(error));
                ok = false;
                break;
            }
            in_flight = true;
        }
        if (in_flight && !await_dma(dma_wait_us)) ok = false;
        payload_us = static_cast<uint32_t>(esp_timer_get_time() - payload_started);
    }
    const auto elapsed = static_cast<uint32_t>(esp_timer_get_time() - started);
    record_phase(panel_work.te_wait_us_total, panel_work.te_wait_us_last, panel_work.te_wait_us_max, te_wait_us);
    record_phase(panel_work.convert_us_total, panel_work.convert_us_last, panel_work.convert_us_max, convert_us);
    record_phase(panel_work.dma_wait_us_total, panel_work.dma_wait_us_last, panel_work.dma_wait_us_max, dma_wait_us);
    record_phase(panel_work.spi_submit_us_total, panel_work.spi_submit_us_last, panel_work.spi_submit_us_max, submit_us);
    panel_work.payload_us_last = payload_us;
    panel_work.payload_us_max = std::max(panel_work.payload_us_max, payload_us);
    panel_work.job_dispatch_us_last = dispatch_us;
    panel_work.job_dispatch_us_max = std::max(panel_work.job_dispatch_us_max, dispatch_us);
    if (ok) {
        ++panel_work.frames;
        panel_work.last_frame_us = elapsed;
        panel_work.frame_us_total += elapsed;
        panel_work.max_frame_us = std::max(panel_work.max_frame_us, elapsed);
    }
    portENTER_CRITICAL(&panel_lock);
    if (ok) {
        frame_times[frame_time_position] = elapsed;
        frame_time_position = (frame_time_position + 1) % frame_times.size();
        frame_time_count = std::min(frame_time_count + 1, frame_times.size());
    }
    panel_published = panel_work;
    portEXIT_CRITICAL(&panel_lock);
    if (ok) completed_frames.fetch_add(1, std::memory_order_release);
    return ok;
}

void display_transport_task(void*) {
    display_worker_running.store(true, std::memory_order_release);
    FrameJob job{};
    for (;;) {
        if (xQueueReceive(frame_jobs, &job, portMAX_DELAY) != pdTRUE) continue;
        if (transmit_frame(job.pixels, job.queued_at_us)) {
            const uint64_t completed_us = esp_timer_get_time();
            portENTER_CRITICAL(&touch_latency_lock);
            touch_latency.completed(job.physical_touch_cutoff, completed_us);
            portEXIT_CRITICAL(&touch_latency_lock);
        }
        // From this point the previous source buffer is no longer read. The UI
        // must obtain this token before handing over the other buffer and letting
        // LVGL swap back to reuse the old one. The worker never calls LVGL.
        display_transfer_pending.store(false, std::memory_order_release);
        xSemaphoreGive(frame_source_free);
    }
}

bool obtain_frame_source() {
    const int64_t started = esp_timer_get_time();
    bool acquired = xSemaphoreTake(frame_source_free, pdMS_TO_TICKS(250)) == pdTRUE;
    if (!acquired) {
        ++stats.flush_errors;
        panel_fault.store(true, std::memory_order_release);
        // Ask the worker to stop before another source read. If it cannot finish
        // within its bounded DMA wait, restart rather than acknowledge a buffer
        // that LVGL could reuse while it is still being read.
        acquired = xSemaphoreTake(frame_source_free, pdMS_TO_TICKS(250)) == pdTRUE;
        if (!acquired) {
            ESP_LOGE(TAG, "Display source ownership was not released; restarting");
            esp_restart();
        }
    }
    const auto duration = static_cast<uint32_t>(esp_timer_get_time() - started);
    record_phase(stats.frame_queue_wait_us_total, stats.frame_queue_wait_us_last,
                 stats.frame_queue_wait_us_max, duration);
    return acquired;
}

void flush(lv_disp_drv_t* driver, const lv_area_t* area, lv_color_t* colors) {
    ++stats.logical_flushes;
    if (INTERNAL_STRIP_RENDER) {
        const int64_t copied_at = esp_timer_get_time();
        if (area->x1 < 0 || area->y1 < 0 || area->x2 >= BOARD_WIDTH ||
            area->y2 >= BOARD_HEIGHT || area->x2 < area->x1 || area->y2 < area->y1) {
            ++stats.flush_errors;
            panel_fault.store(true, std::memory_order_release);
            ESP_LOGE(TAG, "Invalid logical composition area");
            lv_disp_flush_ready(driver);
            return;
        }
        const int width = area->x2 - area->x1 + 1;
        const int height = area->y2 - area->y1 + 1;
        for (int row = 0; row < height; ++row)
            memcpy(framebuffer + (area->y1 + row) * BOARD_WIDTH + area->x1,
                   colors + row * width, width * sizeof(lv_color_t));
        const auto duration = static_cast<uint32_t>(esp_timer_get_time() - copied_at);
        stats.compose_copy_us_total += duration;
        stats.compose_copy_us_max = std::max(stats.compose_copy_us_max, duration);
        stats.composed_pixels += static_cast<uint32_t>(width * height);
    }
    if (!lv_disp_flush_is_last(driver)) {
        ++stats.logical_nonfinal_flushes;
        lv_disp_flush_ready(driver);
        return;
    }
    if (render_started_at_us) {
        record_phase(stats.render_cpu_us_total, stats.render_cpu_us_last, stats.render_cpu_us_max,
                     static_cast<uint32_t>(esp_timer_get_time() - render_started_at_us));
        render_started_at_us = 0;
    }
    const lv_color_t* source = INTERNAL_STRIP_RENDER ? framebuffer : colors;
    portENTER_CRITICAL(&touch_latency_lock);
    const uint32_t physical_touch_cutoff = touch_latency.composition_cutoff();
    portEXIT_CRITICAL(&touch_latency_lock);
    if (ASYNC_DISPLAY_TRANSPORT) {
        obtain_frame_source();
        if (panel_fault.load(std::memory_order_acquire)) {
            xSemaphoreGive(frame_source_free);
            lv_disp_flush_ready(driver);
            return;
        }
        // Only these two full-sized persistent buffers participate in the
        // ownership protocol. No pointer into a transient LVGL area is queued.
        if (source != framebuffer && source != framebuffer_second) {
            ++stats.flush_errors;
            panel_fault.store(true, std::memory_order_release);
            xSemaphoreGive(frame_source_free);
            lv_disp_flush_ready(driver);
            return;
        }
        const FrameJob job{source, esp_timer_get_time(), physical_touch_cutoff};
        display_transfer_pending.store(true, std::memory_order_release);
        if (xQueueSend(frame_jobs, &job, 0) != pdTRUE) {
            ++stats.flush_errors;
            panel_fault.store(true, std::memory_order_release);
            display_transfer_pending.store(false, std::memory_order_release);
            xSemaphoreGive(frame_source_free);
        } else {
            submitted_framebuffer = source;
            ++stats.render_submissions;
        }
        // This releases logical composition, not DMA ownership. LVGL swaps to
        // the other buffer; the source remains immutable until the next final
        // callback obtains frame_source_free after the worker completes it.
        lv_disp_flush_ready(driver);
    } else {
        if (!panel_fault.load(std::memory_order_acquire)) {
            submitted_framebuffer = source;
            ++stats.render_submissions;
            if (transmit_frame(source, esp_timer_get_time())) {
                const uint64_t completed_us = esp_timer_get_time();
                portENTER_CRITICAL(&touch_latency_lock);
                touch_latency.completed(physical_touch_cutoff, completed_us);
                portEXIT_CRITICAL(&touch_latency_lock);
            }
        }
        lv_disp_flush_ready(driver);
    }
}

void record_touch_error(esp_err_t code, uint32_t operation, int64_t started_us,
                        int64_t cycle_started_us, uint32_t poll_gap_us) {
    const int64_t completed_us = esp_timer_get_time();
    ++touch_work.touch_errors;
    if (operation == 1) ++touch_work.touch_write_errors;
    else ++touch_work.touch_read_errors;
    if (code == ESP_ERR_TIMEOUT) ++touch_work.touch_timeout_errors;
    else if (code == ESP_FAIL) ++touch_work.touch_fail_errors;
    else ++touch_work.touch_other_errors;
    touch_work.touch_last_error_code = code;
    touch_work.touch_last_error_operation = operation;
    touch_work.touch_last_error_at_us = completed_us;
    touch_work.touch_last_error_duration_us = static_cast<uint32_t>(completed_us - started_us);
    touch_work.touch_last_error_cycle_us = static_cast<uint32_t>(completed_us - cycle_started_us);
    touch_work.touch_last_error_poll_gap_us = poll_gap_us;
}

AxsTouchReport read_physical(uint32_t poll_gap_us) {
    ++touch_work.touch_polls;
    // AXS15231B touch packet: gesture, point count, X-high/event, X-low,
    // Y-high, Y-low, two reserved bytes. Separate write/read matches vendor IO.
    const int64_t cycle_started = esp_timer_get_time();
    esp_err_t error = i2c_master_write_to_device(
        TOUCH_PORT, TOUCH_ADDRESS, TOUCH_COMMAND, sizeof(TOUCH_COMMAND), TOUCH_TRANSACTION_TIMEOUT);
    touch_work.touch_write_us_last = static_cast<uint32_t>(esp_timer_get_time() - cycle_started);
    touch_work.touch_write_us_max = std::max(touch_work.touch_write_us_max, touch_work.touch_write_us_last);
    if (error != ESP_OK) {
        record_touch_error(error, 1, cycle_started, cycle_started, poll_gap_us);
        return {AxsTouchKind::TransportError};
    }
    const int64_t read_started = esp_timer_get_time();
    error = i2c_master_read_from_device(
        TOUCH_PORT, TOUCH_ADDRESS, touch_packet, sizeof(touch_packet), TOUCH_TRANSACTION_TIMEOUT);
    touch_work.touch_read_us_last = static_cast<uint32_t>(esp_timer_get_time() - read_started);
    touch_work.touch_read_us_max = std::max(touch_work.touch_read_us_max, touch_work.touch_read_us_last);
    if (error != ESP_OK) {
        record_touch_error(error, 2, read_started, cycle_started, poll_gap_us);
        return {AxsTouchKind::TransportError};
    }
    const auto& data = touch_packet;
    touch_work.touch_present = true;
    memcpy(touch_work.last_touch_packet, data, sizeof(data));
    if (std::all_of(std::begin(data), std::end(data), [](uint8_t b) { return b == 0xff; }))
        ++touch_work.touch_all_ff;
    // ESPHome's AXS15231 implementation also checks the first byte: nonzero
    // report headers (including all-FF idle reports) do not contain coordinates.
    // Keep them distinct from malformed coordinate packets in diagnostics.
    if (data[0] != 0) {
        ++touch_work.touch_nonzero_headers;
        ++touch_work.touch_idle_packets;
        return {};
    }
    if (data[1] == 0) {
        ++touch_work.touch_zero_points;
        ++touch_work.touch_idle_packets;
        return {AxsTouchKind::Up};
    }
    if (data[1] > 1) {
        ++touch_work.touch_many_points;
        ++touch_work.invalid_touch_samples;
        memcpy(touch_work.last_invalid_packet, data, sizeof(data));
        return {AxsTouchKind::Invalid};
    }
    const uint8_t event = data[2] >> 6;
    switch (event) {
        case 0: ++touch_work.touch_down_events; break;
        case 1: ++touch_work.touch_up_events; break;
        case 2: ++touch_work.touch_contact_events; break;
        default: ++touch_work.touch_reserved_events; break;
    }
    const uint16_t x = static_cast<uint16_t>(((data[2] & 0x0f) << 8) | data[3]);
    const uint16_t y = static_cast<uint16_t>(((data[4] & 0x0f) << 8) | data[5]);
    touch_work.last_raw_x = x;
    touch_work.last_raw_y = y;
    if (x >= BOARD_WIDTH || y >= BOARD_HEIGHT) {
        ++touch_work.touch_out_of_bounds;
        ++touch_work.invalid_touch_samples;
        memcpy(touch_work.last_invalid_packet, data, sizeof(data));
        // An explicit UP still releases using the last valid coordinates.
        return event == 1 ? AxsTouchReport{AxsTouchKind::Up} :
                            AxsTouchReport{AxsTouchKind::Invalid, x, y, false};
    }
    memcpy(touch_work.last_contact_packet, data, sizeof(data));
    return axs_touch_decode(data, BOARD_WIDTH, BOARD_HEIGHT);
}

void touch_sampler(void*) {
    // FreeRTOS dynamic task stacks are internal in this SDK. The legacy helper
    // builds its command links on this stack, which its IRAM ISR also reads.
    uint8_t stack_probe;
    ESP_ERROR_CHECK(esp_ptr_internal(&stack_probe) && esp_ptr_internal(TOUCH_COMMAND) &&
                    esp_ptr_internal(touch_packet) ? ESP_OK : ESP_ERR_INVALID_STATE);
    AxsTouchState state;
    uint32_t physical_sequence = 0;
    uint64_t physical_pressed_at_us = 0;
    int64_t previous_sample_us = 0;
    bool have_previous_sample = false;
    TouchSampleCadence cadence;
    touch_work.touch_sampler_core = xPortGetCoreID();
    touch_work.touch_sampler_running = true;
    for (;;) {
        // RTOS delays can wake before a microsecond deadline because of tick
        // phase. Recheck without polling; never spin or replay missed slots.
        while (const auto wait_us = cadence.wait_us(esp_timer_get_time())) {
            const auto delay_ms = static_cast<uint32_t>((wait_us + 999) / 1000);
            vTaskDelay(std::max<TickType_t>(1, pdMS_TO_TICKS(delay_ms)));
        }
        const int64_t sampled_us = esp_timer_get_time();
        const auto gap = have_previous_sample ? static_cast<uint32_t>(sampled_us - previous_sample_us) : 0;
        if (have_previous_sample) {
            if (!touch_work.touch_sample_gap_min_us || gap < touch_work.touch_sample_gap_min_us)
                touch_work.touch_sample_gap_min_us = gap;
            if (gap < TouchSampleCadence::period_us) ++touch_work.touch_fast_polls;
        }
        touch_work.touch_sample_gap_max_us = std::max(touch_work.touch_sample_gap_max_us, gap);
        previous_sample_us = sampled_us;
        have_previous_sample = true;
        const AxsTouchReport report = read_physical(gap);
        const uint64_t received_us = esp_timer_get_time();
        const AxsTouchAction action = state.apply(report);
        if (action == AxsTouchAction::Press) {
            ++touch_work.touch_contacts;
            if (++physical_sequence == 0) ++physical_sequence;
            physical_pressed_at_us = received_us;
            touch_work.physical_press_sequence = physical_sequence;
        }
        if (action == AxsTouchAction::Release) ++touch_work.touch_release_edges;
        if (action == AxsTouchAction::Cancel) ++touch_work.touch_error_cancels;
        touch_work.touch_consecutive_errors = state.consecutive_errors();
        touch_work.touch_consecutive_errors_max = std::max(
            touch_work.touch_consecutive_errors_max, state.consecutive_errors());
        if (state.pressed()) {
            ++touch_work.touch_pressed_samples;
            if (report.kind == AxsTouchKind::NoData) ++touch_work.touch_no_data_held_samples;
            if (report.kind == AxsTouchKind::Down || report.kind == AxsTouchKind::Contact)
                touch_work.last_physical_touch_us = sampled_us;
        }
        touch_work.last_screen_x = state.x();
        touch_work.last_screen_y = state.y();
        touch_work.physical_pressed = state.pressed();
        const bool edge = action == AxsTouchAction::Press || action == AxsTouchAction::Release ||
                          action == AxsTouchAction::Cancel;
        portENTER_CRITICAL(&touch_lock);
        if (edge || action == AxsTouchAction::Move)
            queue_touch({state.x(), state.y(), state.pressed(), edge, action == AxsTouchAction::Cancel,
                         physical_sequence, physical_pressed_at_us});
        touch_work.touch_queue_depth = touch_queue_count;
        touch_published = touch_work;
        portEXIT_CRITICAL(&touch_lock);
        cadence.completed(sampled_us, esp_timer_get_time());
    }
}

void read_touch(lv_indev_drv_t*, lv_indev_data_t* data) {
    if (injection_enabled) {
        ++stats.injected_samples;
        // Diagnostic input affects LVGL only. Controller sampling/counters keep
        // running and cannot be mistaken for a physical hardware input test.
        portENTER_CRITICAL(&touch_lock);
        touch_queue_count = 0;
        portEXIT_CRITICAL(&touch_lock);
        data->point.x = injection_x;
        data->point.y = injection_y;
        data->state = injection_pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
        data->continue_reading = false;
        return;
    }
    bool reset = false;
    bool have_sample = false;
    TouchSample sample{};
    bool more = false;
    portENTER_CRITICAL(&touch_lock);
    if (reader_reset_requested) {
        reader_reset_requested = false;
        reset = true;
        reader_block_until_release = touch_published.physical_pressed;
    }
    if (touch_queue_count) {
        sample = touch_queue[0];
        --touch_queue_count;
        memmove(touch_queue.data(), touch_queue.data() + 1, touch_queue_count * sizeof(TouchSample));
        have_sample = true;
    }
    more = touch_queue_count != 0;
    portEXIT_CRITICAL(&touch_lock);
    if (reset) {
        lv_indev_reset(touch_device, nullptr);
        delivered_touch.pressed = false;
    }
    if (have_sample) {
        if (sample.cancel) {
            portENTER_CRITICAL(&touch_latency_lock);
            touch_latency.cancel(sample.physical_sequence);
            portEXIT_CRITICAL(&touch_latency_lock);
            lv_indev_reset(touch_device, nullptr);
            delivered_touch.pressed = false;
            reader_block_until_release = false; // Sampler waits for explicit UP.
        } else if (reader_block_until_release) {
            if (!sample.pressed) reader_block_until_release = false;
        } else {
            delivered_touch = sample;
            if (sample.edge && sample.pressed) {
                portENTER_CRITICAL(&touch_latency_lock);
                touch_latency.delivered(sample.physical_sequence, sample.physical_pressed_at_us);
                portEXIT_CRITICAL(&touch_latency_lock);
            }
        }
    }
    data->point.x = delivered_touch.x;
    data->point.y = delivered_touch.y;
    data->state = delivered_touch.pressed ? LV_INDEV_STATE_PRESSED : LV_INDEV_STATE_RELEASED;
    data->continue_reading = more;
}

}  // namespace

void board_set_brightness(int percent) {
    percent = std::clamp(percent, 0, 100);
    ESP_ERROR_CHECK(ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1, 1023 * percent / 100));
    ESP_ERROR_CHECK(ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_1));
}

void board_display_init() {
    if (display) return;
    transfer_done = xSemaphoreCreateBinary();
    te_edge = xSemaphoreCreateBinary();
    ESP_ERROR_CHECK(transfer_done && te_edge ? ESP_OK : ESP_ERR_NO_MEM);

    ledc_timer_config_t pwm{};
    pwm.speed_mode = LEDC_LOW_SPEED_MODE;
    pwm.duty_resolution = LEDC_TIMER_10_BIT;
    pwm.timer_num = LEDC_TIMER_1;
    pwm.freq_hz = 5000;
    pwm.clk_cfg = LEDC_AUTO_CLK;
    ESP_ERROR_CHECK(ledc_timer_config(&pwm));
    ledc_channel_config_t backlight{};
    backlight.gpio_num = BL_PIN;
    backlight.speed_mode = LEDC_LOW_SPEED_MODE;
    backlight.channel = LEDC_CHANNEL_1;
    backlight.timer_sel = LEDC_TIMER_1;
    backlight.duty = 0;
    ESP_ERROR_CHECK(ledc_channel_config(&backlight));

    spi_bus_config_t bus{};
    bus.sclk_io_num = 47;
    bus.data0_io_num = 21;
    bus.data1_io_num = 48;
    bus.data2_io_num = 40;
    bus.data3_io_num = 39;
    bus.max_transfer_sz = STRIP_PIXELS * sizeof(uint16_t);
    ESP_ERROR_CHECK(spi_bus_initialize(SPI2_HOST, &bus, SPI_DMA_CH_AUTO));

    esp_lcd_panel_io_spi_config_t io{};
    io.cs_gpio_num = 45;
    io.dc_gpio_num = -1;
    io.spi_mode = 3;
    io.pclk_hz = 40 * 1000 * 1000;
    io.trans_queue_depth = 2;
    io.on_color_trans_done = color_done;
    io.lcd_cmd_bits = 32;
    io.lcd_param_bits = 8;
    io.flags.quad_mode = true;
    ESP_ERROR_CHECK(esp_lcd_new_panel_io_spi(
        static_cast<esp_lcd_spi_bus_handle_t>(SPI2_HOST), &io, &panel_io));

    uint16_t command_count = 0;
    axs15231b_vendor_config_t vendor{};
    vendor.init_cmds = jc3248_init_commands(&command_count);
    vendor.init_cmds_size = command_count;
    vendor.flags.use_qspi_interface = 1;
    esp_lcd_panel_dev_config_t panel_config{};
    panel_config.reset_gpio_num = -1;
    panel_config.rgb_ele_order = LCD_RGB_ELEMENT_ORDER_RGB;
    panel_config.bits_per_pixel = 16;
    panel_config.vendor_config = &vendor;
    ESP_ERROR_CHECK(esp_lcd_new_panel_axs15231b(panel_io, &panel_config, &panel));
    ESP_ERROR_CHECK(esp_lcd_panel_reset(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_init(panel));
    ESP_ERROR_CHECK(esp_lcd_panel_disp_on_off(panel, true));
    const uint8_t te_mode = 0;
    ESP_ERROR_CHECK(esp_lcd_panel_io_tx_param(panel_io, 0x02003500, &te_mode, 1));

    gpio_config_t te{};
    te.pin_bit_mask = 1ULL << TE_PIN;
    te.mode = GPIO_MODE_INPUT;
    te.pull_up_en = GPIO_PULLUP_ENABLE;
    te.intr_type = GPIO_INTR_NEGEDGE;
    ESP_ERROR_CHECK(gpio_config(&te));
    const auto service = gpio_install_isr_service(0);
    ESP_ERROR_CHECK(service == ESP_ERR_INVALID_STATE ? ESP_OK : service);
    ESP_ERROR_CHECK(gpio_isr_handler_add(TE_PIN, on_te, nullptr));

    framebuffer = static_cast<lv_color_t*>(heap_caps_calloc(
        BOARD_WIDTH * BOARD_HEIGHT, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (ASYNC_DISPLAY_TRANSPORT) {
        framebuffer_second = static_cast<lv_color_t*>(heap_caps_calloc(
            BOARD_WIDTH * BOARD_HEIGHT, sizeof(lv_color_t), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
        frame_jobs = xQueueCreate(1, sizeof(FrameJob));
        frame_source_free = xSemaphoreCreateBinary();
        ESP_ERROR_CHECK(framebuffer_second && frame_jobs && frame_source_free ? ESP_OK : ESP_ERR_NO_MEM);
        xSemaphoreGive(frame_source_free);
    }
    for (auto& strip : transport)
        strip = static_cast<uint16_t*>(heap_caps_malloc(
            STRIP_PIXELS * sizeof(uint16_t), MALLOC_CAP_INTERNAL | MALLOC_CAP_DMA));
    ESP_ERROR_CHECK(framebuffer && transport[0] && transport[1] ? ESP_OK : ESP_ERR_NO_MEM);
    lv_disp_draw_buf_init(&draw_buffer,
                         INTERNAL_STRIP_RENDER ? reinterpret_cast<lv_color_t*>(transport[0]) : framebuffer,
                         ASYNC_DISPLAY_TRANSPORT ? framebuffer_second : nullptr,
                         INTERNAL_STRIP_RENDER ? STRIP_PIXELS : BOARD_WIDTH * BOARD_HEIGHT);
    lv_disp_drv_init(&display_driver);
    display_driver.hor_res = BOARD_WIDTH;
    display_driver.ver_res = BOARD_HEIGHT;
    display_driver.draw_buf = &draw_buffer;
    display_driver.flush_cb = flush;
    display_driver.render_start_cb = render_started;
    display_driver.full_refresh = 0;
    display_driver.direct_mode = INTERNAL_STRIP_RENDER ? 0 : 1;
    display = lv_disp_drv_register(&display_driver);
    ESP_ERROR_CHECK(display ? ESP_OK : ESP_ERR_NO_MEM);
    // LVGL8 syncs prior dirty areas before render_start_cb. Wrap its existing
    // copy hook and refresh timer without modifying the pinned dependency.
    original_buffer_copy = display_driver.draw_ctx->buffer_copy;
    original_refresh_callback = display->refr_timer->timer_cb;
    ESP_ERROR_CHECK(original_buffer_copy && original_refresh_callback ? ESP_OK : ESP_ERR_INVALID_STATE);
    display_driver.draw_ctx->buffer_copy = profiled_buffer_copy;
    lv_timer_set_cb(display->refr_timer, profiled_refresh);

    i2c_config_t touch_bus{};
    touch_bus.mode = I2C_MODE_MASTER;
    touch_bus.sda_io_num = GPIO_NUM_4;
    touch_bus.scl_io_num = GPIO_NUM_8;
    touch_bus.sda_pullup_en = GPIO_PULLUP_DISABLE;
    touch_bus.scl_pullup_en = GPIO_PULLUP_DISABLE;
    touch_bus.master.clk_speed = 400000;
    ESP_ERROR_CHECK(i2c_param_config(TOUCH_PORT, &touch_bus));
    // SDK legacy i2c.h recommends IRAM allocation for time-sensitive slaves
    // when flash/cache can be unavailable. This preserves the 10 ms budget;
    // queued completion can now arrive even during a cache-off interval.
    ESP_ERROR_CHECK(i2c_driver_install(TOUCH_PORT, touch_bus.mode, 0, 0, ESP_INTR_FLAG_IRAM));
    touch_work.touch_i2c_iram = true;
    lv_indev_drv_init(&touch_driver);
    touch_driver.type = LV_INDEV_TYPE_POINTER;
    touch_driver.disp = display;
    touch_driver.read_cb = read_touch;
    touch_device = lv_indev_drv_register(&touch_driver);
    ESP_ERROR_CHECK(touch_device ? ESP_OK : ESP_ERR_NO_MEM);
    const BaseType_t sampler_created = xTaskCreatePinnedToCore(
        touch_sampler, "physical-touch", 4096, nullptr, 4, nullptr, 1);
    ESP_ERROR_CHECK(sampler_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    if (ASYNC_DISPLAY_TRANSPORT) {
        const BaseType_t worker_created = xTaskCreatePinnedToCore(
            display_transport_task, "display-transport", 4096, nullptr, 3, nullptr, 0);
        ESP_ERROR_CHECK(worker_created == pdPASS ? ESP_OK : ESP_ERR_NO_MEM);
    }
    board_set_brightness(75);
    ESP_LOGI(TAG, "320x480 %s composition/%s QSPI40MHz full-frame; touch SDA4/SCL8 at0x3b; RGB565 host-order",
             INTERNAL_STRIP_RENDER ? "internal-strip" : "direct",
             ASYNC_DISPLAY_TRANSPORT ? "pipelined" : "synchronous");
}

lv_disp_t* board_display() { return display; }
lv_indev_t* board_touch_indev() { return touch_device; }
const uint16_t* board_framebuffer() {
    return reinterpret_cast<const uint16_t*>(submitted_framebuffer ? submitted_framebuffer : framebuffer);
}
uint32_t board_frame_count() { return completed_frames.load(std::memory_order_acquire); }
uint32_t board_render_count() { return stats.render_submissions; }
void board_reset_touch_latency() {
    portENTER_CRITICAL(&touch_latency_lock);
    touch_latency.reset_measurements();
    portEXIT_CRITICAL(&touch_latency_lock);
}
void board_set_staged_transport_copy(bool enabled) {
    staged_transport_copy.store(enabled, std::memory_order_release);
}

BoardStats board_stats() {
    BoardStats snapshot = stats;
    snapshot.internal_strip_render = INTERNAL_STRIP_RENDER;
    snapshot.asynchronous_transport = ASYNC_DISPLAY_TRANSPORT;
    snapshot.staged_transport_copy = staged_transport_copy.load(std::memory_order_acquire);
    snapshot.display_worker_running = display_worker_running.load(std::memory_order_acquire);
    snapshot.display_transfer_pending = display_transfer_pending.load(std::memory_order_acquire);
    snapshot.display_fault = panel_fault.load(std::memory_order_acquire);
    snapshot.te_edges = te_count;
    BoardStats transport_stats;
    std::array<uint32_t, 128> ordered;
    size_t sample_count;
    portENTER_CRITICAL(&panel_lock);
    transport_stats = panel_published;
    ordered = frame_times;
    sample_count = frame_time_count;
    portEXIT_CRITICAL(&panel_lock);
#define PANEL_METRIC(name) snapshot.name = transport_stats.name
    PANEL_METRIC(frames); PANEL_METRIC(frame_us_total); PANEL_METRIC(last_frame_us); PANEL_METRIC(max_frame_us);
    PANEL_METRIC(te_wait_timeouts);
    PANEL_METRIC(te_wait_us_total); PANEL_METRIC(te_wait_us_last); PANEL_METRIC(te_wait_us_max);
    PANEL_METRIC(convert_us_total); PANEL_METRIC(convert_us_last); PANEL_METRIC(convert_us_max);
    PANEL_METRIC(dma_wait_us_total); PANEL_METRIC(dma_wait_us_last); PANEL_METRIC(dma_wait_us_max);
    PANEL_METRIC(spi_submit_us_total); PANEL_METRIC(spi_submit_us_last); PANEL_METRIC(spi_submit_us_max);
    PANEL_METRIC(payload_us_last); PANEL_METRIC(payload_us_max);
    PANEL_METRIC(job_dispatch_us_last); PANEL_METRIC(job_dispatch_us_max);
#undef PANEL_METRIC
    snapshot.flush_errors += transport_stats.flush_errors;
    BoardStats physical;
    portENTER_CRITICAL(&touch_lock);
    physical = touch_published;
    physical.touch_queue_depth = touch_queue_count;
    portEXIT_CRITICAL(&touch_lock);
#define PHYSICAL_METRIC(name) snapshot.name = physical.name
    PHYSICAL_METRIC(touch_polls); PHYSICAL_METRIC(touch_errors);
    PHYSICAL_METRIC(touch_write_errors); PHYSICAL_METRIC(touch_read_errors);
    PHYSICAL_METRIC(touch_timeout_errors); PHYSICAL_METRIC(touch_fail_errors); PHYSICAL_METRIC(touch_other_errors);
    PHYSICAL_METRIC(touch_last_error_code); PHYSICAL_METRIC(touch_last_error_operation);
    PHYSICAL_METRIC(touch_last_error_at_us); PHYSICAL_METRIC(touch_last_error_duration_us);
    PHYSICAL_METRIC(touch_last_error_cycle_us); PHYSICAL_METRIC(touch_last_error_poll_gap_us);
    PHYSICAL_METRIC(touch_write_us_last); PHYSICAL_METRIC(touch_write_us_max);
    PHYSICAL_METRIC(touch_read_us_last); PHYSICAL_METRIC(touch_read_us_max); PHYSICAL_METRIC(touch_i2c_iram);
    PHYSICAL_METRIC(touch_contacts); PHYSICAL_METRIC(touch_pressed_samples);
    PHYSICAL_METRIC(physical_press_sequence);
    PHYSICAL_METRIC(invalid_touch_samples); PHYSICAL_METRIC(touch_zero_points);
    PHYSICAL_METRIC(touch_many_points); PHYSICAL_METRIC(touch_out_of_bounds);
    PHYSICAL_METRIC(touch_all_ff); PHYSICAL_METRIC(touch_idle_packets);
    PHYSICAL_METRIC(touch_nonzero_headers); PHYSICAL_METRIC(touch_down_events);
    PHYSICAL_METRIC(touch_up_events); PHYSICAL_METRIC(touch_contact_events);
    PHYSICAL_METRIC(touch_reserved_events); PHYSICAL_METRIC(touch_queue_events);
    PHYSICAL_METRIC(touch_release_edges); PHYSICAL_METRIC(touch_error_cancels);
    PHYSICAL_METRIC(touch_no_data_held_samples); PHYSICAL_METRIC(touch_consecutive_errors);
    PHYSICAL_METRIC(touch_consecutive_errors_max);
    PHYSICAL_METRIC(touch_queue_coalesced); PHYSICAL_METRIC(touch_queue_overflows);
    PHYSICAL_METRIC(touch_queue_depth); PHYSICAL_METRIC(touch_queue_peak);
    PHYSICAL_METRIC(touch_sample_gap_min_us); PHYSICAL_METRIC(touch_sample_gap_max_us);
    PHYSICAL_METRIC(touch_fast_polls); PHYSICAL_METRIC(touch_sampler_core); PHYSICAL_METRIC(touch_sampler_running);
    PHYSICAL_METRIC(last_raw_x); PHYSICAL_METRIC(last_raw_y);
    PHYSICAL_METRIC(last_screen_x); PHYSICAL_METRIC(last_screen_y);
    PHYSICAL_METRIC(physical_pressed); PHYSICAL_METRIC(touch_present);
    PHYSICAL_METRIC(last_physical_touch_us);
#undef PHYSICAL_METRIC
    memcpy(snapshot.last_touch_packet, physical.last_touch_packet, 8);
    memcpy(snapshot.last_contact_packet, physical.last_contact_packet, 8);
    memcpy(snapshot.last_invalid_packet, physical.last_invalid_packet, 8);
    TouchFrameLatency latency_copy;
    portENTER_CRITICAL(&touch_latency_lock);
    latency_copy = touch_latency;
    portEXIT_CRITICAL(&touch_latency_lock);
    const auto latency = latency_copy.snapshot();
    snapshot.touch_last_delivered_sequence = latency.last_delivered_sequence;
    snapshot.touch_last_delivered_sample_us = latency.last_delivered_sample_us;
    snapshot.touch_frame_latency_samples = latency.samples;
    snapshot.touch_frame_latency_recent_samples = latency.recent_samples;
    snapshot.touch_frame_latency_pending = latency.pending;
    snapshot.touch_frame_latency_us_last = latency.last_us;
    snapshot.touch_frame_latency_us_max = latency.maximum_us;
    snapshot.touch_frame_latency_us_p50 = latency.p50_us;
    snapshot.touch_frame_latency_us_p95 = latency.p95_us;
    snapshot.touch_frame_latency_us_total = latency.total_us;
    snapshot.touch_frame_latency_last_sequence = latency.last_completed_sequence;
    snapshot.touch_frame_latency_dropped = latency.dropped;
    snapshot.touch_frame_latency_ignored_injected = latency.ignored_injected;
    memcpy(snapshot.touch_frame_latency_histogram, latency.histogram.data(),
           sizeof(snapshot.touch_frame_latency_histogram));
    if (sample_count) {
        std::sort(ordered.begin(), ordered.begin() + sample_count);
        snapshot.recent_frame_samples = sample_count;
        snapshot.recent_frame_p50_us = ordered[(sample_count - 1) / 2];
        snapshot.recent_frame_p95_us = ordered[(sample_count * 95 + 99) / 100 - 1];
    }
    snapshot.free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    snapshot.largest_dma_block = heap_caps_get_largest_free_block(MALLOC_CAP_DMA);
    snapshot.free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    return snapshot;
}

void board_inject_touch(uint16_t x, uint16_t y, bool pressed, bool enabled) {
    if (injection_enabled != enabled) {
        if (enabled) {
            portENTER_CRITICAL(&touch_latency_lock);
            touch_latency.discard_pending();
            portEXIT_CRITICAL(&touch_latency_lock);
        }
        portENTER_CRITICAL(&touch_lock);
        touch_queue_count = 0;
        reader_reset_requested = false;
        reader_block_until_release = touch_published.physical_pressed;
        portEXIT_CRITICAL(&touch_lock);
        delivered_touch.pressed = false;
        lv_indev_reset(touch_device, nullptr);
    }
    injection_x = std::min<uint16_t>(x, BOARD_WIDTH - 1);
    injection_y = std::min<uint16_t>(y, BOARD_HEIGHT - 1);
    injection_pressed = pressed;
    injection_enabled = enabled;
}
