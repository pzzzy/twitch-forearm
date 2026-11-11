#include "forearm_fonts.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    const lv_font_t* body = forearm_body_font();
    const lv_font_t* emoji = forearm_emoji_font();
    const lv_font_t* extended = forearm_fallback_font();
    assert(body->line_height == 27 && body->base_line == 5);
    assert(emoji->line_height == body->line_height);
    assert(emoji->base_line == body->base_line);
    assert(extended->line_height == 27 && extended->base_line == 5);
    const uint32_t common[] = {0x1f600, 0x1f602, 0x1f62d, 0x1f525, 0x1f49c,
        0x1f44d, 0x2728, 0x1f30d, 0x1f389, 0x1f914, 0x1f979, 0x2764,
        0x1f680, 0x1f984};
    lv_font_glyph_dsc_t glyph;
    for (unsigned repeat = 0; repeat < 500; ++repeat) {
        const uint32_t cp = common[repeat % (sizeof(common) / sizeof(common[0]))];
        assert(lv_font_get_glyph_dsc(body, &glyph, cp, 0));
        assert(!glyph.is_placeholder && glyph.resolved_font == emoji);
        assert(glyph.adv_w > 0 && glyph.box_w > 0 && glyph.box_h > 0);
        assert(lv_font_get_glyph_bitmap(glyph.resolved_font, cp) != NULL);
    }
    assert(lv_font_get_glyph_dsc(body, &glyph, 'A', 0));
    assert(glyph.resolved_font == body);
    // Regression: cafe/naive/resume accents and an em dash must resolve to
    // their actual text glyphs, rather than the readable '?' replacement.
    const uint32_t text[] = {0x00e9, 0x00ef, 0x00e7, 0x0105, 0x0153, 0x017e,
        0x0395, 0x03bb, 0x03bb, 0x03b7, 0x03bd, 0x03b9, 0x03ba, 0x03ac,
        0x041f, 0x0440, 0x0438, 0x0432, 0x0435, 0x0442, 0x0401,
        0x2014, 0x2018, 0x2019, 0x201c, 0x201d, 0x2026,
        0x20ac, 0x00a3, 0x20bd, 0x2190, 0x2192, 0x2191, 0x2193};
    for (unsigned i = 0; i < sizeof(text) / sizeof(text[0]); ++i) {
        assert(lv_font_get_glyph_dsc(body, &glyph, text[i], 0));
        assert(!glyph.is_placeholder && glyph.resolved_font == extended);
        assert(glyph.adv_w > 0 && glyph.box_w > 0 && glyph.box_h > 0);
        assert(lv_font_get_glyph_bitmap(glyph.resolved_font, text[i]) != NULL);
    }
    const uint32_t formats[] = {0xfe0f, 0xfe0e, 0x200d, 0x200c, 0xe007f};
    for (unsigned i = 0; i < sizeof(formats) / sizeof(formats[0]); ++i) {
        assert(lv_font_get_glyph_dsc(body, &glyph, formats[i], 0));
        assert(glyph.adv_w == 0 && glyph.box_w == 0 && !glyph.is_placeholder);
    }
    lv_font_glyph_dsc_t question;
    assert(lv_font_get_glyph_dsc(body, &question, '?', 0));
    assert(lv_font_get_glyph_dsc(body, &glyph, 0x10ffff, 0));
    assert(!glyph.is_placeholder && glyph.adv_w == question.adv_w);
    assert(lv_font_get_glyph_bitmap(glyph.resolved_font, 0x10ffff) != NULL);
    // Display-name coverage must inspect real cmaps, not this intentionally
    // successful replacement callback (whose is_placeholder is also false).
    assert(lv_font_get_glyph_dsc(body, &glyph, 0x4e00, 0));
    assert(!glyph.is_placeholder);
    assert(!forearm_font_supports_text("一"));
    assert(!forearm_font_supports_text("日本語"));
    assert(!forearm_font_supports_text("viewer_name_漢"));
    const char* supported_names[] = {"viewer_name_", "ASCII_Name123", "café—naïve",
        "Ελληνικά", "Привет", "😀🚀💜", "❤️", "👨‍👩‍👧‍👦", "🇬🇧"};
    for (unsigned i = 0; i < sizeof(supported_names) / sizeof(supported_names[0]); ++i)
        assert(forearm_font_supports_text(supported_names[i]));
    for (unsigned character = 0x20; character <= 0x7e; ++character) {
        const char ascii[] = {(char)character, 0};
        assert(forearm_font_supports_text(ascii));
    }
    assert(!forearm_font_supports_text(NULL));
    assert(!forearm_font_supports_text(""));
    assert(!forearm_font_supports_text("‍️")); // Format-only text has no mapped glyph.
    assert(!forearm_font_supports_text("\x7f")); // Half-open end of ASCII cmap.
    assert(!forearm_font_supports_text("\xc0\x80")); // Overlong NUL.
    assert(!forearm_font_supports_text("\xe2\x82")); // Truncated UTF-8.
    assert(!forearm_font_supports_text("\xed\xa0\x80")); // Surrogate.
    assert(!forearm_font_supports_text("\xf4\x90\x80\x80")); // Beyond Unicode.
    puts("LVGL8 font lookup/coverage passed: 500 emoji lookups, Latin/Greek/Cyrillic, controls, real cmap boundaries, exact ASCII login, unsupported CJK/replacement rejection, strict UTF-8; no shaping claim.");
}
