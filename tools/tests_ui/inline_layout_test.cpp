// Real LVGL pixels and native layout, with anonymous fixtures and immutable
// fake assets. No network, device access, chat sends or moderation requests.
#include "app_ui.h"
#include "emote_cache.h"
#include "forearm_fonts.h"
#include "lvgl.h"

#include <algorithm>
#include <array>
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

static std::int64_t fake_us = 0;
static bool asset_ready = false;
static lv_indev_state_t pointer_state = LV_INDEV_STATE_REL;
static lv_point_t pointer_point{};
static std::array<lv_color_t, 320 * 480> framebuffer{};
static lv_img_dsc_t asset{};
static std::array<std::uint8_t, 25 * 28 * 3> asset_pixels{};
static unsigned fixture_id = 0;

std::int64_t esp_timer_get_time() { return fake_us; }
bool emote_cache_init() { return true; }
bool emote_cache_request(const char *) { return true; }
const lv_img_dsc_t *emote_cache_get(const char *id) {
    return asset_ready && !std::strcmp(id, "25") ? &asset : nullptr;
}
EmoteCacheStats emote_cache_stats() { EmoteCacheStats stats{}; stats.ready = asset_ready ? 1 : 0; return stats; }

static void pump(unsigned ms = 64) {
    for (unsigned elapsed = 0; elapsed < ms; elapsed += 8) {
        fake_us += 8000; lv_tick_inc(8); app_ui_tick(); lv_timer_handler();
    }
}
static void tap(int x, int y) {
    pointer_point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
    pointer_state = LV_INDEV_STATE_PR; pump(24);
    pointer_state = LV_INDEV_STATE_REL; pump(48);
}
static lv_obj_t *find_label(lv_obj_t *obj, const char *text) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    for (int i = static_cast<int>(lv_obj_get_child_cnt(obj)) - 1; i >= 0; --i)
        if (auto *found = find_label(lv_obj_get_child(obj, i), text)) return found;
    return lv_obj_check_type(obj, &lv_label_class) && !std::strcmp(lv_label_get_text(obj), text) ? obj : nullptr;
}
static lv_area_t bounds(lv_obj_t *obj) { lv_obj_update_layout(obj); lv_area_t area{}; lv_obj_get_coords(obj, &area); return area; }
static void images(lv_obj_t *parent, std::vector<lv_obj_t *> &out) {
    for (unsigned i = 0; i < lv_obj_get_child_cnt(parent); ++i) {
        auto *obj = lv_obj_get_child(parent, i);
        if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) continue;
        if (lv_obj_check_type(obj, &lv_img_class)) out.push_back(obj);
    }
}
static auto capture() {
    lv_obj_invalidate(lv_scr_act()); lv_refr_now(nullptr); return framebuffer;
}
static unsigned ink(const std::array<lv_color_t, 320 * 480> &pixels, lv_area_t area) {
    const auto background = lv_color_hex(0x08090C).full;
    unsigned count = 0;
    for (int y = std::max<int>(0, area.y1); y <= std::min<int>(479, area.y2); ++y)
        for (int x = std::max<int>(0, area.x1); x <= std::min<int>(319, area.x2); ++x)
            if (pixels[y * 320 + x].full != background) ++count;
    return count;
}
static std::string fresh(const char *author, const std::string &text, const char *login = "viewer_name_") {
    app_ui_set_fixture_mode(false); app_ui_set_fixture_mode(true);
    const auto id = "inline-" + std::to_string(++fixture_id);
    assert(app_ui_append("100", id.c_str(), "300", author, text.c_str(), 0, login));
    app_ui_go_live(); pump(500); return id;
}
static lv_obj_t *author_row(const char *author) {
    auto *name = find_label(lv_scr_act(), author); assert(name); return lv_obj_get_parent(name);
}
static void emotes(const std::string &id, const AppUiEmote *items, unsigned count) {
    app_ui_set_message_emotes("100", id.c_str(), items, count); pump(128);
}

