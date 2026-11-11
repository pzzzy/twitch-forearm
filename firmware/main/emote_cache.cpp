#include "emote_cache.h"
#include "twitch_client.h"
#include "forearm/seventv_catalog.h"
#include "forearm/catalog_store.h"
#include "forearm/media_enrich.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

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
constexpr unsigned kEntries = 64;
constexpr std::size_t kSourceLimit = 32768;
constexpr unsigned kDimensionLimit = 128;
constexpr std::uint32_t kPixelBudget = 512 * 1024;
enum class State : std::uint8_t { Empty, Waiting, Downloading, Ready, Failed };
struct Entry {
    char id[forearm::kMediaIdBytes]{};
    lv_img_dsc_t descriptor{};
    std::atomic<State> state{State::Empty};
    unsigned attempts = 0;
    std::int64_t retry_at_ms = 0;
    unsigned char* seeded_png = nullptr;
    std::size_t seeded_bytes = 0;
};
Entry* entries = nullptr;
std::atomic<bool> initialized{false}, initializing{false};
QueueHandle_t requests = nullptr;
StaticQueue_t request_control;
std::uint8_t request_storage[kEntries];
portMUX_TYPE cache_mux = portMUX_INITIALIZER_UNLOCKED;
EmoteCacheStats statistics{};
std::int64_t cdn_backoff_until_ms = 0;
std::int64_t provider_backoff_until_ms[3]{};
unsigned provider(const char* id) { return !std::strncmp(id, "7tv:", 4) ? 1 : !std::strncmp(id, "emoji:", 6) ? 2 : 0; }
constexpr unsigned kCatalogEntries = 512;
struct Catalog {
    forearm::CatalogEmote items[kCatalogEntries]{};
    unsigned count = 0, skipped = 0;
    char channel[33]{};
    bool global_ready = false, channel_ready = false, channel_set = false;
};
Catalog* catalogs[2]{};
unsigned catalog_readers[2]{};
int active_catalog = -1, building_catalog = -1;
char desired_channel[33]{};
unsigned catalog_generation = 0, building_generation = 0, catalog_phase = 0;
std::int64_t catalog_due_ms = 0, catalog_retry_after_ms = 0;

