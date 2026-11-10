#include "axs_touch_state.h"
#include <cassert>
#include <cstdio>

static AxsTouchReport decode(const uint8_t (&data)[8]) {
    return axs_touch_decode(data, 320, 480);
}

int main() {
    // The UP and repeating 0x4b packet are from the physical Back-button trace.
    const uint8_t down[8] = {0, 1, 0, 159, 1, 205, 48, 49};
    const uint8_t contact[8] = {0, 1, 128, 159, 1, 205, 48, 49};
    const uint8_t up[8] = {0, 1, 64, 159, 1, 205, 48, 49};
    const uint8_t idle[8] = {75, 75, 75, 75, 75, 75, 75, 75};
    const uint8_t idle_ff[8] = {255, 255, 255, 255, 255, 255, 255, 255};
    const uint8_t zero[8] = {};
    const uint8_t invalid_contact[8] = {0, 1, 143, 255, 15, 255, 0, 0};
    const uint8_t invalid_up[8] = {0, 1, 79, 255, 15, 255, 0, 0};
    const uint8_t reserved[8] = {0, 1, 192, 159, 1, 205, 0, 0};
    AxsTouchState state;
    unsigned presses = 0, releases = 0;
    for (unsigned tap = 0; tap < 2; ++tap) {
        assert(state.apply(decode(down)) == AxsTouchAction::Press);
        ++presses;
        assert(state.pressed());
        for (unsigned i = 0; i < 100; ++i) {
            assert(state.apply(decode(idle)) == AxsTouchAction::None);
            assert(state.apply(decode(idle_ff)) == AxsTouchAction::None);
            assert(state.apply(decode(contact)) == AxsTouchAction::None);
            assert(state.pressed());
        }
        assert(state.apply(decode(up)) == AxsTouchAction::Release);
        ++releases;
        assert(!state.pressed() && state.x() == 159 && state.y() == 461);
        assert(state.apply(decode(up)) == AxsTouchAction::None);
        assert(state.apply(decode(idle)) == AxsTouchAction::None);
        assert(!state.pressed());
    }
    assert(presses == 2 && releases == 2);

    assert(state.apply(decode(down)) == AxsTouchAction::Press);
    assert(state.apply(decode(invalid_contact)) == AxsTouchAction::None);
    assert(state.apply(decode(reserved)) == AxsTouchAction::None);
    assert(state.pressed() && state.x() == 159 && state.y() == 461);
    assert(state.apply(decode(invalid_up)) == AxsTouchAction::Release);
    assert(!state.pressed() && state.x() == 159 && state.y() == 461);
    assert(state.apply(decode(down)) == AxsTouchAction::Press);
    assert(state.apply(decode(zero)) == AxsTouchAction::Release);

    assert(state.apply(decode(down)) == AxsTouchAction::Press);
    const AxsTouchReport error{AxsTouchKind::TransportError};
    assert(state.apply(error) == AxsTouchAction::None);
    assert(state.apply(error) == AxsTouchAction::None);
    assert(state.pressed());
    assert(state.apply(error) == AxsTouchAction::Cancel); // Never a click/release.
    assert(!state.pressed());
    assert(state.apply(decode(contact)) == AxsTouchAction::None);
    assert(!state.pressed()); // Recovery cannot synthesize a new mid-contact tap.
    assert(state.apply(decode(up)) == AxsTouchAction::None);
    assert(state.apply(decode(down)) == AxsTouchAction::Press);
    assert(state.apply(decode(up)) == AxsTouchAction::Release);
    puts("AXS physical event regression passed: Down/0x4b/Contact/0x4b/Up is one contact; explicit releases, invalid packets and transport cancellation stay distinct.");
}