int main() {
    lv_init();
    static lv_disp_draw_buf_t buffer; lv_disp_draw_buf_init(&buffer, framebuffer.data(), nullptr, framebuffer.size());
    lv_disp_drv_t display; lv_disp_drv_init(&display);
    display.hor_res = 320; display.ver_res = 480; display.direct_mode = 1; display.draw_buf = &buffer;
    display.flush_cb = [](lv_disp_drv_t *driver, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(driver); };
    lv_disp_drv_register(&display);
    lv_indev_drv_t input; lv_indev_drv_init(&input); input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = [](lv_indev_drv_t *, lv_indev_data_t *data) { data->point = pointer_point; data->state = pointer_state; };
    lv_indev_drv_register(&input);
    const auto green = lv_color_make(0, 255, 0).full;
    for (unsigned i = 0; i < 25 * 28; ++i) {
        asset_pixels[i * 3] = green & 255; asset_pixels[i * 3 + 1] = green >> 8; asset_pixels[i * 3 + 2] = 255;
    }
    asset.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA; asset.header.w = 25; asset.header.h = 28;
    asset.data = asset_pixels.data(); asset.data_size = asset_pixels.size();
    app_ui_set_font_fallback(forearm_fallback_font()); app_ui_init();
    app_ui_set_authenticated("Synthetic owner", "synthetic_room");
    assert(app_ui_stats().read_only);

    // Missing assets keep literal text. Arrival replaces that occurrence and
    // invalidates an already built sprite even when the row height is unchanged.
    auto id = fresh("viewer_name_", "Kappa");
    const AppUiEmote first{"25", "Kappa", 0, 5, true}; emotes(id, &first, 1);
    auto *row = author_row("viewer_name_"); auto body = bounds(lv_obj_get_child(row, 1));
    auto literal = capture();
    assert(!app_ui_stats().visible_emote_images);
    assert(ink(literal, {55, body.y1, 100, body.y2}) > 0);
    const auto builds = app_ui_stats().sprite_cache_builds;
    asset_ready = true; pump(128);
    auto loaded = capture();
    assert(app_ui_stats().visible_emote_images == 1 && app_ui_stats().sprite_cache_builds > builds);
    assert(ink(loaded, {55, body.y1, 100, body.y2}) == 0); // No duplicate Kappa text or strip.
    std::vector<lv_obj_t *> visible; images(row, visible); assert(visible.size() == 1);
    auto image = bounds(visible[0]); assert(image.x1 == body.x1 && image.y1 >= body.y1 && image.y1 <= body.y1 + 2);
    app_ui_set_sprite_cache(false); pump(64);
    const auto uncached = capture(); assert(!std::memcmp(loaded.data(), uncached.data(), sizeof(loaded)));
    app_ui_set_sprite_cache(true); pump(64);

    // Only the middle occurrence is an emote. Identical ordinary words on
    // either side retain their pixels, rather than being globally replaced.
    id = fresh("viewer_name_", "Kappa Kappa Kappa");
    const AppUiEmote middle{"25", "Kappa", 6, 5, true}; emotes(id, &middle, 1);
    row = author_row("viewer_name_"); body = bounds(lv_obj_get_child(row, 1)); visible.clear(); images(row, visible);
    assert(visible.size() == 1); image = bounds(visible[0]); assert(image.x1 > body.x1 + 50);
    const auto middle_pixels = capture();
    assert(ink(middle_pixels, {body.x1, body.y1, static_cast<lv_coord_t>(image.x1 - 3), body.y2}) > 0);
    assert(ink(middle_pixels, {static_cast<lv_coord_t>(image.x1 + 32), body.y1, 250, body.y2}) > 0);
    id = fresh("viewer_name_", "Kappa Kappa");
    const AppUiEmote repeated[] = {first, {"25", "Kappa", 6, 5, true}}; emotes(id, repeated, 2);
    visible.clear(); images(author_row("viewer_name_"), visible); assert(visible.size() == 2);
    assert(bounds(visible[1]).x1 > bounds(visible[0]).x1);

    // Byte offsets follow UTF-8 fragments, including a multibyte emote token.
    const std::string multibyte = u8"é💜 にこ にこ";
    id = fresh("viewer_name_", multibyte);
    const auto start = multibyte.find(u8"にこ");
    const AppUiEmote unicode{"25", u8"にこ", static_cast<std::uint16_t>(start), 6, true}; emotes(id, &unicode, 1);
    visible.clear(); images(author_row("viewer_name_"), visible); assert(visible.size() == 1 && bounds(visible[0]).x1 > 30);
    tap(30, 45); pump(500); assert(find_label(lv_scr_act(), multibyte.c_str())); // Raw message stays intact.
    app_ui_go_live(); pump(64);
    for (const AppUiEmote invalid : {AppUiEmote{"25", u8"にこ", 3, 6, true},
                                    AppUiEmote{"25", "wrong", static_cast<std::uint16_t>(start), 5, true},
                                    AppUiEmote{"25", u8"にこ", static_cast<std::uint16_t>(start), 6, false}}) {
        id = fresh("viewer_name_", multibyte); emotes(id, &invalid, 1);
        assert(!app_ui_stats().visible_emote_images);
    }
    // Literal reserved private-use characters are visible replacements, never
    // mistaken for zero-ink markers or images injected by untrusted chat text.
    id = fresh("viewer_name_", u8"\U000F0000\U000F0001");
    row = author_row("viewer_name_"); body = bounds(lv_obj_get_child(row, 1));
    assert(!app_ui_stats().visible_emote_images && ink(capture(), body) > 0);

    // Bound the pool at eight actual occurrences in a full 500-character line;
    // remaining occurrences retain their normal text fallback.
    std::string maximum;
    for (unsigned i = 0; i < 83; ++i) maximum += "Kappa ";
    maximum += "ok"; assert(maximum.size() == 500);
    id = fresh("viewer_name_", maximum);
    std::array<AppUiEmote, 9> many{};
    for (unsigned i = 0; i < many.size(); ++i) many[i] = {"25", "Kappa", static_cast<std::uint16_t>(i * 6), 5, true};
    emotes(id, many.data(), many.size()); app_ui_scroll(-100000); pump(128);
    visible.clear(); images(author_row("viewer_name_"), visible);
    assert(visible.size() == 8 && app_ui_stats().sprite_cache_bytes <= 2U * 1024 * 1024);
    assert(app_ui_stats().history_height > 480);
    app_ui_scroll(200); pump(64); assert(app_ui_stats().paused);
    std::string unicode_maximum;
    for (unsigned i = 0; i < 125; ++i) unicode_maximum += u8"にこ💜 "; // Exactly 500 Unicode scalars.
    id = fresh("viewer_name_", unicode_maximum);
    for (unsigned i = 0; i < many.size(); ++i)
        many[i] = {"25", u8"にこ", static_cast<std::uint16_t>(i * 11), 6, true};
    emotes(id, many.data(), many.size()); app_ui_scroll(-100000); pump(128);
    visible.clear(); images(author_row("viewer_name_"), visible);
    assert(visible.size() == 8 && app_ui_stats().history_height > 480 &&
           app_ui_stats().sprite_cache_bytes <= 2U * 1024 * 1024);

    // Asset arrival reflows compact inline boxes once, retaining the paused
    // message anchor and its pixel offset while rows above it change height.
    asset_ready = false;
    app_ui_set_fixture_mode(false); app_ui_set_fixture_mode(true);
    std::string tokens; for (unsigned i = 0; i < 20; ++i) tokens += "Kappa ";
    for (unsigned i = 0; i < many.size(); ++i)
        many[i] = {"25", "Kappa", static_cast<std::uint16_t>(i * 6), 5, true};
    for (unsigned i = 0; i < 8; ++i) {
        const auto message = "arrival-" + std::to_string(i);
        assert(app_ui_append("100", message.c_str(), "300", "arrival_viewer", tokens.c_str()));
        app_ui_set_message_emotes("100", message.c_str(), many.data(), 8);
    }
    app_ui_go_live(); pump(128); app_ui_scroll(-100000); pump(64);
    const auto row_height = lv_obj_get_height(author_row("arrival_viewer"));
    app_ui_scroll(row_height * 2 + 10); pump(64);
    const auto before_arrival = app_ui_stats();
    assert(before_arrival.anchor_sequence == 3 && before_arrival.anchor_offset == -10);
    asset_ready = true; pump(128);
    const auto after_arrival = app_ui_stats();
    assert(after_arrival.paused && after_arrival.anchor_sequence == before_arrival.anchor_sequence &&
           after_arrival.anchor_offset == before_arrival.anchor_offset && after_arrival.scroll_y < before_arrival.scroll_y);

    // Full names wrap in a stable 224px column outside the cog's 72px target.
    const std::string maximum_name(25, 'W');
    id = fresh(maximum_name.c_str(), "Readable message.");
    auto *name = find_label(lv_scr_act(), maximum_name.c_str()); assert(name);
    const auto name_area = bounds(name); row = lv_obj_get_parent(name); body = bounds(lv_obj_get_child(row, 1));
    assert(lv_label_get_long_mode(name) == LV_LABEL_LONG_WRAP && name_area.x2 < 248 && name_area.y2 < body.y1);
    assert(lv_obj_get_style_text_font(name, LV_PART_MAIN) == &lv_font_montserrat_20);
    lv_point_t measured_name{};
    lv_txt_get_size(&measured_name, maximum_name.c_str(), &lv_font_montserrat_20, 0, 4, 224, LV_TEXT_FLAG_NONE);
    assert(lv_obj_get_height(name) == measured_name.y && measured_name.y > 23 && ink(capture(), name_area) > 0);
    for (const auto &unit : {std::string(u8"Ж"), std::string(u8"Ω"), std::string(u8"🔥")}) {
        std::string unicode_name; for (unsigned i = 0; i < 25; ++i) unicode_name += unit;
        id = fresh(unicode_name.c_str(), "Readable Unicode author.");
        name = find_label(lv_scr_act(), unicode_name.c_str()); assert(name);
        assert(lv_obj_get_style_text_font(name, LV_PART_MAIN) != &lv_font_montserrat_20);
        assert(lv_font_get_line_height(lv_obj_get_style_text_font(name, LV_PART_MAIN)) == 27);
        assert(bounds(name).x2 < 248 && bounds(name).y2 < bounds(lv_obj_get_child(lv_obj_get_parent(name), 1)).y1);
    }
    id = fresh(u8"日本語_viewer", "Unsupported display script uses the exact login.", "viewer_name_");
    assert(find_label(lv_scr_act(), "viewer_name_") && !find_label(lv_scr_act(), u8"日本語_viewer"));
    assert(lv_obj_get_style_text_font(find_label(lv_scr_act(), "viewer_name_"), LV_PART_MAIN) == &lv_font_montserrat_20);
    const auto fallback_pixels = capture();
    id = fresh("viewer_name_", "Unsupported display script uses the exact login.", "viewer_name_");
    const auto login_pixels = capture();
    assert(!std::memcmp(fallback_pixels.data(), login_pixels.data(), sizeof(login_pixels)));
    tap(30, 45); pump(500); assert(find_label(lv_scr_act(), "viewer_name_"));
    app_ui_go_live(); pump(64);
    id = fresh("viewer_name_", std::string(500, 'W'));
    auto *pause = lv_obj_get_parent(find_label(lv_scr_act(), LV_SYMBOL_PAUSE)); assert(pause);
    assert(lv_obj_get_width(pause) == 52 && lv_obj_get_height(pause) == 52 &&
           lv_obj_get_style_bg_opa(pause, LV_PART_MAIN) == LV_OPA_30);
    std::puts("Inline/layout pixels passed: literal fallback, arrival sprite invalidation, no duplicate/global replacement, exact UTF-8 spans, repeated and bounded8 emotes, reserved-marker collision, paused reflow anchors, full25-character names, ASCII20/Unicode24, exact login fallback pixels and52px translucent pause.");
}
