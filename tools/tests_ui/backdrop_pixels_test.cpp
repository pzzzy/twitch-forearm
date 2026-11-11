// Actual LVGL RGB565 draws, with different poison pixels before each comparison.
// Fake immutable emote assets and heap failures; no network, serial or API sends.
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

bool forearm_ui_test_fail_large_allocations = false;
unsigned forearm_ui_test_allocations = 0;
unsigned forearm_ui_test_frees = 0;
unsigned forearm_ui_test_fail_allocation_at = 0;
static std::int64_t fake_us = 0;
static lv_indev_state_t pointer_state = LV_INDEV_STATE_REL;
static lv_point_t pointer_point{};
static std::array<lv_color_t, 320 * 480> framebuffer{};
static lv_disp_t *display = nullptr;
static unsigned comparisons = 0;
static unsigned author_comparisons = 0;
static lv_img_dsc_t emote{};
static std::array<std::uint8_t, 25 * 28 * 3> emote_pixels{};
static lv_color_t tint_background(const lv_color_filter_dsc_t *, lv_color_t source, lv_opa_t opacity) {
    return lv_color_mix(lv_color_hex(0x321C46), source, opacity);
}

std::int64_t esp_timer_get_time() { return fake_us; }
bool emote_cache_init() { return true; }
bool emote_cache_request(const char *) { return true; }
const lv_img_dsc_t *emote_cache_get(const char *id) { return !std::strcmp(id, "25") ? &emote : nullptr; }
EmoteCacheStats emote_cache_stats() { EmoteCacheStats stats{}; stats.ready = 1; return stats; }

static void pump(unsigned ms = 48) {
    for (unsigned elapsed = 0; elapsed < ms; elapsed += 8) {
        fake_us += 8000;
        lv_tick_inc(8);
        app_ui_tick();
        lv_timer_handler();
    }
}
static void tap(int x, int y) {
    pointer_point = {static_cast<lv_coord_t>(x), static_cast<lv_coord_t>(y)};
    pointer_state = LV_INDEV_STATE_PR; pump(24);
    pointer_state = LV_INDEV_STATE_REL; pump(48);
}
static lv_obj_t *find_label(lv_obj_t *obj, const char *caption) {
    if (lv_obj_has_flag(obj, LV_OBJ_FLAG_HIDDEN)) return nullptr;
    for (int i = static_cast<int>(lv_obj_get_child_cnt(obj)) - 1; i >= 0; --i)
        if (auto *found = find_label(lv_obj_get_child(obj, i), caption)) return found;
    return lv_obj_check_type(obj, &lv_label_class) && !std::strcmp(lv_label_get_text(obj), caption) ? obj : nullptr;
}
static void back() {
    auto *caption = find_label(lv_scr_act(), "Back to chat");
    if (!caption) caption = find_label(lv_scr_act(), LV_SYMBOL_LEFT "  Back to chat");
    assert(caption);
    lv_area_t area{};
    lv_obj_get_coords(lv_obj_get_parent(caption), &area);
    tap((area.x1 + area.x2) / 2, (area.y1 + area.y2) / 2);
    pump(500);
}
static std::uint16_t poison(unsigned index, std::uint16_t seed) {
    return static_cast<std::uint16_t>(seed ^ (index * 131u) ^ (index / 320u * 47u));
}
static std::array<lv_color_t, 320 * 480> render(bool cull, const lv_area_t &clip, std::uint16_t seed) {
    app_ui_set_backdrop_culling(cull);
    lv_obj_update_layout(lv_scr_act());
    for (unsigned i = 0; i < framebuffer.size(); ++i) framebuffer[i].full = poison(i, seed);
    _lv_inv_area(display, nullptr);
    _lv_inv_area(display, &clip);
    const auto allocations = forearm_ui_test_allocations, frees = forearm_ui_test_frees;
    lv_refr_now(display);
    assert(allocations == forearm_ui_test_allocations && frees == forearm_ui_test_frees);
    return framebuffer;
}
static void equivalent(const char *scenario, bool expect_skipped, const lv_area_t &clip = {0, 0, 319, 479}) {
    const auto normal = render(false, clip, 0x1357);
    const auto before = app_ui_stats().backdrop_pixels_skipped;
    const auto optimized = render(true, clip, 0xB46A);
    for (int y = 0; y < 480; ++y) {
        for (int x = 0; x < 320; ++x) {
            const unsigned index = y * 320 + x;
            const bool inside = x >= clip.x1 && x <= clip.x2 && y >= clip.y1 && y <= clip.y2;
            if (inside && normal[index].full != optimized[index].full) {
                std::fprintf(stderr, "%s pixel mismatch at %d,%d: %04x != %04x\n", scenario, x, y,
                             normal[index].full, optimized[index].full);
                assert(false);
            }
            if (!inside) assert(optimized[index].full == poison(index, 0xB46A));
        }
    }
    if (expect_skipped && app_ui_stats().backdrop_pixels_skipped == before)
        std::fprintf(stderr, "%s did not exercise culling (sprites=%u, fallback draws=%u)\n", scenario,
                     app_ui_stats().sprite_cache_bytes, app_ui_stats().backdrop_fallback_draws);
    if (expect_skipped) assert(app_ui_stats().backdrop_pixels_skipped > before);
    else {
        if (app_ui_stats().backdrop_pixels_skipped != before)
            std::fprintf(stderr, "%s unexpectedly exercised culling\n", scenario);
        assert(app_ui_stats().backdrop_pixels_skipped == before);
    }
    ++comparisons;
}
static void fresh(unsigned count, const std::string &text, bool with_emotes = false) {
    app_ui_set_fixture_mode(false);
    app_ui_set_fixture_mode(true);
    app_ui_set_sprite_cache(false);
    for (unsigned i = 0; i < count; ++i) {
        char id[64], author[64];
        std::snprintf(id, sizeof(id), "msg-%u", i);
        std::snprintf(author, sizeof(author), "row-%u", i);
        assert(app_ui_append("100", id, "300", author, text.c_str()));
        if (with_emotes) {
            const auto offset = text.find("Kappa"); assert(offset != std::string::npos);
            const AppUiEmote item{"25", "Kappa", static_cast<std::uint16_t>(offset), 5, true};
            app_ui_set_message_emotes("100", id, &item, 1);
        }
    }
    app_ui_go_live();
    pump(500);
}
static void enable_cache() { app_ui_set_sprite_cache(true); pump(64); }

