#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Call from the LVGL-owning task. Montserrat24 -> extended text24 ->
// Noto Emoji24 -> readable replacement. No runtime rasterizer.
const lv_font_t* forearm_body_font(void);
// Compiled font sizes 16/18/20/22/24/28/30/32/36; unknown sizes use the default 24.
// Smaller/larger sizes have native text bitmaps and bounded emoji fallbacks.
const lv_font_t* forearm_body_font_for_size(unsigned pixels);
const lv_font_t* forearm_emoji_font(void);

// Attach this to a Montserrat24 font's fallback field. Adds extended Latin,
// Greek, Cyrillic, punctuation, currency and arrows before the emoji fallback.
const lv_font_t* forearm_fallback_font(void);

// True only for valid UTF-8 represented by real glyphs in the compiled body
// font chain. The readable '?' replacement does not establish support. Hidden
// ZWJ/variation/tag controls may accompany mapped text; shaping is not implied.
// NULL, empty/format-only text and any unsupported base codepoint return false.
bool forearm_font_supports_text(const char* utf8);

#ifdef __cplusplus
}
#endif
