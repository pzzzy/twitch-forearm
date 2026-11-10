#pragma once

#include <cstdint>
#include "lvgl.h"

constexpr int BOARD_WIDTH = 320;
constexpr int BOARD_HEIGHT = 480;

struct BoardStats {
    uint32_t logical_flushes = 0;
    uint32_t logical_nonfinal_flushes = 0;
    bool internal_strip_render = false;
    bool asynchronous_transport = false;
    bool display_worker_running = false;
    bool display_transfer_pending = false;
    bool display_fault = false;
    bool staged_transport_copy = false;
    uint32_t render_submissions = 0;
    // UI-owner profiling: exact refresh timer wall time and render-start to
    // final logical flush (before waiting for transport buffer ownership).
    uint64_t lvgl_refresh_us_total = 0;
    uint32_t lvgl_refresh_us_last = 0, lvgl_refresh_us_max = 0;
    uint64_t render_cpu_us_total = 0;
    uint32_t render_cpu_us_last = 0, render_cpu_us_max = 0;
    uint64_t sync_copy_us_total = 0, sync_copy_pixels = 0;
    uint32_t sync_copy_calls = 0, sync_copy_us_last = 0, sync_copy_us_max = 0;
    uint64_t frame_queue_wait_us_total = 0;
    uint32_t frame_queue_wait_us_last = 0;
    uint32_t frame_queue_wait_us_max = 0;
    uint64_t te_wait_us_total = 0;
    uint32_t te_wait_us_last = 0;
    uint32_t te_wait_us_max = 0;
    uint64_t convert_us_total = 0;
    uint32_t convert_us_last = 0;
    uint32_t convert_us_max = 0;
    uint64_t dma_wait_us_total = 0;
    uint32_t dma_wait_us_last = 0;
    uint32_t dma_wait_us_max = 0;
    uint64_t spi_submit_us_total = 0;
    uint32_t spi_submit_us_last = 0;
    uint32_t spi_submit_us_max = 0;
    uint32_t payload_us_last = 0;
    uint32_t payload_us_max = 0;
    uint32_t job_dispatch_us_last = 0;
    uint32_t job_dispatch_us_max = 0;
    uint32_t physical_press_sequence = 0;
    uint32_t touch_last_delivered_sequence = 0;
    uint64_t touch_last_delivered_sample_us = 0;
    uint32_t touch_frame_latency_samples = 0;
    uint32_t touch_frame_latency_recent_samples = 0;
    uint32_t touch_frame_latency_pending = 0;
    uint32_t touch_frame_latency_us_last = 0;
    uint32_t touch_frame_latency_us_max = 0;
    uint32_t touch_frame_latency_us_p50 = 0;
    uint32_t touch_frame_latency_us_p95 = 0;
    uint64_t touch_frame_latency_us_total = 0;
    uint32_t touch_frame_latency_last_sequence = 0;
    uint32_t touch_frame_latency_dropped = 0;
    uint32_t touch_frame_latency_ignored_injected = 0;
    // 10 ms buckets: [0,10ms), [10,20ms), ...; last bucket is >=150ms.
    uint32_t touch_frame_latency_histogram[16]{};
    uint64_t compose_copy_us_total = 0;
    uint32_t compose_copy_us_max = 0;
    uint64_t composed_pixels = 0;
    uint32_t frames = 0;
    uint64_t frame_us_total = 0;
    uint32_t last_frame_us = 0;
    uint32_t max_frame_us = 0;
    uint32_t recent_frame_samples = 0;
    uint32_t recent_frame_p50_us = 0;
    uint32_t recent_frame_p95_us = 0;
    uint32_t flush_errors = 0;
    uint32_t te_edges = 0;
    uint32_t te_wait_timeouts = 0;
    uint32_t touch_polls = 0;
    uint32_t touch_errors = 0;
    uint32_t touch_contacts = 0;  // Physical rising contacts, excludes injection.
    uint32_t touch_pressed_samples = 0;
    uint32_t injected_samples = 0;
    uint32_t invalid_touch_samples = 0;
    uint32_t touch_zero_points = 0;
    uint32_t touch_many_points = 0;
    uint32_t touch_out_of_bounds = 0;
    uint32_t touch_all_ff = 0;
    uint32_t touch_idle_packets = 0;
    uint32_t touch_nonzero_headers = 0;
    uint32_t touch_down_events = 0;
    uint32_t touch_up_events = 0;
    uint32_t touch_contact_events = 0;
    uint32_t touch_reserved_events = 0;
    uint32_t touch_release_edges = 0;
    uint32_t touch_error_cancels = 0;
    uint32_t touch_no_data_held_samples = 0;
    uint32_t touch_consecutive_errors = 0;
    uint32_t touch_consecutive_errors_max = 0;
    uint32_t touch_queue_events = 0;
    uint32_t touch_queue_coalesced = 0;
    uint32_t touch_queue_overflows = 0;
    uint32_t touch_queue_depth = 0;
    uint32_t touch_queue_peak = 0;
    uint32_t touch_sample_gap_max_us = 0;
    bool touch_sampler_running = false;
    uint8_t last_touch_packet[8]{};
    uint8_t last_contact_packet[8]{};
    uint8_t last_invalid_packet[8]{};
    uint16_t last_raw_x = 0;
    uint16_t last_raw_y = 0;
    uint16_t last_screen_x = 0;
    uint16_t last_screen_y = 0;
    bool physical_pressed = false;
    bool touch_present = false;
    uint64_t last_physical_touch_us = 0;
    uint32_t free_internal = 0;
    uint32_t largest_dma_block = 0;
    uint32_t free_psram = 0;
};

// Call after lv_init(), from the sole LVGL-owning task.
void board_display_init();
void board_set_brightness(int percent);
lv_disp_t* board_display();
lv_indev_t* board_touch_indev();
BoardStats board_stats();
uint32_t board_frame_count(); // Cheap UI-owner counter; no histogram sorting.
uint32_t board_render_count(); // Logical submissions; physical count stays separate.
void board_reset_touch_latency(); // UI owner; clear measurements before a manual run.
// Diagnostics A/B only; applies to the next physical frame. Both modes emit
// identical RGB565 bytes and preserve TE/DMA ownership. Default is false.
void board_set_staged_transport_copy(bool enabled);

// Latest complete composition, ordinary host-order RGB565, LV_COLOR_16_SWAP=0.
// Only inspect/copy on the LVGL-owning task outside lv_timer_handler(). In async
// mode the completed composition may still be transmitting to the panel.
const uint16_t* board_framebuffer();

// Diagnostics only. Injection never increments physical touch counters. Set
// enabled=false after the final release to return to actual controller input.
void board_inject_touch(uint16_t x, uint16_t y, bool pressed, bool enabled = true);
