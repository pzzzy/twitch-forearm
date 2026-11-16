// Actual scaled-image pixels, clipping and source reuse with pinned real LVGL.
// Anonymous synthetic data only; no networking, device access or moderation.
#include "app_ui.h"
#include "emote_cache.h"
#include "lvgl.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>

struct Asset {
    const char *id;
    lv_img_dsc_t image{};
    std::array<std::uint8_t, 128 * 128 * 3> pixels{};
};
static std::array<Asset, 3> assets{{{"25"}, {"emoji:1f600"}, {"7tv:wide"}}};
static std::array<lv_color_t, 320 * 480> framebuffer{};
static std::int64_t fake_us;
std::int64_t esp_timer_get_time() { return fake_us; }
bool emote_cache_init() { return true; }
bool emote_cache_request(const char *) { return true; }
const lv_img_dsc_t *emote_cache_acquire(const char *id) {
    for (const auto &asset : assets) if (!std::strcmp(id, asset.id)) return &asset.image;
    return nullptr;
}
bool emote_cache_dimensions(const char *id, unsigned &width, unsigned &height) {
    const auto *image = emote_cache_acquire(id);
    width = image ? image->header.w : 0; height = image ? image->header.h : 0;
    return image;
}
bool emote_cache_matches(const char *id, const lv_img_dsc_t *image) { return image == emote_cache_acquire(id); }
void emote_cache_release(const lv_img_dsc_t *) {}
EmoteCacheStats emote_cache_stats() { EmoteCacheStats value{}; value.ready = assets.size(); return value; }
unsigned emote_cache_collect(const char *, const char *, forearm::MediaOccurrence *, unsigned count, unsigned) { return count; }
static void pump() {
    for (unsigned i = 0; i < 64; i += 8) { fake_us += 8000; lv_tick_inc(8); app_ui_tick(); lv_timer_handler(); }
}
static lv_obj_t *visible_image(lv_obj_t *obj) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    if (lv_obj_check_type(obj, &lv_img_class)) return obj;
    for (unsigned i = 0; i < lv_obj_get_child_cnt(obj); ++i)
        if (auto *found = visible_image(lv_obj_get_child(obj, i))) return found;
    return nullptr;
}
static auto capture() { lv_obj_invalidate(lv_scr_act()); lv_refr_now(nullptr); return framebuffer; }
int main() {
    lv_init();
    static lv_disp_draw_buf_t buffer; lv_disp_draw_buf_init(&buffer, framebuffer.data(), nullptr, framebuffer.size());
    lv_disp_drv_t display; lv_disp_drv_init(&display); display.hor_res = 320; display.ver_res = 480;
    display.direct_mode = 1; display.draw_buf = &buffer;
    display.flush_cb = [](lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(driver); };
    lv_disp_drv_register(&display);
    const auto green = lv_color_make(0, 255, 0).full;
    for (unsigned i = 0; i < assets.size(); ++i) {
        auto &asset = assets[i]; const unsigned width = i == 0 ? 48 : i == 1 ? 72 : 96;
        const unsigned height = i == 0 ? 48 : i == 1 ? 72 : 24;
        asset.image.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
        asset.image.header.w = width; asset.image.header.h = height;
        asset.image.data = asset.pixels.data(); asset.image.data_size = width * height * 3;
        for (unsigned pixel = 0; pixel < width * height; ++pixel) {
            asset.pixels[pixel * 3] = green & 255; asset.pixels[pixel * 3 + 1] = green >> 8;
            asset.pixels[pixel * 3 + 2] = 255;
        }
    }
    app_ui_init(); app_ui_set_authenticated("Synthetic owner", "synthetic_room");
    for (const auto percent : {67U, 75U, 80U, 90U, 100U, 115U, 125U, 133U, 150U}) {
        app_ui_set_ui_scale(percent); app_ui_set_fixture_mode(false); app_ui_set_fixture_mode(true);
        assert(app_ui_append("100", "image-origin", "300", "viewer", "emote"));
        lv_obj_t *reused = nullptr;
        for (const auto &asset : assets) {
            const AppUiEmote occurrence{asset.id, "emote", 0, 5, true};
            app_ui_set_message_emotes("100", "image-origin", &occurrence, 1); app_ui_go_live(); pump();
            assert(app_ui_stats().visible_emote_images == 1);
            auto *actual = visible_image(lv_scr_act()); assert(actual);
            if (reused) assert(actual == reused); // Different source, same LVGL object.
            reused = actual;
            lv_area_t location{}, row{}; lv_obj_get_coords(actual, &location);
            lv_obj_get_coords(lv_obj_get_parent(actual), &row);
            const auto zoom = lv_img_get_zoom(actual);
            const int scaled_width = (asset.image.header.w * zoom + 255) / 256;
            const int scaled_height = (asset.image.header.h * zoom + 255) / 256;
            const auto rendered = capture();
            lv_obj_add_flag(actual, LV_OBJ_FLAG_HIDDEN);
            // An independent screen child has no row clip. Its explicit origin
            // represents the full image the inline row must preserve.
            auto *reference = lv_img_create(lv_scr_act());
            lv_img_set_src(reference, &asset.image); lv_img_set_pivot(reference, 0, 0);
            lv_img_set_zoom(reference, zoom); lv_obj_set_pos(reference, location.x1, location.y1);
            const auto complete = capture();
            assert(!std::memcmp(rendered.data(), complete.data(), sizeof(framebuffer)));
            int left = 320, right = -1, top = 480, bottom = -1;
            for (int y = 0; y < 480; ++y) for (int x = 0; x < 320; ++x)
                if (rendered[y * 320 + x].full == green) {
                    left = std::min(left, x); right = std::max(right, x);
                    top = std::min(top, y); bottom = std::max(bottom, y);
                }
            assert(right - left + 1 >= scaled_width - 1 && bottom - top + 1 >= scaled_height - 1);
            assert(left >= location.x1 - 1 && left <= location.x1 + 1 &&
                   top >= location.y1 - 1 && top <= location.y1 + 1);
            assert(left >= row.x1 && right <= row.x2 && top >= row.y1 && bottom < row.y2);
            lv_obj_del(reference); lv_obj_clear_flag(actual, LV_OBJ_FLAG_HIDDEN);
        }
    }
    std::puts("Inline origin pixels passed:27 row-clipped vs complete native image comparisons across all9 sizes,48/72px squares and96px wide sources; same-object source rebinding preserves origin and complete ink.");
}

const lv_img_dsc_t* emote_cache_frame(const lv_img_dsc_t* image, std::uint32_t) { return image; }
