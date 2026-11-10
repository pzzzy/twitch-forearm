#include "emote_cache.h"
#include "twitch_client.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "esp_crt_bundle.h"
#include "esp_heap_caps.h"
#include "esp_http_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

// LVGL bundles this C decoder. With LV_MEM_CUSTOM=1 its allocator wrappers use
// thread-safe libc malloc/free, not LVGL's UI-owned TLSF arena. No UI objects or
// image-decoder registration functions are touched from the asset worker.
#define LODEPNG_NO_COMPILE_CPP
extern "C" {
#include "src/extra/libs/png/lodepng.h"
}

static_assert(LV_MEM_CUSTOM == 1, "Background PNG decoding needs thread-safe custom allocation");
static_assert(LV_COLOR_DEPTH == 16, "Emote cache stores RGB565 plus alpha");

namespace {
constexpr unsigned kEntries = 16;
constexpr std::size_t kSourceLimit = 32768;
constexpr unsigned kDimensionLimit = 128;
constexpr std::uint32_t kPixelBudget = 512 * 1024;
enum class State : std::uint8_t { Empty, Waiting, Downloading, Ready, Failed };
struct Entry {
    char id[65]{};
    lv_img_dsc_t descriptor{};
    std::atomic<State> state{State::Empty};
    unsigned attempts = 0;
    std::int64_t retry_at_ms = 0;
    unsigned char* seeded_png = nullptr;
    std::size_t seeded_bytes = 0;
};
Entry entries[kEntries];
QueueHandle_t requests = nullptr;
StaticQueue_t request_control;
std::uint8_t request_storage[kEntries];
portMUX_TYPE cache_mux = portMUX_INITIALIZER_UNLOCKED;
EmoteCacheStats statistics{};
std::int64_t cdn_backoff_until_ms = 0;

std::int64_t now_ms() { return esp_timer_get_time() / 1000; }
bool valid_id(const char* id) {
    if (!id || !*id || std::strlen(id) > 64) return false;
    for (const auto* p = id; *p; ++p) {
        const auto c = static_cast<unsigned char>(*p);
        if (!std::isalnum(c) && c != '_' && c != '-') return false;
    }
    return true;
}
struct Download {
    unsigned char* bytes = nullptr;
    std::size_t length = 0;
    bool okay = false;
    unsigned retry_after_seconds = 0;
};
esp_err_t header_event(esp_http_client_event_t* event) {
    auto* response = static_cast<Download*>(event->user_data);
    if (event->event_id == HTTP_EVENT_ON_HEADER && event->header_key && event->header_value &&
        !strcasecmp(event->header_key, "Retry-After")) {
        response->retry_after_seconds = std::clamp(std::atoi(event->header_value), 1, 600);
    }
    return ESP_OK;
}
Download download(const char* id) {
    Download result;
    // Only an ASCII identifier is interpolated; origin/path/format/theme/size
    // are fixed. No credential, arbitrary URL, redirects or animated asset.
    char url[176];
    const int count = std::snprintf(url, sizeof(url),
        "https://static-cdn.jtvnw.net/emoticons/v2/%s/static/dark/1.0", id);
    if (count < 0 || static_cast<std::size_t>(count) >= sizeof(url)) return result;
    esp_http_client_config_t config{};
    config.url = url;
    config.timeout_ms = 6000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.buffer_size = 1024;
    config.buffer_size_tx = 512;
    config.event_handler = header_event;
    config.user_data = &result;
    config.user_agent = "ForearmChat/0.2";
    auto client = esp_http_client_init(&config);
    if (!client) return result;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        const auto content_length = esp_http_client_fetch_headers(client);
        const auto status = esp_http_client_get_status_code(client);
        portENTER_CRITICAL(&cache_mux); ++statistics.downloads; portEXIT_CRITICAL(&cache_mux);
        if (status == 429) result.retry_after_seconds = std::max(result.retry_after_seconds, 60u);
        if (status == 200 && content_length >= -1 && content_length <= static_cast<std::int64_t>(kSourceLimit)) {
            result.bytes = static_cast<unsigned char*>(heap_caps_malloc(kSourceLimit, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (result.bytes) {
                bool complete = false;
                while (result.length < kSourceLimit) {
                    const auto got = esp_http_client_read(client,
                        reinterpret_cast<char*>(result.bytes + result.length),
                        std::min<std::size_t>(1024, kSourceLimit - result.length));
                    if (got < 0) break;
                    if (got > 0) result.length += got;
                    if (esp_http_client_is_complete_data_received(client)) { complete = true; break; }
                    if (!got) break;
                    // Yield between chunks. EventSub/auth use higher priority
                    // on this core; touch/rendering have their own UI owner.
                    vTaskDelay(pdMS_TO_TICKS(1));
                }
                result.okay = complete && result.length >= 33;
            }
        }
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    return result;
}

bool decode(Entry& entry, const Download& source) {
    constexpr unsigned char signature[] = {137,80,78,71,13,10,26,10};
    if (!source.okay || source.length < 33 || std::memcmp(source.bytes, signature, sizeof(signature))) return false;
    LodePNGState state;
    lodepng_state_init(&state);
    state.info_raw.colortype = LCT_RGBA;
    state.info_raw.bitdepth = 8;
    state.decoder.zlibsettings.max_output_size = kDimensionLimit * kDimensionLimit * 8 + 2048;
#ifdef LODEPNG_COMPILE_ANCILLARY_CHUNKS
    state.decoder.read_text_chunks = 0;
    state.decoder.remember_unknown_chunks = 0;
    state.decoder.max_text_size = 4096;
    state.decoder.max_icc_size = 4096;
#endif
    unsigned width = 0, height = 0;
    unsigned char* rgba = nullptr;
    auto error = lodepng_inspect(&width, &height, &state, source.bytes, source.length);
    if (!error && width && height && width <= kDimensionLimit && height <= kDimensionLimit) {
        error = lodepng_decode(&rgba, &width, &height, &state, source.bytes, source.length);
    } else error = 1;
    lodepng_state_cleanup(&state);
    if (error || !rgba || !width || !height || width > kDimensionLimit || height > kDimensionLimit) {
        lv_mem_free(rgba);
        return false;
    }
    const std::uint32_t bytes = width * height * 3;
    portENTER_CRITICAL(&cache_mux); const bool fits = statistics.decoded_bytes + bytes <= kPixelBudget; portEXIT_CRITICAL(&cache_mux);
    auto* output = fits ? static_cast<std::uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) : nullptr;
    if (!output) { lv_mem_free(rgba); return false; }
    for (unsigned i = 0; i < width * height; ++i) {
        const auto* pixel = rgba + i * 4;
        std::uint16_t rgb = (static_cast<std::uint16_t>(pixel[0] >> 3) << 11) |
                            (static_cast<std::uint16_t>(pixel[1] >> 2) << 5) |
                            static_cast<std::uint16_t>(pixel[2] >> 3);
#if LV_COLOR_16_SWAP
        rgb = (rgb >> 8) | (rgb << 8);
#endif
        output[i * 3] = rgb & 255;
        output[i * 3 + 1] = rgb >> 8;
        output[i * 3 + 2] = pixel[3];
    }
    lv_mem_free(rgba);
    entry.descriptor.header.always_zero = 0;
    entry.descriptor.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    entry.descriptor.header.w = width;
    entry.descriptor.header.h = height;
    entry.descriptor.data_size = bytes;
    entry.descriptor.data = output;
    portENTER_CRITICAL(&cache_mux);
    ++statistics.ready; statistics.bytes += source.length; statistics.decoded_bytes += bytes;
    portEXIT_CRITICAL(&cache_mux);
    // Release publishes all descriptor fields and pixel bytes together. No
    // ready entry is overwritten or evicted, so the UI may retain this pointer.
    entry.state.store(State::Ready, std::memory_order_release);
    return true;
}

void worker(void*) {
    portENTER_CRITICAL(&cache_mux); statistics.worker_running = true; portEXIT_CRITICAL(&cache_mux);
    for (;;) {
        const auto stack_free = uxTaskGetStackHighWaterMark(nullptr);
        portENTER_CRITICAL(&cache_mux); statistics.worker_stack_free = stack_free; portEXIT_CRITICAL(&cache_mux);
        std::uint8_t slot = 0;
        if (xQueueReceive(requests, &slot, pdMS_TO_TICKS(200)) != pdTRUE || slot >= kEntries) continue;
        auto& entry = entries[slot];
        if (entry.state.load(std::memory_order_acquire) != State::Waiting) continue;
        // Keep the ID queued during radio setup. This does not spend retries or
        // start a TLS handshake before Wi-Fi and certificate time are ready.
        portENTER_CRITICAL(&cache_mux); const bool has_seed = entry.seeded_png != nullptr; portEXIT_CRITICAL(&cache_mux);
        const auto network = twitch_status();
        if (!has_seed && (!network.wifi_connected || !network.time_synchronized || network.login_pending ||
                         twitch_network_busy() || now_ms() < cdn_backoff_until_ms)) {
            // Requeue instead of retaining one blocked item: an offline seed
            // behind it must still be able to exercise the hardware decoder.
            xQueueSend(requests, &slot, 0);
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        entry.state.store(State::Downloading, std::memory_order_release);
        ++entry.attempts;
        Download source;
        portENTER_CRITICAL(&cache_mux);
        source.bytes = entry.seeded_png; source.length = entry.seeded_bytes;
        entry.seeded_png = nullptr; entry.seeded_bytes = 0;
        portEXIT_CRITICAL(&cache_mux);
        if (source.bytes) source.okay = true;
        else source = download(entry.id);
        const bool okay = decode(entry, source);
        heap_caps_free(source.bytes);
        if (!okay) {
            if (source.retry_after_seconds) cdn_backoff_until_ms = now_ms() + source.retry_after_seconds * 1000LL;
            entry.retry_at_ms = now_ms() + (entry.attempts == 1 ? 30000 : 120000);
            entry.state.store(State::Failed, std::memory_order_release);
            portENTER_CRITICAL(&cache_mux); ++statistics.failed; portEXIT_CRITICAL(&cache_mux);
        }
        // Never start a stream of downloads faster than one per second.
        cdn_backoff_until_ms = std::max(cdn_backoff_until_ms, now_ms() + 1000);
    }
}
} // namespace

bool emote_cache_init() {
    if (requests) return true;
    requests = xQueueCreateStatic(kEntries, sizeof(std::uint8_t), request_storage, &request_control);
    if (requests && xTaskCreatePinnedToCore(worker, "emote-cache", 12288, nullptr, 2, nullptr, 0) == pdPASS) return true;
    if (requests) vQueueDelete(requests);
    requests = nullptr;
    return false;
}
bool emote_cache_request(const char* id) {
    if (!requests || !valid_id(id)) {
        portENTER_CRITICAL(&cache_mux); ++statistics.rejected; portEXIT_CRITICAL(&cache_mux);
        return false;
    }
    int chosen = -1;
    int vacant = -1;
    bool matched = false;
    bool already = false;
    portENTER_CRITICAL(&cache_mux);
    for (unsigned i = 0; i < kEntries; ++i) {
        const auto state = entries[i].state.load(std::memory_order_acquire);
        if (state != State::Empty && !std::strcmp(entries[i].id, id)) {
            matched = true;
            if (state == State::Ready || state == State::Waiting || state == State::Downloading) already = true;
            else if (entries[i].attempts < 3 && now_ms() >= entries[i].retry_at_ms) chosen = i;
            break;
        }
        if (state == State::Empty && vacant < 0) vacant = i;
    }
    if (!matched) chosen = vacant;
    if (!already && chosen >= 0) {
        auto& entry = entries[chosen];
        if (entry.state.load(std::memory_order_relaxed) == State::Empty) {
            std::memcpy(entry.id, id, std::strlen(id) + 1);
            ++statistics.requested;
        }
        entry.state.store(State::Waiting, std::memory_order_release);
    }
    if (!already && chosen < 0 && !matched) ++statistics.rejected;
    portEXIT_CRITICAL(&cache_mux);
    if (already) return true;
    if (chosen < 0) return false;
    const auto slot = static_cast<std::uint8_t>(chosen);
    if (xQueueSend(requests, &slot, 0) == pdTRUE) return true;
    entries[chosen].state.store(State::Failed, std::memory_order_release);
    portENTER_CRITICAL(&cache_mux); ++statistics.rejected; portEXIT_CRITICAL(&cache_mux);
    return false;
}
const lv_img_dsc_t* emote_cache_get(const char* id) {
    if (!valid_id(id)) return nullptr;
    for (const auto& entry : entries) {
        if (entry.state.load(std::memory_order_acquire) == State::Ready && !std::strcmp(entry.id, id))
            return &entry.descriptor;
    }
    return nullptr;
}
EmoteCacheStats emote_cache_stats() {
    portENTER_CRITICAL(&cache_mux); const auto snapshot = statistics; portEXIT_CRITICAL(&cache_mux);
    return snapshot;
}
bool emote_cache_seed_png(const char* id, const std::uint8_t* png, std::size_t bytes) {
    if (!requests || !valid_id(id) || !png || bytes < 33 || bytes > kSourceLimit) return false;
    auto* copy = static_cast<unsigned char*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    if (!copy) return false;
    std::memcpy(copy, png, bytes);
    int matching = -1, vacant = -1;
    bool enqueue = false;
    portENTER_CRITICAL(&cache_mux);
    for (unsigned i = 0; i < kEntries; ++i) {
        const auto state = entries[i].state.load(std::memory_order_acquire);
        if (state != State::Empty && !std::strcmp(entries[i].id, id)) { matching = i; break; }
        if (state == State::Empty && vacant < 0) vacant = i;
    }
    const int chosen = matching >= 0 ? matching : vacant;
    if (chosen >= 0) {
        auto& entry = entries[chosen];
        const auto state = entry.state.load(std::memory_order_acquire);
        if (state != State::Ready && state != State::Downloading && !entry.seeded_png) {
            if (state == State::Empty) {
                std::memcpy(entry.id, id, std::strlen(id) + 1); ++statistics.requested;
            }
            entry.seeded_png = copy; entry.seeded_bytes = bytes;
            enqueue = state != State::Waiting;
            entry.state.store(State::Waiting, std::memory_order_release);
            copy = nullptr;
        }
    }
    portEXIT_CRITICAL(&cache_mux);
    if (copy) { heap_caps_free(copy); return false; }
    if (!enqueue) return true;
    const auto slot = static_cast<std::uint8_t>(chosen);
    if (xQueueSend(requests, &slot, 0) == pdTRUE) return true;
    portENTER_CRITICAL(&cache_mux);
    auto* unused = entries[chosen].seeded_png;
    entries[chosen].seeded_png = nullptr; entries[chosen].seeded_bytes = 0;
    entries[chosen].state.store(State::Failed, std::memory_order_release);
    ++statistics.rejected;
    portEXIT_CRITICAL(&cache_mux);
    heap_caps_free(unused);
    return false;
}
