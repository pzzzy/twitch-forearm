#pragma once

#include <cstdint>

// AXS15231B event stream observed on the connected JC3248W535: nonzero
// headers (including repeating 0x4b and 0xff) mean no new report, not release.
enum class AxsTouchKind { NoData, Down, Contact, Up, Invalid, TransportError };
struct AxsTouchReport {
    AxsTouchKind kind = AxsTouchKind::NoData;
    uint16_t x = 0, y = 0;
    bool coordinates_valid = false;
};

inline AxsTouchReport axs_touch_decode(const uint8_t data[8], uint16_t width, uint16_t height) {
    if (data[0] != 0) return {};
    if (data[1] == 0) return {AxsTouchKind::Up};
    if (data[1] != 1) return {AxsTouchKind::Invalid};
    const uint8_t event = data[2] >> 6;
    const uint16_t x = static_cast<uint16_t>(((data[2] & 0x0f) << 8) | data[3]);
    const uint16_t y = static_cast<uint16_t>(((data[4] & 0x0f) << 8) | data[5]);
    const bool valid = x < width && y < height;
    if (event == 1) return {AxsTouchKind::Up, x, y, valid};
    if (!valid || event == 3) return {AxsTouchKind::Invalid, x, y, valid};
    return {event == 0 ? AxsTouchKind::Down : AxsTouchKind::Contact, x, y, true};
}

enum class AxsTouchAction { None, Press, Move, Release, Cancel };
class AxsTouchState {
 public:
    AxsTouchAction apply(const AxsTouchReport& report) {
        if (report.kind == AxsTouchKind::TransportError) {
            if (errors_ != UINT32_MAX) ++errors_;
            if (errors_ >= 3 && pressed_) {
                pressed_ = false;
                blocked_ = true;
                return AxsTouchAction::Cancel;
            }
            return AxsTouchAction::None;
        }
        errors_ = 0;
        if (report.kind == AxsTouchKind::NoData || report.kind == AxsTouchKind::Invalid)
            return AxsTouchAction::None;
        if (report.kind == AxsTouchKind::Up) {
            const bool was_pressed = pressed_;
            if (report.coordinates_valid) { x_ = report.x; y_ = report.y; }
            pressed_ = false;
            blocked_ = false;
            return was_pressed ? AxsTouchAction::Release : AxsTouchAction::None;
        }
        if (blocked_) return AxsTouchAction::None;
        const bool moved = x_ != report.x || y_ != report.y;
        const bool was_pressed = pressed_;
        x_ = report.x; y_ = report.y; pressed_ = true;
        return !was_pressed ? AxsTouchAction::Press :
               moved ? AxsTouchAction::Move : AxsTouchAction::None;
    }
    bool pressed() const { return pressed_; }
    uint16_t x() const { return x_; }
    uint16_t y() const { return y_; }
    uint32_t consecutive_errors() const { return errors_; }
 private:
    bool pressed_ = false;
    bool blocked_ = false;
    uint16_t x_ = 0, y_ = 0;
    uint32_t errors_ = 0;
};
