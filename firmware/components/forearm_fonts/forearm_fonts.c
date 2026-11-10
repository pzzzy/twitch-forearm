#include "forearm_fonts.h"

#include <stdbool.h>
#include <string.h>

LV_FONT_DECLARE(forearm_emoji_24);
LV_FONT_DECLARE(forearm_text_24);

static lv_font_t body_font;
static lv_font_t emoji_font;
static lv_font_t extended_font;
static lv_font_t replacement_font;
static bool initialized;

static bool invisible_format_character(uint32_t letter) {
    return letter < 0x20 || letter == 0xf8ff ||
           letter == 0x200b || letter == 0x200c || letter == 0x200d ||
           letter == 0x2060 || letter == 0xfeff ||
           (letter >= 0xfe00 && letter <= 0xfe0f) ||
           (letter >= 0xe0020 && letter <= 0xe007f) ||
           (letter >= 0xe0100 && letter <= 0xe01ef);
}

static bool replacement_description(const lv_font_t* font,
                                    lv_font_glyph_dsc_t* description,
                                    uint32_t letter, uint32_t next) {
    (void)font;
    (void)next;
    if (invisible_format_character(letter)) {
        memset(description, 0, sizeof(*description));
        description->bpp = 4;
        return true;
    }
    // Deliberately use a recognizable question mark for an unsupported script
    // or emoji. Never silently draw LVGL's missing-glyph outline box.
    return lv_font_montserrat_24.get_glyph_dsc(
        &lv_font_montserrat_24, description, '?', 0);
}

static const uint8_t* replacement_bitmap(const lv_font_t* font, uint32_t letter) {
    (void)font;
    if (invisible_format_character(letter)) return NULL;
    return lv_font_montserrat_24.get_glyph_bitmap(&lv_font_montserrat_24, '?');
}

static void initialize(void) {
    if (initialized) return;
    replacement_font = lv_font_montserrat_24;
    replacement_font.get_glyph_dsc = replacement_description;
    replacement_font.get_glyph_bitmap = replacement_bitmap;
    replacement_font.fallback = NULL;
    emoji_font = forearm_emoji_24;
    emoji_font.fallback = &replacement_font;
    extended_font = forearm_text_24;
    extended_font.line_height = 27;
    extended_font.base_line = 5;
    extended_font.fallback = &emoji_font;
    body_font = lv_font_montserrat_24;
    body_font.fallback = &extended_font;
    initialized = true;
}

const lv_font_t* forearm_body_font(void) {
    initialize();
    return &body_font;
}

const lv_font_t* forearm_emoji_font(void) {
    initialize();
    return &emoji_font;
}

const lv_font_t* forearm_fallback_font(void) {
    initialize();
    return &extended_font;
}
