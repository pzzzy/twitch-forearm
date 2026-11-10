#pragma once

#include <cstdint>
#include <cstddef>
#include "lvgl.h"

struct EmoteCacheStats {
    bool worker_running = false;
    std::uint32_t requested = 0;
    std::uint32_t ready = 0;
    std::uint32_t failed = 0;
    std::uint32_t rejected = 0;
    std::uint32_t downloads = 0;
    std::uint32_t bytes = 0;
    std::uint32_t decoded_bytes = 0;
    std::uint32_t worker_stack_free = 0;
};

// Sixteen immutable session entries. Once ready, a descriptor and its pixels
// remain valid until reboot, including while display DMA references them.
bool emote_cache_init();
bool emote_cache_request(const char* id);
const lv_img_dsc_t* emote_cache_get(const char* id);
EmoteCacheStats emote_cache_stats();
// Explicit diagnostics: enqueue a copied PNG fixture through the same bounded
// background decoder. Does not download, replace a ready asset, or imply a
// successful CDN request. The caller may release its bytes after this returns.
bool emote_cache_seed_png(const char* id, const std::uint8_t* png, std::size_t bytes);
