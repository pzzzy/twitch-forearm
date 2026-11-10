#pragma once

#include <algorithm>
#include <array>
#include <cstdint>

// Caller supplies synchronization. This tracker never observes raw sampling or
// injection itself: only a physical Down actually delivered to LVGL enters it.
class TouchFrameLatency {
 public:
    static constexpr uint32_t bucket_us = 10000;
    struct Snapshot {
        uint32_t samples = 0, recent_samples = 0, pending = 0;
        uint32_t last_us = 0, maximum_us = 0, p50_us = 0, p95_us = 0;
        uint32_t dropped = 0, ignored_injected = 0;
        uint32_t last_completed_sequence = 0, last_delivered_sequence = 0;
        uint64_t total_us = 0, last_delivered_sample_us = 0;
        std::array<uint32_t, 16> histogram{};
    };

    void delivered(uint32_t sequence, uint64_t sampled_us, bool injected = false) {
        if (injected) { ++statistics_.ignored_injected; return; }
        if (!sequence || !sampled_us ||
            (statistics_.last_delivered_sequence && !after(sequence, statistics_.last_delivered_sequence))) return;
        statistics_.last_delivered_sequence = sequence;
        statistics_.last_delivered_sample_us = sampled_us;
        if (pending_count_ == pending_.size()) {
            std::move(pending_.begin() + 1, pending_.end(), pending_.begin());
            --pending_count_;
            ++statistics_.dropped;
        }
        pending_[pending_count_++] = {sequence, sampled_us};
    }

    // Capture this at the final composition flush. A frame queued before a Down
    // reaches LVGL cannot inherit that Down when it later finishes transmitting.
    uint32_t composition_cutoff() const { return statistics_.last_delivered_sequence; }

    void completed(uint32_t cutoff, uint64_t completed_us) {
        if (!cutoff) return;
        std::size_t consumed = 0;
        while (consumed < pending_count_ && !after(pending_[consumed].sequence, cutoff)) {
            const Trace trace = pending_[consumed++];
            if (completed_us < trace.sampled_us) { ++statistics_.dropped; continue; }
            const auto elapsed = static_cast<uint32_t>(std::min<uint64_t>(completed_us - trace.sampled_us, UINT32_MAX));
            statistics_.last_us = elapsed;
            statistics_.maximum_us = std::max(statistics_.maximum_us, elapsed);
            statistics_.total_us += elapsed;
            ++statistics_.samples;
            statistics_.last_completed_sequence = trace.sequence;
            ++statistics_.histogram[std::min<std::size_t>(elapsed / bucket_us, statistics_.histogram.size() - 1)];
            recent_[recent_position_] = elapsed;
            recent_position_ = (recent_position_ + 1) % recent_.size();
            recent_count_ = std::min(recent_count_ + 1, recent_.size());
        }
        if (consumed) {
            std::move(pending_.begin() + consumed, pending_.begin() + pending_count_, pending_.begin());
            pending_count_ -= consumed;
        }
    }

    void cancel(uint32_t sequence) {
        for (std::size_t i = 0; i < pending_count_; ++i) {
            if (pending_[i].sequence != sequence) continue;
            std::move(pending_.begin() + i + 1, pending_.begin() + pending_count_, pending_.begin() + i);
            --pending_count_;
            ++statistics_.dropped;
            return;
        }
    }

    // Injection must not provide the first visible frame for an unobserved real
    // contact. This affects measurements only; input behavior is unchanged.
    void discard_pending() { statistics_.dropped += pending_count_; pending_count_ = 0; }

    void reset_measurements() {
        const auto delivered_sequence = statistics_.last_delivered_sequence;
        statistics_ = {};
        statistics_.last_delivered_sequence = delivered_sequence;
        pending_count_ = recent_count_ = recent_position_ = 0;
        recent_.fill(0);
    }

    Snapshot snapshot() const {
        Snapshot result = statistics_;
        result.pending = pending_count_;
        result.recent_samples = recent_count_;
        if (recent_count_) {
            auto ordered = recent_;
            std::sort(ordered.begin(), ordered.begin() + recent_count_);
            result.p50_us = ordered[(recent_count_ - 1) / 2];
            result.p95_us = ordered[(recent_count_ * 95 + 99) / 100 - 1];
        }
        return result;
    }

 private:
    struct Trace { uint32_t sequence; uint64_t sampled_us; };
    static bool after(uint32_t a, uint32_t b) { return static_cast<int32_t>(a - b) > 0; }
    std::array<Trace, 64> pending_{};
    std::array<uint32_t, 128> recent_{};
    std::size_t pending_count_ = 0, recent_count_ = 0, recent_position_ = 0;
    Snapshot statistics_{};
};
