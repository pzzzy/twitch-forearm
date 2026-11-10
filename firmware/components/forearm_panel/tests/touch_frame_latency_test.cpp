#include "touch_frame_latency.h"

#include <cassert>
#include <cstdio>
#include <limits>

int main() {
    TouchFrameLatency tracker;
    // An older in-flight frame cannot be credited for a Down not yet delivered
    // to the UI. A later Move or duplicate delivery cannot reset its origin.
    const auto before_press = tracker.composition_cutoff();
    tracker.delivered(1, 1000);
    tracker.delivered(1, 2000);
    tracker.completed(before_press, 11000);
    assert(tracker.snapshot().samples == 0);
    assert(tracker.snapshot().pending == 1);
    const auto first_frame = tracker.composition_cutoff();
    tracker.delivered(2, 20000);
    tracker.completed(first_frame, 31000);
    auto stats = tracker.snapshot();
    assert(stats.samples == 1 && stats.last_us == 30000);
    assert(stats.last_completed_sequence == 1 && stats.pending == 1);
    tracker.completed(first_frame, 40000);
    assert(tracker.snapshot().samples == 1); // one observation per press
    tracker.completed(tracker.composition_cutoff(), 50000);
    stats = tracker.snapshot();
    assert(stats.samples == 2 && stats.last_us == 30000 && stats.pending == 0);
    assert(stats.total_us == 60000 && stats.histogram[3] == 2);

    // Multiple UI-consumed presses coalesced in one frame retain separate
    // timestamps. Synthetic input never becomes a physical latency sample.
    tracker.delivered(3, 60000);
    tracker.delivered(4, 70000);
    tracker.delivered(5, 75000, true);
    tracker.completed(tracker.composition_cutoff(), 90000);
    stats = tracker.snapshot();
    assert(stats.samples == 4 && stats.last_us == 20000);
    assert(stats.last_completed_sequence == 4 && stats.ignored_injected == 1);
    assert(stats.p50_us == 30000 && stats.p95_us == 30000);
    assert(stats.histogram[2] == 1 && stats.histogram[3] == 3);

    tracker.delivered(5, 100000);
    tracker.cancel(5);
    tracker.completed(5, 110000);
    assert(tracker.snapshot().samples == 4 && tracker.snapshot().dropped == 1);
    tracker.delivered(6, 120000);
    tracker.discard_pending(); // entering diagnostic injection
    tracker.completed(6, 130000);
    assert(tracker.snapshot().samples == 4 && tracker.snapshot().dropped == 2);
    tracker.reset_measurements();
    assert(tracker.snapshot().samples == 0 && tracker.snapshot().pending == 0);
    assert(tracker.composition_cutoff() == 6);
    tracker.delivered(6, 140000); // stale edge after reset remains rejected
    assert(tracker.snapshot().pending == 0);
    tracker.delivered(7, 150000);
    tracker.completed(7, 140000); // invalid clock ordering is excluded
    assert(tracker.snapshot().samples == 0 && tracker.snapshot().dropped == 1);

    // A bounded pending queue reports loss rather than fabricating latency.
    TouchFrameLatency bounded;
    for (uint32_t i = 1; i <= 65; ++i) bounded.delivered(i, i * 1000);
    assert(bounded.snapshot().pending == 64 && bounded.snapshot().dropped == 1);
    bounded.completed(65, 100000);
    assert(bounded.snapshot().samples == 64 && bounded.snapshot().pending == 0);
    assert(bounded.snapshot().last_us == 35000);

    // Sequence wrap (zero is reserved) must retain ordering across frame jobs.
    TouchFrameLatency wrapping;
    wrapping.delivered(UINT32_MAX - 1, 1000);
    const auto old_cutoff = wrapping.composition_cutoff();
    wrapping.delivered(UINT32_MAX, 2000);
    wrapping.delivered(1, 3000);
    wrapping.completed(old_cutoff, 10000);
    assert(wrapping.snapshot().samples == 1 && wrapping.snapshot().pending == 2);
    wrapping.completed(1, 11000);
    assert(wrapping.snapshot().samples == 3 && wrapping.snapshot().pending == 0);
    assert(wrapping.snapshot().last_completed_sequence == 1);

    // p95 uses the latest 128 completions, while maxima/counts cover the run.
    TouchFrameLatency window;
    for (uint32_t i = 1; i <= 140; ++i) {
        window.delivered(i, 1000000);
        window.completed(i, 1000000 + i * 1000);
    }
    stats = window.snapshot();
    assert(stats.samples == 140 && stats.recent_samples == 128);
    assert(stats.p50_us == 76000 && stats.p95_us == 134000);
    assert(stats.maximum_us == 140000 && stats.last_us == 140000);
    std::puts("Physical touch-to-frame latency: PASS (cutoffs, unique edges, injection, cancellation, wrap, histogram)");
}
