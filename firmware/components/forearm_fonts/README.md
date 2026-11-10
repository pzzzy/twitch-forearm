# Forearm message font

`forearm_body_font()` returns LVGL's Montserrat24 with extended text and
antialiased monochrome Noto Emoji24 fallbacks. `forearm_fallback_font()` returns
the extended-text → emoji → readable-replacement chain for attaching to another
Montserrat24 font. `forearm_emoji_font()` remains available as the emoji-only
fallback. The runtime fonts use the same 27 px line height and 5 px baseline as
Montserrat24. They are rasterized ahead of time at 4 bits per pixel; no font
downloads, rasterization, color-image decoding or dynamic glyph cache is required
at runtime. The emoji bitmap is approximately 410 KiB before glyph descriptors
and mapping tables. Extended text adds approximately 113 KiB including tables
in the host object, below the 200 KiB additional-font budget.

Source: Google Fonts `ofl/notoemoji/NotoEmoji[wght].ttf`, pinned to commit
`8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5`. The original font and SIL Open Font
License are in `assets/`. Exact URLs, sizes and SHA-256 hashes are recorded in
`assets/provenance.json`. This is Google's monochrome outline font, rather than
the color Noto font. The font's license applies to these derived bitmaps too.

The text fallback contains **819 additional glyphs** from the same pinned Google
Fonts commit: Montserrat regular for available characters in U+00A0–U+024F,
U+0400–U+052F, U+2000–U+206F, U+20A0–U+20CF and U+2190–U+21FF; Noto Sans for
Greek U+0370–U+03FF. Montserrat does not contain the required Greek alphabet, so
the Greek subset has its own source. The original fonts, separate OFL licenses
and verified Git blob/SHA-256 hashes are in `assets/extended-text-provenance.json`.

Montserrat's variable-font default is weight **100**. Before conversion,
FontTools **4.60.1** explicitly instantiates weight **400** so accented letters
match the regular-weight ASCII body text. Noto Sans is instantiated at weight
400 and width 100. The generated font retains its natural metrics in the C file;
the runtime wrapper aligns its metadata to the primary font's 27/5 metrics.

Converter: official `lvgl/lv_font_conv`, npm version **1.5.3**, with package
integrity and bundled dependencies pinned in `tools/package-lock.json`.
The generated C is compatible with LVGL 8.4.0 and uses uncompressed bitmaps to
avoid per-glyph decompression work while scrolling. The generated C is committed
as source; npm is not needed for an ordinary firmware build.

Coverage includes supported glyphs in U+1F300–U+1FAFF, U+2600–U+27FF, regional
indicators, and common additional emoji symbols. Not every codepoint in a range
is assigned or present in the font. Unsupported characters receive a readable
`?` instead of LVGL's missing-glyph box. Variation selectors, joiners and emoji
tag controls are zero width.

LVGL 8 performs codepoint lookup, not full emoji shaping. ZWJ families,
skin-tone combinations, country-flag pairs and keycap sequences may appear as
their individual components rather than a single combined glyph. This component
does not claim complete Unicode script coverage or color emoji rendering.

Regenerate after checking the input hashes:

```sh
cd tools
npm ci --ignore-scripts --no-audit --no-fund
sh generate.sh
sh generate-text.sh
```

`sh tools/check-font.sh` compiles the real LVGL 8.4 font lookup implementation
with this component on the host. It checks 500 emoji lookups, real bitmap
resolution, accented Latin (`café naïve résumé`), Greek, Cyrillic, punctuation,
currency, arrows, fallback order, zero-width selectors and explicit replacement
for an unsupported codepoint. Final device linking can differ slightly from host
object sizes. Ordinary firmware builds need neither npm nor FontTools.

The font files and generated bitmap data are licensed under **SIL OFL 1.1**,
separately from the project's Apache-2.0 code. Preserve the complete copyright
and license texts in `assets/OFL.txt`, `assets/Montserrat-OFL.txt` and
`assets/NotoSans-OFL.txt`. Generated C files carry OFL headers; both generation
scripts restore those headers after conversion. Font wrapper and project tool
code use the root project license. The converter and FontTools retain their own
MIT notices in `tools/`.

LVGL's built-in Montserrat ASCII/symbol fonts have separate historical
Montserrat and Font Awesome attribution. See the root `THIRD_PARTY_NOTICES.md`
and `LICENSES/` for those full texts and dependency scope. Include applicable
font licenses with firmware binaries as well as source distributions.

Official references:

- https://github.com/google/fonts/tree/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/notoemoji
- https://github.com/lvgl/lv_font_conv
- https://github.com/google/fonts/tree/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/montserrat
- https://github.com/google/fonts/tree/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/notosans
- https://fonttools.readthedocs.io/en/latest/varLib/instancer.html
