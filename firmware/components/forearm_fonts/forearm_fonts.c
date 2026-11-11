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

static bool font_maps_codepoint(const lv_font_t* font, uint32_t letter) {
    const lv_font_fmt_txt_dsc_t* data = font->dsc;
    for (uint16_t i = 0; i < data->cmap_num; ++i) {
        const lv_font_fmt_txt_cmap_t* map = &data->cmaps[i];
        if (letter < map->range_start) continue;
        const uint32_t relative = letter - map->range_start;
        // range_length is a count. Do not admit the following codepoint via
        // LVGL8's broader descriptor lookup or our readable replacement font.
        if (relative >= map->range_length) continue;
        if (map->type == LV_FONT_FMT_TXT_CMAP_FORMAT0_TINY)
            return map->glyph_id_start + relative != 0;
        if (map->type == LV_FONT_FMT_TXT_CMAP_FORMAT0_FULL) {
            const uint8_t* offsets = map->glyph_id_ofs_list;
            return offsets && map->glyph_id_start + offsets[relative] != 0;
        }
        if (map->type != LV_FONT_FMT_TXT_CMAP_SPARSE_TINY &&
            map->type != LV_FONT_FMT_TXT_CMAP_SPARSE_FULL) continue;
        if (!map->unicode_list) continue;
        uint16_t low = 0, high = map->list_length;
        while (low < high) {
            const uint16_t middle = low + (high - low) / 2;
            if (map->unicode_list[middle] < relative) low = middle + 1;
            else high = middle;
        }
        if (low == map->list_length || map->unicode_list[low] != relative) continue;
        if (map->type == LV_FONT_FMT_TXT_CMAP_SPARSE_TINY)
            return map->glyph_id_start + low != 0;
        const uint16_t* offsets = map->glyph_id_ofs_list;
        return offsets && map->glyph_id_start + offsets[low] != 0;
    }
    return false;
}

bool forearm_font_supports_text(const char* utf8) {
    if (!utf8 || !*utf8) return false;
    const unsigned char* cursor = (const unsigned char*)utf8;
    bool mapped_glyph_seen = false;
    while (*cursor) {
        uint32_t letter = *cursor++;
        if (letter >= 0x80) {
            unsigned continuation;
            uint32_t minimum;
            if (letter >= 0xc2 && letter <= 0xdf) {
                letter &= 0x1f; continuation = 1; minimum = 0x80;
            } else if (letter >= 0xe0 && letter <= 0xef) {
                letter &= 0x0f; continuation = 2; minimum = 0x800;
            } else if (letter >= 0xf0 && letter <= 0xf4) {
                letter &= 7; continuation = 3; minimum = 0x10000;
            } else return false;
            for (unsigned i = 0; i < continuation; ++i) {
                if ((*cursor & 0xc0) != 0x80) return false;
                letter = (letter << 6) | (*cursor++ & 0x3f);
            }
            if (letter < minimum || letter > 0x10ffff ||
                (letter >= 0xd800 && letter <= 0xdfff)) return false;
        }
        if (invisible_format_character(letter)) continue;
        if (!font_maps_codepoint(&lv_font_montserrat_24, letter) &&
            !font_maps_codepoint(&forearm_text_24, letter) &&
            !font_maps_codepoint(&forearm_emoji_24, letter)) return false;
        mapped_glyph_seen = true;
    }
    return mapped_glyph_seen;
}
