#pragma once

#include <cstdint>

// Monotonic microsecond deadlines for one physical poller. Missed intervals are
// discarded, never replayed as a burst of catch-up transactions.
class TouchSampleCadence {
 public:
    static constexpr std::uint32_t period_us = 8000;

    std::uint64_t wait_us(std::uint64_t now_us) const {
        return now_us < next_start_us_ ? next_start_us_ - now_us : 0;
    }

    void completed(std::uint64_t actual_start_us, std::uint64_t completed_us) {
        const auto normal_next = actual_start_us + period_us;
        next_start_us_ = completed_us >= normal_next ? completed_us + period_us : normal_next;
    }

 private:
    std::uint64_t next_start_us_ = 0;
};
