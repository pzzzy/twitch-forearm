#pragma once
#include <algorithm>
#include <cstdint>

namespace forearm {
// Absolute-time schedule with bounded catch-up. Rendering stalls never create
// an unbounded burst; elapsed slots beyond the per-tick budget are counted.
class LoadSchedule {
public:
    bool start(std::uint32_t rate, std::uint32_t duration_ms, std::uint64_t now_ms) {
        if (!rate || rate > 100 || !duration_ms || duration_ms > 600000) return false;
        rate_ = rate; duration_ = duration_ms; started_ = now_ms; stopped_elapsed_ = delivered_ = dropped_ = slots_ = 0; active_ = true;
        return true;
    }
    std::uint32_t tick(std::uint64_t now_ms, std::uint32_t budget = 8) {
        if (!active_) return 0;
        const auto elapsed = std::min<std::uint64_t>(now_ms >= started_ ? now_ms - started_ : 0, duration_);
        const auto due = elapsed * rate_ / 1000;
        const auto pending = due > slots_ ? due - slots_ : 0;
        const auto batch = static_cast<std::uint32_t>(std::min<std::uint64_t>(pending, budget));
        slots_ += pending; delivered_ += batch; dropped_ += pending - batch;
        if (elapsed == duration_) { stopped_elapsed_ = elapsed; active_ = false; }
        return batch;
    }
    void stop(std::uint64_t now_ms) { if (active_) { stopped_elapsed_ = elapsed(now_ms); active_ = false; } }
    bool active() const { return active_; }
    std::uint64_t elapsed(std::uint64_t now_ms) const {
        if (!active_) return stopped_elapsed_;
        return std::min<std::uint64_t>(now_ms >= started_ ? now_ms - started_ : 0, duration_);
    }
    std::uint64_t generated() const { return delivered_; }
    std::uint64_t dropped() const { return dropped_; }
    std::uint32_t rate() const { return rate_; }
    std::uint32_t duration() const { return duration_; }
private:
    std::uint32_t rate_ = 0, duration_ = 0;
    std::uint64_t started_ = 0, stopped_elapsed_ = 0, delivered_ = 0, dropped_ = 0, slots_ = 0;
    bool active_ = false;
};
} // namespace forearm
