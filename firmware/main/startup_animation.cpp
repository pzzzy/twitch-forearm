#include "startup_animation.h"

#include <algorithm>

#include "lvgl.h"

namespace {
constexpr std::uint32_t kInk = 0x08090C;
constexpr std::uint32_t kViolet = 0xBE98F1;
constexpr std::uint32_t kTeal = 0x73E6D2;
constexpr std::uint32_t kDuration = 2200;
constexpr std::uint32_t kWipeStart = 2040;
constexpr std::uint32_t kFrameInterval = 16;

struct State {
    lv_obj_t *overlay = nullptr;
    std::uint64_t started = 0;
    std::uint32_t elapsed = 0;
    int width = 320;
    int height = 480;
    bool dismiss = false;
} animation;

lv_color_t color(std::uint32_t rgb) { return lv_color_hex(rgb); }
int progress(std::uint32_t t, std::uint32_t begin, std::uint32_t duration) {
    if (t <= begin) return 0;
    if (t >= begin + duration) return 1024;
    return static_cast<int>((t - begin) * 1024 / duration);
}
int ease_out(int p) {
    const auto inverse = static_cast<std::uint32_t>(1024 - p);
    return 1024 - static_cast<int>((inverse * inverse * inverse) >> 20);
}
int mix(int a, int b, int p) { return a + (b - a) * p / 1024; }
lv_opa_t opacity(int p) {
    return static_cast<lv_opa_t>(std::clamp(p, 0, 1024) * 255 / 1024);
}
int sine(int degrees) { return lv_trigo_sin(static_cast<std::int16_t>((degrees % 360 + 360) % 360)); }

void rectangle(lv_draw_ctx_t *ctx, int x, int y, int width, int height,
               std::uint32_t rgb, lv_opa_t opa, int radius = 0) {
    if (!opa || width <= 0 || height <= 0) return;
    lv_draw_rect_dsc_t d;
    lv_draw_rect_dsc_init(&d);
    d.bg_color = color(rgb);
    d.bg_opa = opa;
    d.radius = radius;
    const lv_area_t area{static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y),
                         static_cast<lv_coord_t>(x + width - 1), static_cast<lv_coord_t>(y + height - 1)};
    lv_draw_rect(ctx, &d, &area);
}
void arc(lv_draw_ctx_t *ctx, int cx, int cy, int radius, int start, int end,
         std::uint32_t rgb, lv_opa_t opa, int width) {
    if (!opa || radius <= 0) return;
    lv_draw_arc_dsc_t d;
    lv_draw_arc_dsc_init(&d);
    d.color = color(rgb);
    d.opa = opa;
    d.width = width;
    d.rounded = true;
    const lv_point_t center{static_cast<lv_coord_t>(cx), static_cast<lv_coord_t>(cy)};
    lv_draw_arc(ctx, &d, &center, radius, static_cast<std::uint16_t>((start % 360 + 360) % 360),
                static_cast<std::uint16_t>((end % 360 + 360) % 360));
}
void line(lv_draw_ctx_t *ctx, int x1, int y1, int x2, int y2,
          std::uint32_t rgb, lv_opa_t opa, int width) {
    if (!opa) return;
    lv_draw_line_dsc_t d;
    lv_draw_line_dsc_init(&d);
    d.color = color(rgb); d.opa = opa; d.width = width;
    d.round_start = true; d.round_end = true;
    const lv_point_t a{static_cast<lv_coord_t>(x1), static_cast<lv_coord_t>(y1)};
    const lv_point_t b{static_cast<lv_coord_t>(x2), static_cast<lv_coord_t>(y2)};
    lv_draw_line(ctx, &d, &a, &b);
}
void draw(lv_event_t *event) {
    auto *ctx = lv_event_get_draw_ctx(event);
    if (!ctx) return;
    const auto t = animation.elapsed;
    const int cx = animation.width / 2;
    const int cy = animation.height / 2;
    const int overall = 1024 - progress(t, 1880, 160);
    const int reveal = ease_out(progress(t, 40, 400));
    const int orbit = reveal * (1024 - progress(t, 1460, 390)) / 1024 * overall / 1024;
    const int radius = mix(38, 76, reveal);
    const int angle = t < 1100 ? static_cast<int>(t * 270 / 1000)
                               : 297 + static_cast<int>((t - 1100) * 90 / 1000);
    arc(ctx, cx, cy, radius, 0, 359, 0x292033, opacity(orbit / 2), 1);
    arc(ctx, cx, cy, radius, angle, angle + 104, kViolet, opacity(orbit), 2);
    arc(ctx, cx, cy, radius - 10, 190 - angle, 242 - angle, kTeal, opacity(orbit * 3 / 4), 2);
    for (int i = 0; i < 3; ++i) {
        const int a = angle + i * 120;
        const int x = cx + sine(a + 90) * radius / LV_TRIGO_SIN_MAX;
        const int y = cy + sine(a) * radius / LV_TRIGO_SIN_MAX;
        rectangle(ctx, x - 2, y - 2, 5, 5, i == 1 ? kTeal : kViolet, opacity(orbit), LV_RADIUS_CIRCLE);
    }

    const int morph = ease_out(progress(t, 820, 500));
    const int bubble = ease_out(progress(t, 980, 450)) * overall / 1024;
    if (bubble) {
        lv_draw_rect_dsc_t d;
        lv_draw_rect_dsc_init(&d);
        d.bg_opa = LV_OPA_TRANSP;
        d.border_color = color(kViolet); d.border_width = 3;
        d.border_opa = opacity(bubble); d.radius = 16;
        const lv_area_t area{static_cast<lv_coord_t>(cx - 43), static_cast<lv_coord_t>(cy - 31),
                             static_cast<lv_coord_t>(cx + 42), static_cast<lv_coord_t>(cy + 32)};
        lv_draw_rect(ctx, &d, &area);
        rectangle(ctx, cx - 28, cy + 29, 19, 6, kInk, LV_OPA_COVER);
        line(ctx, cx - 28, cy + 30, cx - 28, cy + 46, kViolet, opacity(bubble), 3);
        line(ctx, cx - 28, cy + 46, cx - 9, cy + 32, kViolet, opacity(bubble), 3);
    }
    for (int i = 0; i < 5; ++i) {
        const int entrance = ease_out(progress(t, 110 + i * 55, 280));
        const int wave = (sine(static_cast<int>(t / 3) + i * 63) + LV_TRIGO_SIN_MAX) * 16 / (2 * LV_TRIGO_SIN_MAX);
        const int h = mix(12 + wave, 6, morph);
        const int x = mix(cx - 24 + i * 11, cx - 18 + (i - 1) * 16, morph);
        int alpha = entrance * overall / 1024;
        if (i == 0 || i == 4) alpha = alpha * (1024 - morph) / 1024;
        const int w = mix(4, 6, morph);
        rectangle(ctx, x, cy - h / 2, w, h, i == 2 ? kTeal : kViolet, opacity(alpha), 3);
    }
    const int track_width = std::min(112, animation.width - 48);
    const int left = (animation.width - track_width) / 2;
    const int top = animation.height * 94 / 100;
    rectangle(ctx, left, top, track_width, 2, 0x282132, opacity(overall));
    rectangle(ctx, left, top, track_width * std::min<std::uint32_t>(t, 1850) / 1850, 2, kTeal, opacity(overall));
}

