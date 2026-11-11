#pragma once

#include <cstdint>

// UI-owner calls only. Owns one temporary object on LVGL's top layer; never
// changes chat objects, network state, timers, or the active screen.
// Returns false when a display/object cannot be obtained. Chat continues.
bool startup_animation_start(std::uint64_t now_ms);
void startup_animation_tick(std::uint64_t now_ms);
bool startup_animation_active();
void startup_animation_finish();