static void compare_author_pixels(const std::array<lv_color_t, 320 * 480> &native,
                                  const std::array<lv_color_t, 320 * 480> &cached,
                                  const lv_area_t &clip, const char *scenario) {
    for (int y = 0; y < 480; ++y) for (int x = 0; x < 320; ++x) {
        const auto index = y * 320 + x;
        if (x >= clip.x1 && x <= clip.x2 && y >= clip.y1 && y <= clip.y2) {
            if (native[index].full != cached[index].full) {
                std::fprintf(stderr, "author %s mismatch at%d,%d: %04x != %04x\n",
                             scenario, x, y, native[index].full, cached[index].full);
                assert(false);
            }
        } else assert(cached[index].full == poison(index, 0xB46A));
    }
    ++author_comparisons;
}
static void author_equivalent(const char *scenario, bool expect_hit,
                              const lv_area_t &clip = {0, 0, 319, 479}, bool masked = false) {
    lv_draw_mask_fade_param_t mask;
    const lv_area_t mask_area{5, 5, 310, 470};
    app_ui_set_author_cache(false); pump(64);
    if (masked) lv_draw_mask_fade_init(&mask, &mask_area, LV_OPA_50, 5, LV_OPA_COVER, 470);
    const auto native_mask = masked ? lv_draw_mask_add(&mask, nullptr) : -1;
    const auto native = render(false, clip, masked ? 0xB46A : 0x1357);
    if (masked) lv_draw_mask_remove_id(native_mask);
    app_ui_set_author_cache(true); pump(64); // All allocation occurs before DRAW.
    if (masked) assert(app_ui_stats().author_cache_bytes > 0); // A ready cache must still be refused.
    const auto cached_mask = masked ? lv_draw_mask_add(&mask, nullptr) : -1;
    const auto hits = app_ui_stats().author_cache_hits;
    const auto cached = render(true, clip, 0xB46A);
    if (masked) lv_draw_mask_remove_id(cached_mask);
    compare_author_pixels(native, cached, clip, scenario);
    if (expect_hit) {
        if (app_ui_stats().author_cache_hits == hits) std::fprintf(stderr, "author %s did not use cache\n", scenario);
        assert(app_ui_stats().author_cache_hits > hits);
    } else {
        if (app_ui_stats().author_cache_hits != hits) std::fprintf(stderr, "author %s unexpectedly used cache\n", scenario);
        assert(app_ui_stats().author_cache_hits == hits);
    }
    assert(app_ui_stats().author_cache_bytes <= 256U * 1024);
}
static void fresh_authors(const std::vector<std::string> &names, const std::string &text = "Short native body.") {
    app_ui_set_author_cache(false);
    app_ui_set_fixture_mode(false); app_ui_set_fixture_mode(true);
    for (unsigned i = 0; i < names.size(); ++i) {
        const auto id = "author-" + std::to_string(i);
        assert(app_ui_append("100", id.c_str(), "300", names[i].c_str(), text.c_str()));
    }
    app_ui_go_live(); enable_cache(); pump(64);
}