void event(lv_event_t *e) {
    if (lv_event_get_code(e) == LV_EVENT_DRAW_MAIN) draw(e);
    else if (lv_event_get_code(e) == LV_EVENT_PRESSED) animation.dismiss = true;
    else if (lv_event_get_code(e) == LV_EVENT_DELETE && lv_event_get_target(e) == animation.overlay)
        animation.overlay = nullptr;
}
} // namespace

bool startup_animation_start(std::uint64_t now_ms) {
    startup_animation_finish();
    if (!lv_disp_get_default()) return false;
    auto *parent = lv_layer_top();
    if (!parent) return false;
    // The convenience lv_obj_create() initializes unconditionally in LVGL8.
    // Check the class allocation before initialization so a failed first object
    // allocation simply leaves the already-created chat screen visible.
    auto *overlay = lv_obj_class_create_obj(&lv_obj_class, parent);
    if (!overlay) return false;
    lv_obj_class_init_obj(overlay);
    animation.overlay = overlay;
    animation.started = now_ms; animation.elapsed = 0; animation.dismiss = false;
    animation.width = lv_disp_get_hor_res(nullptr); animation.height = lv_disp_get_ver_res(nullptr);
    lv_obj_remove_style_all(overlay);
    lv_obj_set_pos(overlay, 0, 0);
    lv_obj_set_size(overlay, animation.width, animation.height);
    lv_obj_set_style_bg_color(overlay, color(kInk), 0);
    lv_obj_set_style_bg_opa(overlay, LV_OPA_COVER, 0);
    lv_obj_clear_flag(overlay, LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE);
    lv_obj_add_flag(overlay, LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_PRESS_LOCK);
    lv_obj_add_event_cb(overlay, event, LV_EVENT_ALL, nullptr);
    lv_obj_move_foreground(overlay);
    // Input timers may run before the first refresh. Commit the full-screen
    // hit area now so the very first contact cannot reach an underlying row.
    lv_obj_update_layout(overlay);
    return true;
}
void startup_animation_tick(std::uint64_t now_ms) {
    if (!animation.overlay) return;
    const auto age = now_ms >= animation.started ? now_ms - animation.started : 0;
    if (animation.dismiss || age >= kDuration) { startup_animation_finish(); return; }
    if (age < animation.elapsed + kFrameInterval) return;
    animation.elapsed = static_cast<std::uint32_t>(age);
    if (age >= kWipeStart) {
        const int remaining = static_cast<int>(kDuration - age);
        lv_obj_set_height(animation.overlay, std::max(1, animation.height * remaining /
            static_cast<int>(kDuration - kWipeStart)));
    }
    lv_obj_invalidate(animation.overlay);
}
bool startup_animation_active() { return animation.overlay != nullptr; }
void startup_animation_finish() {
    if (!animation.overlay) return;
    auto *overlay = animation.overlay;
    animation.overlay = nullptr;
    animation.dismiss = false;
    // A startup contact must never become a click on the revealed chat/settings.
    lv_indev_reset(nullptr, nullptr);
    for (auto *indev = lv_indev_get_next(nullptr); indev; indev = lv_indev_get_next(indev))
        if (lv_indev_get_type(indev) == LV_INDEV_TYPE_POINTER && indev->proc.state == LV_INDEV_STATE_PR)
            lv_indev_wait_release(indev);
    lv_obj_del(overlay);
}
