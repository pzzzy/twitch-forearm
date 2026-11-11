#include "touch_sample_cadence.h"

#include <cassert>
#include <cstdio>

int main() {
    TouchSampleCadence cadence;
    assert(cadence.wait_us(0) == 0);
    cadence.completed(0, 600);
    assert(cadence.wait_us(600) == 7400);
    assert(cadence.wait_us(7999) == 1); // Early tick wake must not start I2C.
    assert(cadence.wait_us(8000) == 0);
    cadence.completed(8000, 8600);
    assert(cadence.wait_us(8600) == 7400);
    assert(cadence.wait_us(16000) == 0); // Healthy starts retain an 8ms period.

    // A 145ms I2C/task stall discards old slots and leaves a full period before
    // another transaction. vTaskDelayUntil's old schedule would return at once.
    cadence.completed(16000, 161000);
    assert(cadence.wait_us(161000) == 8000);
    assert(cadence.wait_us(168999) == 1);
    assert(cadence.wait_us(169000) == 0);
    cadence.completed(169000, 169600);
    assert(cadence.wait_us(169600) == 7400);

    // A delayed RTOS wake causes one late poll, then anchors the next deadline
    // to its actual start; there is no immediate train of overdue polls.
    assert(cadence.wait_us(314000) == 0);
    cadence.completed(314000, 314853);
    assert(cadence.wait_us(314853) == 7147);
    assert(cadence.wait_us(322000) == 0);
    cadence.completed(322000, 330000); // Exactly one period of work also skips.
    assert(cadence.wait_us(330000) == 8000);

    // Deadline arithmetic remains valid beyond a 32-bit microsecond wrap.
    const auto late = (1ULL << 32) + 5000;
    cadence.completed(late, late + 600);
    assert(cadence.wait_us(late + 7999) == 1);
    assert(cadence.wait_us(late + 8000) == 0);

    std::puts("Touch cadence passed: normal8ms starts, early-wake guard,145ms stall skip, delayed-wake rebasing, no catch-up burst,64-bit time.");
}
