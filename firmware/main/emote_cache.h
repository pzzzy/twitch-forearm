#pragma once

#include <cstdint>
#include <cstddef>
#include "lvgl.h"
#include "forearm/media.h"

struct EmoteCacheStats {
    bool worker_running = false;
    std::uint32_t requested = 0;
    std::uint32_t ready = 0;
    std::uint32_t failed = 0;
    std::uint32_t rejected = 0;
    std::uint32_t downloads = 0;
    std::uint32_t bytes = 0;
    std::uint32_t decoded_bytes = 0;
    std::uint32_t generation = 0, evicted = 0, pinned = 0, lease_errors = 0;
    std::uint32_t catalog_generation = 0, catalog_retained = 0;
    std::uint32_t worker_stack_free = 0;
    std::uint32_t catalog_entries = 0, catalog_global_entries = 0, catalog_channel_entries = 0;
    std::uint32_t catalog_fetches = 0, catalog_failures = 0, catalog_truncated = 0;
    bool catalog_ready = false, catalog_channel_matched = false;
};

// Sixty-four LRU entries sharing a 512KiB pixel cap. Requests/metrics are
// thread-safe. Image leases and release are exclusively on the LVGL UI owner.
bool emote_cache_init();
bool emote_cache_request(const char* id);
bool emote_cache_dimensions(const char* id, unsigned& width, unsigned& height);
// The caller must detach every LVGL image using this descriptor BEFORE release.
// Last release invalidates LVGL's decoder cache before allowing worker eviction.
// No raw descriptor is valid without a lease. Display DMA reads composed panel
// framebuffers and does not borrow these asset pixels.
const lv_img_dsc_t* emote_cache_acquire(const char* id);
bool emote_cache_matches(const char* id, const lv_img_dsc_t* leased);
void emote_cache_release(const lv_img_dsc_t* image);
EmoteCacheStats emote_cache_stats();
// Called after resolving the selected Twitch broadcaster ID. Fetches public
// global/channel 7TV catalogs in the asset worker without any Twitch credential.
void emote_cache_set_channel(const char* twitch_channel_id);
// Nonblocking bounded enrichment for live events and explicitly synthetic USB
// fixtures. Existing native Twitch spans win. No HTTP or long catalog lock.
// Empty channel uses global catalog + emoji only. Returns total count <=8.
unsigned emote_cache_collect(const char* channel_id, const char* text,
                            forearm::MediaOccurrence* occurrences, unsigned count,
                            unsigned capacity = 8);
// Explicit diagnostics: enqueue a copied PNG fixture through the same bounded
// background decoder. Does not download, replace a ready asset, or imply a
// successful CDN request. The caller may release its bytes after this returns.
bool emote_cache_seed_png(const char* id, const std::uint8_t* png, std::size_t bytes);
