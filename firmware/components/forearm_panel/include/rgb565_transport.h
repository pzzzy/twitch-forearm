#pragma once

#include <cstddef>
#include <cstdint>
#include <cstring>

// Swap bytes within each RGB565 pixel while preserving pixel order. Four-byte
// aligned buffers use pairs of pixels; unaligned buffers and odd tails remain
// valid. Explicit compiler builtins preserve C++ aliasing rules for LVGL's
// lv_color16_t union and inline the fixed-width transfers even under IDF's
// -fno-builtin-memcpy (ordinary memcpy would call twice for every pixel pair).
inline void rgb565_swap_copy(const uint16_t* source, uint16_t* destination, std::size_t pixels) {
    std::size_t offset = 0;
    if (((reinterpret_cast<std::uintptr_t>(source) | reinterpret_cast<std::uintptr_t>(destination)) & 3) == 0) {
        const auto* input = static_cast<const uint8_t*>(__builtin_assume_aligned(source, 4));
        auto* output = static_cast<uint8_t*>(__builtin_assume_aligned(destination, 4));
        for (; offset + 1 < pixels; offset += 2) {
            uint32_t pair;
            __builtin_memcpy(&pair, input + offset * sizeof(uint16_t), sizeof(pair));
            pair = ((pair & 0x00ff00ffU) << 8) | ((pair & 0xff00ff00U) >> 8);
            __builtin_memcpy(output + offset * sizeof(uint16_t), &pair, sizeof(pair));
        }
    }
    for (; offset < pixels; ++offset) {
        const uint16_t pixel = source[offset];
        destination[offset] = static_cast<uint16_t>((pixel << 8) | (pixel >> 8));
    }
}

// A/B alternative for external-memory sources: one optimized bulk read first,
// then perform the same conversion entirely in internal RAM. Identical buffers
// are supported; otherwise source and destination must not overlap.
inline void rgb565_copy_then_swap(const uint16_t* source, uint16_t* destination, std::size_t pixels) {
    if (!pixels) return;
    if (source != destination) std::memcpy(destination, source, pixels * sizeof(uint16_t));
    rgb565_swap_copy(destination, destination, pixels);
}