int main() {
    lv_init();
    static lv_disp_draw_buf_t buffer;
    lv_disp_draw_buf_init(&buffer, framebuffer.data(), nullptr, framebuffer.size());
    lv_disp_drv_t driver;
    lv_disp_drv_init(&driver);
    driver.hor_res = 320; driver.ver_res = 480;
    driver.direct_mode = 1; driver.draw_buf = &buffer;
    driver.flush_cb = [](lv_disp_drv_t *drv, const lv_area_t *, lv_color_t *) { lv_disp_flush_ready(drv); };
    display = lv_disp_drv_register(&driver);
    lv_indev_drv_t input;
    lv_indev_drv_init(&input);
    input.type = LV_INDEV_TYPE_POINTER;
    input.read_cb = [](lv_indev_drv_t *, lv_indev_data_t *data) { data->point = pointer_point; data->state = pointer_state; };
    lv_indev_drv_register(&input);
    for (unsigned i = 0; i < 25 * 28; ++i) {
        const auto value = lv_color_make(i * 3, i * 7, i * 11).full;
        emote_pixels[i * 3] = value & 255;
        emote_pixels[i * 3 + 1] = value >> 8;
        emote_pixels[i * 3 + 2] = i % 3 == 0 ? 0 : i % 3 == 1 ? 128 : 255;
    }
    emote.header.cf = LV_IMG_CF_TRUE_COLOR_ALPHA;
    emote.header.w = 25; emote.header.h = 28;
    emote.data = emote_pixels.data(); emote.data_size = emote_pixels.size();
    const auto before_init_allocations = forearm_ui_test_allocations;
    const auto before_init_frees = forearm_ui_test_frees;
    const auto before_init_children = lv_obj_get_child_cnt(lv_scr_act());
    forearm_ui_test_fail_allocation_at = before_init_allocations + 2; // Layout after Store.
    app_ui_init();
    assert(forearm_ui_test_allocations == before_init_allocations + 2);
    assert(forearm_ui_test_frees == before_init_frees + 1); // No partial owner leaked.
    assert(lv_obj_get_child_cnt(lv_scr_act()) == before_init_children);
    assert(app_ui_stats().retained == 0);
    assert(!app_ui_append("100", "uninitialized", "300", "viewer", "Must reject safely."));
    app_ui_tick(); app_ui_pause(); app_ui_go_live();
    forearm_ui_test_fail_allocation_at = 0;
    app_ui_init();
    assert(app_ui_stats().retained == 12); // Retry constructs the UI exactly once.
    std::puts("PSRAM layout initialization passed: second-allocation failure frees history, exposes no partial UI, rejects use, and clean retry initializes defaults.");
    app_ui_set_author_cache(false); // Keep the original64 body-culling cases isolated.
    assert(app_ui_stats().backdrop_culling_enabled);
    assert(app_ui_stats().sprite_cache_enabled);
    app_ui_set_authenticated("Owner", "synthetic");

    fresh(0, ""); equivalent("empty screen", false);
    fresh(3, "Readable body with a few lines. Test margins and author bands.");
    equivalent("cache disabled", false);
    enable_cache(); equivalent("three cached rows", true);
    equivalent("left margin only", false, {0, 0, 10, 479});
    equivalent("partial cached body", true, {25, 45, 110, 64});
    equivalent("mixed clip and margin", true, {0, 70, 317, 180});
    equivalent("floating control below history", false, {230, 450, 319, 479});

    auto *author = find_label(lv_scr_act(), "row-1"); assert(author);
    auto *row = lv_obj_get_parent(author);
    auto *body = lv_obj_get_child(row, 1);
    lv_obj_add_state(row, LV_STATE_PRESSED);
    equivalent("pressed row", true);
    lv_obj_clear_state(row, LV_STATE_PRESSED);
    lv_obj_add_flag(body, LV_OBJ_FLAG_HIDDEN);
    equivalent("hidden cached body with other visible bodies", true);
    lv_obj_clear_flag(body, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN);
    equivalent("hidden cached row", true);
    lv_obj_clear_flag(row, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_style_opa(row, LV_OPA_50, 0);
    equivalent("layered half-opacity row", true); // Other unlayered rows still qualify.
    lv_obj_set_style_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_clip_corner(row, true, 0);
    equivalent("rounded clipped row", true);
    lv_obj_set_style_clip_corner(row, false, 0);
    lv_obj_set_style_radius(row, 0, 0);
    lv_obj_set_style_bg_grad_color(lv_scr_act(), lv_color_hex(0x321046), 0);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_HOR, 0);
    equivalent("unsupported background gradient falls back", false);
    lv_obj_set_style_bg_grad_dir(lv_scr_act(), LV_GRAD_DIR_NONE, 0);

    app_ui_show_setup("Setup overlay", "Opaque overlay must restore the complete backdrop.");
    equivalent("opaque setup overlay", false); back();
    tap(30, 45); pump(500); assert(app_ui_stats().selection_visible);
    equivalent("message-selection overlay", false); back();
    app_ui_set_login("https://www.twitch.tv/activate", "ABCD-EFGH");
    equivalent("login QR overlay", false); app_ui_hide_login(); pump(64);
    equivalent("overlay teardown", true);

    fresh(3, "Allocation failure must never leave a hole.");
    forearm_ui_test_fail_large_allocations = true;
    enable_cache();
    assert(app_ui_stats().sprite_cache_rejected > 0 && app_ui_stats().sprite_cache_bytes == 0);
    equivalent("sprite allocation failure", false);
    forearm_ui_test_fail_large_allocations = false;
    app_ui_set_sprite_cache(false); enable_cache();
    equivalent("allocation recovery", true);
    app_ui_delete_message("100", "msg-1"); pump(64);
    equivalent("deleted row rerasterized", true);

    std::string oversized;
    for (unsigned i = 0; i < 100; ++i) oversized += "line\n";
    fresh(1, oversized); enable_cache();
    equivalent("oversized uncached body", false);
    fresh(2, std::string(500, 'W')); enable_cache();
    equivalent("500-character tall body at tail", true);
    equivalent("floating control over cached body", true, {230, 415, 319, 479});
    for (int delta : {-73, -377, -211, 118, 403}) {
        app_ui_scroll(delta); pump(64); equivalent("clipped tall-row scroll", true);
    }

    fresh(3, "Text plus Kappa as an inline alpha emote.", true); enable_cache();
    assert(app_ui_stats().visible_emote_images > 0);
    equivalent("inline alpha emotes", true);
    fresh(90, "Small body used to evict sprite LRU entries."); enable_cache();
    for (unsigned i = 0; i < 20; ++i) {
        app_ui_scroll(-310); pump(48); equivalent("LRU scroll", true);
    }
    assert(app_ui_stats().sprite_cache_evictions > 0);
    for (unsigned i = 0; i < 12; ++i) {
        app_ui_scroll(421); pump(48); equivalent("LRU reverse scroll", true);
    }
    for (unsigned i = 0; i < 600; ++i) {
        char id[64]; std::snprintf(id, sizeof(id), "evicted-%u", i);
        assert(app_ui_append("100", id, "301", "Replacement", "New history evicts old cache identities."));
    }
    pump(64); equivalent("history eviction and reused rows", true);
    app_ui_set_font_fallback(&lv_font_montserrat_20); pump(64);
    equivalent("font generation invalidates cache", true);
    app_ui_clear_channel("100"); pump(64);
    equivalent("channel tombstones", true);
    std::printf("Native LVGL backdrop culling: %u poisoned-frame comparisons pixel-identical; clip/scroll/cache miss+OOM/eviction/deletion/overlay/pressed/hidden/layer/alpha cases; no cache allocations/frees during draw.\n", comparisons);

    app_ui_set_font_fallback(forearm_fallback_font());
    fresh_authors({"viewer_name_", "another_viewer", "short_name", "fourth_viewer"});
    author_equivalent("short ASCII", true);
    author_equivalent("left-clipped glyphs", true, {15, 8, 70, 28});
    author_equivalent("right-clipped glyphs", true, {70, 8, 180, 28});
    author_equivalent("half-height glyph clip", true, {12, 13, 235, 21});
    author_equivalent("full author/body edge clip", true, {0, 8, 319, 48});
    const std::string maximum_name(25, 'W');
    fresh_authors({maximum_name, "next_viewer"});
    author_equivalent("wrapped25-character ASCII", true);
    author_equivalent("wrapped line partial clip", true, {20, 38, 200, 70});
    for (unsigned start = 0x20; start <= 0x7E; start += 24) {
        std::string printable;
        for (unsigned c = start; c <= 0x7E && c < start + 24; ++c) printable += static_cast<char>(c);
        char scenario[64]; std::snprintf(scenario, sizeof(scenario), "printable ASCII chunk0x%02x", start);
        fresh_authors({printable});
        author_equivalent(scenario, true);
    }
    fresh_authors({std::string(25, 'j')}); author_equivalent("left/right glyph bearing clip", true);
    for (const auto &unit : {std::string(u8"Ж"), std::string(u8"Ω"), std::string(u8"🔥")}) {
        std::string name; for (unsigned i = 0; i < 25; ++i) name += unit;
        fresh_authors({name});
        author_equivalent("full Unicode name", true);
        author_equivalent("Unicode clipped glyphs", true, {15, 17, 220, 60});
    }
    fresh_authors({u8"éΩЖ💜👨‍👩‍👧‍👦"}); author_equivalent("mixed Unicode and zero-width joiners", true);
    fresh_authors({"pressed_viewer"});
    app_ui_set_author_cache(true); pump(64);
    assert(app_ui_stats().author_cache_bytes > 0);
    auto *name = find_label(lv_scr_act(), "pressed_viewer"); assert(name);
    auto *pressed_row = lv_obj_get_parent(name);
    lv_obj_add_state(pressed_row, LV_STATE_PRESSED);
    const auto pressed_hits = app_ui_stats().author_cache_hits;
    const auto cached_pressed = render(true, {0, 0, 319, 479}, 0xB46A);
    assert(app_ui_stats().author_cache_hits == pressed_hits);
    app_ui_set_author_cache(false);
    const auto native_pressed = render(false, {0, 0, 319, 479}, 0x1357);
    compare_author_pixels(native_pressed, cached_pressed, {0, 0, 319, 479}, "pressed ready-cache fallback");
    lv_obj_clear_state(pressed_row, LV_STATE_PRESSED);
    author_equivalent("released original label", true);
    author_equivalent("active fade mask with ready cache", false, {0, 0, 319, 479}, true);
    lv_obj_set_style_opa(pressed_row, LV_OPA_50, 0);
    author_equivalent("layered opacity fallback", false);
    lv_obj_set_style_opa(pressed_row, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(pressed_row, 12, 0); lv_obj_set_style_clip_corner(pressed_row, true, 0);
    author_equivalent("rounded ancestor fallback", false);
    lv_obj_set_style_clip_corner(pressed_row, false, 0); lv_obj_set_style_radius(pressed_row, 0, 0);
    lv_obj_set_style_bg_color(pressed_row, lv_color_hex(0x30203F), 0);
    lv_obj_set_style_bg_opa(pressed_row, LV_OPA_COVER, 0);
    author_equivalent("non-ink background fallback", false);
    lv_obj_set_style_bg_opa(pressed_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_text_opa(name, LV_OPA_50, 0);
    author_equivalent("text opacity fallback", false);
    lv_obj_set_style_text_opa(name, LV_OPA_COVER, 0);
    lv_obj_set_style_text_decor(name, LV_TEXT_DECOR_UNDERLINE, 0);
    author_equivalent("text decoration fallback", false);
    lv_obj_set_style_text_decor(name, LV_TEXT_DECOR_NONE, 0);
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_LEFT, 0);
    lv_obj_set_style_base_dir(name, LV_BASE_DIR_RTL, 0);
    author_equivalent("explicit RTL base with left alignment", false);
    lv_obj_set_style_base_dir(name, LV_BASE_DIR_AUTO, 0);
    author_equivalent("nondefault automatic base direction", false);
    lv_obj_set_style_base_dir(name, LV_BASE_DIR_LTR, 0);
    lv_color_filter_dsc_t filter; lv_color_filter_dsc_init(&filter, tint_background);
    lv_obj_set_style_bg_color(pressed_row, lv_color_hex(0x08090C), 0);
    lv_obj_set_style_bg_opa(pressed_row, LV_OPA_COVER, 0);
    lv_obj_set_style_color_filter_dsc(pressed_row, &filter, 0);
    lv_obj_set_style_color_filter_opa(pressed_row, LV_OPA_50, 0);
    author_equivalent("filtered ancestor ink background", false);
    lv_obj_set_style_color_filter_opa(pressed_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_color_filter_dsc(pressed_row, nullptr, 0);
    lv_obj_set_style_bg_opa(pressed_row, LV_OPA_TRANSP, 0);
    lv_obj_set_style_bg_img_src(pressed_row, &emote, 0);
    author_equivalent("transparent ancestor background image", false);
    lv_obj_set_style_bg_img_src(pressed_row, nullptr, 0);
    lv_obj_set_style_border_side(pressed_row, LV_BORDER_SIDE_FULL, 0);
    lv_obj_set_style_border_width(pressed_row, 18, 0);
    author_equivalent("ancestor border overlaps author", false);
    lv_obj_set_style_border_width(pressed_row, 1, 0);
    lv_obj_set_style_border_side(pressed_row, LV_BORDER_SIDE_BOTTOM, 0);
    app_ui_set_author_cache(false); app_ui_set_author_cache(true); pump(64);
    assert(app_ui_stats().author_cache_bytes > 0);
    lv_obj_add_flag(pressed_row, LV_OBJ_FLAG_HIDDEN);
    const auto hidden_hits = app_ui_stats().author_cache_hits;
    const auto cached_hidden = render(true, {0, 0, 319, 479}, 0xB46A);
    assert(app_ui_stats().author_cache_hits == hidden_hits);
    app_ui_set_author_cache(false);
    const auto native_hidden = render(false, {0, 0, 319, 479}, 0x1357);
    compare_author_pixels(native_hidden, cached_hidden, {0, 0, 319, 479}, "hidden ready cache never covers");
    lv_obj_clear_flag(pressed_row, LV_OBJ_FLAG_HIDDEN);
    forearm_ui_test_fail_large_allocations = true;
    author_equivalent("author allocation failure uses native", false);
    assert(app_ui_stats().author_cache_rejected && !app_ui_stats().author_cache_bytes);
    forearm_ui_test_fail_large_allocations = false;
    author_equivalent("author allocation recovery", true);
    fresh_authors({std::string(100, 'W')});
    author_equivalent("oversized author uses native", false);
    assert(!app_ui_stats().author_cache_bytes);
    fresh_authors({"shared_viewer", "shared_viewer", "shared_viewer", "shared_viewer"});
    const auto shared_builds = app_ui_stats().author_cache_builds;
    author_equivalent("shared author cache identity", true);
    assert(app_ui_stats().author_cache_builds == shared_builds + 1);
    std::vector<std::string> distinct;
    for (unsigned i = 0; i < 90; ++i) distinct.push_back("distinct_" + std::to_string(i));
    fresh_authors(distinct); app_ui_set_author_cache(true); pump(64);
    for (unsigned i = 0; i < 40; ++i) { app_ui_scroll(-180); pump(48); }
    assert(app_ui_stats().author_cache_evictions > 0 && app_ui_stats().author_cache_bytes <= 256U * 1024);
    author_equivalent("author LRU scroll and eviction", true);
    app_ui_set_font_fallback(forearm_fallback_font()); pump(64);
    author_equivalent("font generation invalidation", true);
    std::printf("Native LVGL author cache: %u native-label poisoned-frame comparisons pixel-identical; ASCII25/Unicode100B/clips/pressed/mask/layers/OOM/LRU; no cache allocation/free during DRAW.\n", author_comparisons);
}
