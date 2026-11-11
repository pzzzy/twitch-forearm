#include "forearm_fonts.h"

#include <stdbool.h>
#include <string.h>

LV_FONT_DECLARE(forearm_emoji_24);
LV_FONT_DECLARE(forearm_text_24);
LV_FONT_DECLARE(forearm_text_20);
LV_FONT_DECLARE(forearm_text_16);
LV_FONT_DECLARE(forearm_text_18);
LV_FONT_DECLARE(forearm_text_22);
LV_FONT_DECLARE(forearm_text_28);
LV_FONT_DECLARE(forearm_text_30);
LV_FONT_DECLARE(forearm_text_32);
LV_FONT_DECLARE(forearm_text_36);
LV_FONT_DECLARE(forearm_emoji_16);
LV_FONT_DECLARE(forearm_emoji_20);

struct font_family {
    lv_font_t body, extended, emoji, replacement;
};
static struct font_family sized_families[8];
static const lv_font_t* const sized_base[] = {
    &lv_font_montserrat_16, &lv_font_montserrat_18,
    &lv_font_montserrat_20, &lv_font_montserrat_22,
    &lv_font_montserrat_28, &lv_font_montserrat_30,
    &lv_font_montserrat_32, &lv_font_montserrat_36
};

static lv_font_t body_font;
static lv_font_t emoji_font;
static lv_font_t extended_font;
static lv_font_t replacement_font;
static bool initialized;

static const lv_font_t* replacement_source(const lv_font_t* font) {
    for (unsigned i = 0; i < 8; ++i)
        if (font == &sized_families[i].replacement) return sized_base[i];
    return &lv_font_montserrat_24;
}

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
    (void)next;
    if (invisible_format_character(letter)) {
        memset(description, 0, sizeof(*description));
        description->bpp = 4;
        return true;
    }
    // Deliberately use a recognizable question mark for an unsupported script
    // or emoji. Never silently draw LVGL's missing-glyph outline box.
    const lv_font_t* source = replacement_source(font);
    return source->get_glyph_dsc(source, description, '?', 0);
}

static const uint8_t* replacement_bitmap(const lv_font_t* font, uint32_t letter) {
    if (invisible_format_character(letter)) return NULL;
    const lv_font_t* source = replacement_source(font);
    return source->get_glyph_bitmap(source, '?');
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
    const lv_font_t* const text[] = {
        &forearm_text_16, &forearm_text_18, &forearm_text_20, &forearm_text_22,
        &forearm_text_28, &forearm_text_30, &forearm_text_32, &forearm_text_36
    };
    for (unsigned i = 0; i < 8; ++i) {
        struct font_family* family = &sized_families[i];
        family->body = *sized_base[i];
        family->replacement = *sized_base[i];
        family->replacement.get_glyph_dsc = replacement_description;
        family->replacement.get_glyph_bitmap = replacement_bitmap;
        family->replacement.fallback = NULL;
        family->emoji = i < 2 ? forearm_emoji_16 : i < 4 ? forearm_emoji_20 : forearm_emoji_24;
        family->emoji.line_height = family->body.line_height;
        family->emoji.base_line = family->body.base_line;
        family->emoji.fallback = &family->replacement;
        family->extended = *text[i];
        family->extended.line_height = family->body.line_height;
        family->extended.base_line = family->body.base_line;
        family->extended.fallback = &family->emoji;
        family->body.fallback = &family->extended;
    }
    initialized = true;
}

const lv_font_t* forearm_body_font_for_size(unsigned pixels) {
    initialize();
    switch (pixels) {
        case 16: return &sized_families[0].body;
        case 18: return &sized_families[1].body;
        case 20: return &sized_families[2].body;
        case 22: return &sized_families[3].body;
        case 28: return &sized_families[4].body;
        case 30: return &sized_families[5].body;
        case 32: return &sized_families[6].body;
        case 36: return &sized_families[7].body;
        default: return &body_font;
    }
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
