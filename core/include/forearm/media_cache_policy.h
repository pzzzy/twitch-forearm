#pragma once
#include <cstddef>
#include <cstdint>
#include <limits>

namespace forearm {
// All transitions are made under the cache mutex. A lease covers both LVGL's
// image source and its decoder cache, not merely the current drawing call.
enum class MediaCacheState : std::uint8_t { Empty, Waiting, Downloading, Ready, Failed };
struct MediaCacheSlot {
    MediaCacheState state = MediaCacheState::Empty;
    unsigned pins = 0;
    std::uint64_t used = 0;
    std::int64_t wanted_until_ms = 0;
    std::int64_t retry_at_ms = 0;
    unsigned attempts = 0;
};
inline bool media_cache_pin(MediaCacheSlot& slot, std::uint64_t stamp) {
    if (slot.state != MediaCacheState::Ready || slot.pins == std::numeric_limits<unsigned>::max()) return false;
    ++slot.pins; slot.used = stamp; return true;
}
inline bool media_cache_unpin(MediaCacheSlot& slot) {
    if (slot.state != MediaCacheState::Ready || !slot.pins) return false;
    --slot.pins; return true;
}
// Return an empty/failed slot first, otherwise the least-recently used Ready
// slot that has no outstanding image/decoder lease. Never recycle queued work.
template<class SlotAt>
inline int media_cache_victim(unsigned count, SlotAt at, bool ready_only = false) {
    int best = -1;
    for (unsigned i = 0; i < count; ++i) {
        const auto& slot = at(i);
        if (slot.pins) continue;
        if (!ready_only && slot.state == MediaCacheState::Empty) return static_cast<int>(i);
        if (slot.state != MediaCacheState::Ready && (ready_only || slot.state != MediaCacheState::Failed)) continue;
        if (best < 0 || (!ready_only && slot.state == MediaCacheState::Failed && at(best).state != MediaCacheState::Failed) ||
            (slot.state == at(best).state && slot.used < at(best).used)) best = static_cast<int>(i);
    }
    return best;
}
inline std::int64_t media_cache_retry_delay(unsigned attempts) {
    return attempts <= 1 ? 30000 : attempts == 2 ? 120000 : 300000;
}
inline bool media_cache_retry_due(const MediaCacheSlot& slot, std::int64_t now_ms) {
    return slot.state == MediaCacheState::Failed && now_ms >= slot.retry_at_ms && now_ms < slot.wanted_until_ms;
}
// Retain a provider only after a failed refresh, never after a successfully
// parsed empty set. Channel records additionally require the identical room.
inline bool catalog_retain_provider(bool fetched, bool previous_valid, bool scope_matches) {
    return !fetched && previous_valid && scope_matches;
}
} // namespace forearm
