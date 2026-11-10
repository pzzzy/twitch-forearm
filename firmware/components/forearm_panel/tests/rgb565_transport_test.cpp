#include "rgb565_transport.h"
#include <array>
#include <cassert>
#include <cstdio>

int main() {
    alignas(4) std::array<uint16_t, 516> source{}, destination{};
    uint32_t random = 0x12345678;
    for (auto& pixel : source) {
        random = random * 1664525U + 1013904223U;
        pixel = static_cast<uint16_t>(random >> 7);
    }
    const auto original = source;
    for (const bool staged : {false, true})
      for (unsigned source_offset = 0; source_offset < 2; ++source_offset)
        for (unsigned destination_offset = 0; destination_offset < 2; ++destination_offset)
            for (unsigned count = 0; count <= 513; ++count) {
                destination.fill(0x5aa5);
                if (staged) rgb565_copy_then_swap(source.data() + source_offset, destination.data() + destination_offset, count);
                else rgb565_swap_copy(source.data() + source_offset, destination.data() + destination_offset, count);
                for (unsigned i = 0; i < count; ++i) {
                    const uint16_t value = source[source_offset + i];
                    assert(destination[destination_offset + i] == static_cast<uint16_t>((value << 8) | (value >> 8)));
                }
                assert(destination[destination_offset + count] == 0x5aa5);
                assert(source == original);
            }
    auto in_place = source;
    rgb565_swap_copy(in_place.data(), in_place.data(), in_place.size());
    rgb565_swap_copy(in_place.data(), in_place.data(), in_place.size());
    assert(in_place == source);
    rgb565_copy_then_swap(in_place.data(), in_place.data(), in_place.size());
    rgb565_copy_then_swap(in_place.data(), in_place.data(), in_place.size());
    assert(in_place == source);
    puts("RGB565 transport copy passed: direct and staged modes; aligned/unaligned pointers, zero/odd/even lengths, canaries, immutable source and in-place round trip.");
}