std::int64_t now_ms() { return esp_timer_get_time() / 1000; }
bool valid_id(const char* id) {
    char url[256];
    return id && std::strlen(id) < forearm::kMediaIdBytes && forearm::media_url(id, url, sizeof(url));
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
    const auto deadline = now_ms() + 12000;
    // Only an ASCII identifier is interpolated; origin/path/format/theme/size
    // are fixed. No credential, arbitrary URL, redirects or animated asset.
    char url[256];
    if (!forearm::media_url(id, url, sizeof(url))) return result;
    esp_http_client_config_t config{};
    config.url = url;
    config.timeout_ms = 6000;
    config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true;
    config.buffer_size = 1024;
    config.buffer_size_tx = 512;
    config.event_handler = header_event;
    config.user_data = &result;
    config.user_agent = "ForearmChat/0.5";
    auto client = esp_http_client_init(&config);
    if (!client) return result;
    if (esp_http_client_open(client, 0) == ESP_OK) {
        esp_http_client_set_timeout_ms(client, std::clamp<std::int64_t>(deadline - now_ms(), 1, 6000));
        const auto content_length = esp_http_client_fetch_headers(client);
        const auto status = esp_http_client_get_status_code(client);
        portENTER_CRITICAL(&cache_mux); ++statistics.downloads; portEXIT_CRITICAL(&cache_mux);
        if (status == 429) result.retry_after_seconds = std::max(result.retry_after_seconds, 60u);
        if (status == 200 && content_length >= -1 && content_length <= static_cast<std::int64_t>(kSourceLimit)) {
            result.bytes = static_cast<unsigned char*>(heap_caps_malloc(kSourceLimit, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
            if (result.bytes) {
                bool complete = false;
                while (result.length < kSourceLimit && now_ms() < deadline) {
                    esp_http_client_set_timeout_ms(client, std::clamp<std::int64_t>(deadline - now_ms(), 1, 6000));
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
                result.okay = complete && result.length >= 33 && now_ms() <= deadline;
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
    unsigned output_width = 0, output_height = 0;
    forearm::media_dimensions(width, height, output_width, output_height);
    const std::uint32_t bytes = output_width * output_height * 3;
    portENTER_CRITICAL(&cache_mux); const bool fits = statistics.decoded_bytes + bytes <= kPixelBudget; portEXIT_CRITICAL(&cache_mux);
    auto* output = fits ? static_cast<std::uint8_t*>(heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) : nullptr;
    if (!output) { lv_mem_free(rgba); return false; }
    for (unsigned y = 0; y < output_height; ++y) for (unsigned x = 0; x < output_width; ++x) {
        const auto i = y * output_width + x;
        const auto source_x = std::min(width - 1, (x * width + output_width / 2) / output_width);
        const auto source_y = std::min(height - 1, (y * height + output_height / 2) / output_height);
        const auto* pixel = rgba + (source_y * width + source_x) * 4;
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
    entry.descriptor.header.w = output_width;
    entry.descriptor.header.h = output_height;
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

void add_catalog_emote(void* context, const forearm::CatalogEmote& emote) {
    auto& catalog = *static_cast<Catalog*>(context);
    if (!forearm::catalog_add(catalog.items, catalog.count, kCatalogEntries, emote)) ++catalog.skipped;
}

bool fetch_catalog(Catalog& catalog, bool global, bool* has_channel_set = nullptr) {
    if (has_channel_set) *has_channel_set = false;
    char url[128];
    if (global) std::strcpy(url, "https://7tv.io/v3/emote-sets/global");
    else std::snprintf(url, sizeof(url), "https://7tv.io/v3/users/twitch/%s", catalog.channel);
    Download headers;
    esp_http_client_config_t config{};
    config.url = url; config.timeout_ms = 4000; config.crt_bundle_attach = esp_crt_bundle_attach;
    config.disable_auto_redirect = true; config.buffer_size = 1024; config.buffer_size_tx = 512;
    config.event_handler = header_event; config.user_data = &headers; config.user_agent = "ForearmChat/0.5";
    auto client = esp_http_client_init(&config);
    if (!client) return false;
    bool okay = false;
    portENTER_CRITICAL(&cache_mux); ++statistics.catalog_fetches; portEXIT_CRITICAL(&cache_mux);
    if (esp_http_client_open(client, 0) == ESP_OK) {
        const auto content_length = esp_http_client_fetch_headers(client);
        const auto status = esp_http_client_get_status_code(client);
        if (!global && status == 404) okay = true; // No 7TV account: globals still work.
        if (status == 200 && content_length >= -1 && content_length <= 4 * 1024 * 1024) {
            forearm::SevenTvCatalogReader parser(add_catalog_emote, &catalog, global);
            char chunk[1024]; bool complete = false, valid = true;
            const auto deadline = now_ms() + 15000;
            while (valid && now_ms() < deadline) {
                const auto got = esp_http_client_read(client, chunk, sizeof(chunk));
                if (got < 0) break;
                if (got > 0) valid = parser.feed(chunk, got);
                if (esp_http_client_is_complete_data_received(client)) { complete = true; break; }
                if (!got) break;
                vTaskDelay(pdMS_TO_TICKS(1));
            }
            okay = complete && valid && parser.finish();
            if (okay && has_channel_set) *has_channel_set = parser.has_channel_set();
        }
        if (status == 429 || headers.retry_after_seconds)
            catalog_retry_after_ms = now_ms() + std::max(headers.retry_after_seconds, 60u) * 1000LL;
        esp_http_client_close(client);
    }
    esp_http_client_cleanup(client);
    if (!okay) { portENTER_CRITICAL(&cache_mux); ++statistics.catalog_failures; portEXIT_CRITICAL(&cache_mux); }
    return okay;
}

void catalog_step(const TwitchStatus& network) {
    if (!network.wifi_connected || !network.time_synchronized || !network.chat_connected || network.login_pending ||
        twitch_network_busy() || now_ms() < catalog_retry_after_ms) return;
    unsigned generation = 0; char channel[33];
    portENTER_CRITICAL(&cache_mux);
    generation = catalog_generation; std::strcpy(channel, desired_channel);
    if (building_catalog >= 0 && generation != building_generation) { building_catalog = -1; catalog_phase = 0; catalog_due_ms = 0; }
    if (building_catalog < 0 && now_ms() >= catalog_due_ms) {
        const int slot = active_catalog == 0 ? 1 : 0;
        if (!catalog_readers[slot]) { building_catalog = slot; building_generation = generation; catalog_phase = 1; }
    }
    const auto slot = building_catalog; const auto phase = catalog_phase;
    portEXIT_CRITICAL(&cache_mux);
    if (slot < 0) return;
    auto& catalog = *catalogs[slot];
    if (phase == 1) {
        catalog.count = catalog.skipped = 0; std::strcpy(catalog.channel, channel);
        catalog.global_ready = fetch_catalog(catalog, true);
        if (!catalog.global_ready) catalog.count = 0; // Never publish a partial/malformed document.
        catalog.channel_ready = catalog.channel_set = false;
        catalog_phase = 2;
        return; // Assets get a turn before the second bounded HTTP request.
    }
    const auto before_channel = catalog.count;
    if (*catalog.channel) catalog.channel_ready = fetch_catalog(catalog, false, &catalog.channel_set);
    else catalog.channel_ready = true;
    if (!catalog.channel_ready) {
        // Channel aliases append after immutable globals. Roll back only that
        // response on malformed JSON/transport failure; globals stay usable.
        catalog.count = before_channel; catalog.channel_set = false;
    }
    std::sort(catalog.items, catalog.items + catalog.count,
        [](const forearm::CatalogEmote& a, const forearm::CatalogEmote& b) { return std::strcmp(a.name, b.name) < 0; });
    unsigned global_count = 0;
    for (unsigned i = 0; i < catalog.count; ++i) if (catalog.items[i].global) ++global_count;
    portENTER_CRITICAL(&cache_mux);
    if (generation == catalog_generation) {
        active_catalog = slot;
        statistics.catalog_entries = catalog.count; statistics.catalog_global_entries = global_count;
        statistics.catalog_channel_entries = catalog.count - global_count;
        statistics.catalog_ready = catalog.global_ready || catalog.channel_ready;
        statistics.catalog_channel_matched = catalog.channel_ready && catalog.channel_set && *catalog.channel;
        statistics.catalog_truncated += catalog.skipped;
        catalog_due_ms = now_ms() + ((catalog.global_ready && catalog.channel_ready) ? 15 * 60 * 1000LL : 60000);
    } else catalog_due_ms = 0;
    building_catalog = -1; catalog_phase = 0;
    portEXIT_CRITICAL(&cache_mux);
}

void worker(void*) {
    portENTER_CRITICAL(&cache_mux); statistics.worker_running = true; portEXIT_CRITICAL(&cache_mux);
    for (;;) {
        const auto stack_free = uxTaskGetStackHighWaterMark(nullptr);
        portENTER_CRITICAL(&cache_mux); statistics.worker_stack_free = stack_free; portEXIT_CRITICAL(&cache_mux);
        std::uint8_t slot = 0;
        if (xQueueReceive(requests, &slot, pdMS_TO_TICKS(200)) != pdTRUE || slot >= kEntries) {
            catalog_step(twitch_status());
            continue;
        }
        auto& entry = entries[slot];
        if (entry.state.load(std::memory_order_acquire) != State::Waiting) continue;
        // Keep the ID queued during radio setup. This does not spend retries or
        // start a TLS handshake before Wi-Fi and certificate time are ready.
        portENTER_CRITICAL(&cache_mux); const bool has_seed = entry.seeded_png != nullptr; portEXIT_CRITICAL(&cache_mux);
        const auto network = twitch_status();
        if (!has_seed && (!network.wifi_connected || !network.time_synchronized || network.login_pending ||
                         twitch_network_busy() || now_ms() < cdn_backoff_until_ms ||
                         now_ms() < provider_backoff_until_ms[provider(entry.id)])) {
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
            if (source.retry_after_seconds) provider_backoff_until_ms[provider(entry.id)] = now_ms() + source.retry_after_seconds * 1000LL;
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
    if (initialized.load(std::memory_order_acquire)) return true;
    if (initializing.exchange(true)) return false;
    entries = static_cast<Entry*>(heap_caps_malloc(sizeof(Entry) * kEntries, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT));
    for (unsigned i = 0; entries && i < kEntries; ++i) new (entries + i) Entry();
    for (auto& catalog : catalogs) {
        auto* memory = heap_caps_malloc(sizeof(Catalog), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
        catalog = memory ? new (memory) Catalog() : nullptr;
    }
    requests = xQueueCreateStatic(kEntries, sizeof(std::uint8_t), request_storage, &request_control);
    if (entries && catalogs[0] && catalogs[1] && requests && xTaskCreatePinnedToCore(worker, "emote-cache", 12288, nullptr, 2, nullptr, 0) == pdPASS) {
        initialized.store(true, std::memory_order_release); initializing.store(false); return true;
    }
    if (requests) vQueueDelete(requests);
    requests = nullptr; heap_caps_free(entries); entries = nullptr;
    heap_caps_free(catalogs[0]); heap_caps_free(catalogs[1]); catalogs[0] = catalogs[1] = nullptr;
    initializing.store(false);
    return false;
}
bool emote_cache_request(const char* id) {
    if (!initialized.load(std::memory_order_acquire) || !valid_id(id)) {
        portENTER_CRITICAL(&cache_mux); ++statistics.rejected; portEXIT_CRITICAL(&cache_mux);
        return false;
    }
    int chosen = -1;
    int vacant = -1;
    int reclaim = -1;
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
        if (state == State::Failed && now_ms() >= entries[i].retry_at_ms && !entries[i].seeded_png && reclaim < 0) reclaim = i;
    }
    if (!matched) chosen = vacant >= 0 ? vacant : reclaim;
    if (!already && chosen >= 0) {
        auto& entry = entries[chosen];
        if (!matched) {
            std::memcpy(entry.id, id, std::strlen(id) + 1);
            entry.attempts = 0; entry.retry_at_ms = 0;
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
    if (!initialized.load(std::memory_order_acquire) || !valid_id(id)) return nullptr;
    for (unsigned i = 0; i < kEntries; ++i) {
        const auto& entry = entries[i];
        if (entry.state.load(std::memory_order_acquire) == State::Ready && !std::strcmp(entry.id, id))
            return &entry.descriptor;
    }
    return nullptr;
}
EmoteCacheStats emote_cache_stats() {
    portENTER_CRITICAL(&cache_mux); const auto snapshot = statistics; portEXIT_CRITICAL(&cache_mux);
    return snapshot;
}
void emote_cache_set_channel(const char* channel) {
    const auto value = std::string_view(channel ? channel : "");
    if (value.size() > 32) return;
    for (const auto c : value) if (c < '0' || c > '9') return;
    portENTER_CRITICAL(&cache_mux);
    if (value != desired_channel) {
        std::memcpy(desired_channel, value.data(), value.size()); desired_channel[value.size()] = 0;
        ++catalog_generation; catalog_due_ms = 0;
        statistics.catalog_channel_matched = false;
    }
    portEXIT_CRITICAL(&cache_mux);
    emote_cache_init();
}
unsigned emote_cache_collect(const char* channel, const char* text,
                            forearm::MediaOccurrence* output, unsigned count, unsigned capacity) {
    capacity = std::min(capacity, 8u); count = std::min(count, capacity);
    if (!text || !output || count >= capacity) return count;
    const std::string_view message(text);
    if (message.size() > 2048) return count;
    int slot = -1;
    portENTER_CRITICAL(&cache_mux);
    if (initialized.load(std::memory_order_acquire) && active_catalog >= 0) {
        slot = active_catalog; ++catalog_readers[slot];
    }
    portEXIT_CRITICAL(&cache_mux);
    const Catalog* catalog = slot >= 0 ? catalogs[slot] : nullptr;
    const bool channel_matches = catalog && channel && *channel && !std::strcmp(channel, catalog->channel);
    auto lookup = [catalog, channel_matches](std::string_view name) -> const char* {
        if (!catalog) return nullptr;
        const auto* found = forearm::catalog_find(catalog->items, catalog->count, name, channel_matches && catalog->channel_set);
        return found ? found->id : nullptr;
    };
    count = forearm::collect_media(message, output, count, capacity, lookup);
    if (slot >= 0) { portENTER_CRITICAL(&cache_mux); --catalog_readers[slot]; portEXIT_CRITICAL(&cache_mux); }
    return count;
}
bool emote_cache_seed_png(const char* id, const std::uint8_t* png, std::size_t bytes) {
    if (!initialized.load(std::memory_order_acquire) || !valid_id(id) || !png || bytes < 33 || bytes > kSourceLimit) return false;
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
