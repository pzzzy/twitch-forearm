#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

// Call from the LVGL-owning task. Montserrat24 -> extended text24 ->
// Noto Emoji24 -> readable replacement. No runtime rasterizer.
const lv_font_t* forearm_body_font(void);
const lv_font_t* forearm_emoji_font(void);

// Attach this to a Montserrat24 font's fallback field. Adds extended Latin,
// Greek, Cyrillic, punctuation, currency and arrows before the emoji fallback.
const lv_font_t* forearm_fallback_font(void);

#ifdef __cplusplus
}
#endif
