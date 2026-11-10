# Third-party notices

The root [Apache-2.0 license](LICENSE) covers original Forearm Chat code and
documentation, except where a file or this inventory states otherwise. It does
not relicense third-party code, fonts, trademarks, emotes, or user content.
Retain [NOTICE](NOTICE), this inventory, and applicable full texts in
[LICENSES/](LICENSES/) when redistributing the project. License-text origins and
SHA-256 hashes are in [LICENSES/provenance.json](LICENSES/provenance.json).

## Source included in this repository

| Files | Origin and modifications | License |
| --- | --- | --- |
| `firmware/components/forearm_panel/esp_lcd_axs15231b.c` and `include/esp_lcd_axs15231b.h` | Espressif Systems (Shanghai) CO LTD, 2021–2024 / 2022–2024. Vendor-demo mirror `NorthernMan54/JC3248W535EN` at `56161e8269dbb2c32296dcc6e44d4a00f1a2b97e`, directory `JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL`. Touch implementation removed, display-on semantics and error propagation corrected. Original headers and local modification notices are retained. | [Apache-2.0](LICENSES/Apache-2.0.txt). The mirror's [MIT notice](LICENSES/Panel-mirror-MIT.txt) is also preserved for any mirror-level contribution; it does not replace explicit vendor-file licenses. |
| `firmware/components/forearm_panel/jc3248_init.c` | Espressif Systems (Shanghai) CO LTD, 2021–2024. Initialization table extracted from that snapshot's `esp_bsp.c`, with a local native-component wrapper. | [CC0-1.0](LICENSES/CC0-1.0.txt), as identified in the source header. |
| `firmware/components/forearm_fonts/assets/NotoEmoji[wght].ttf` and generated `forearm_emoji_24.c` | Copyright 2013 Google LLC. Original font plus a modified 24 px monochrome bitmap subset. | [SIL OFL 1.1, Noto Emoji](LICENSES/NotoEmoji-OFL-1.1.txt). |
| `firmware/components/forearm_fonts/assets/Montserrat[wght].ttf` and the Montserrat part of generated `forearm_text_24.c` | Copyright 2024 The Montserrat.Git Project Authors. Original font plus a weight-400 instance and modified 24 px bitmap subset. | [SIL OFL 1.1, Montserrat](LICENSES/Montserrat-2024-OFL-1.1.txt). |
| `firmware/components/forearm_fonts/assets/NotoSans[wdth,wght].ttf` and the Greek part of generated `forearm_text_24.c` | Copyright 2022 The Noto Project Authors. Original font plus a weight-400, width-100 instance and modified Greek bitmap subset. | [SIL OFL 1.1, Noto Sans](LICENSES/NotoSans-OFL-1.1.txt). |

Google Fonts inputs are pinned to commit
`8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5`. Exact URLs, input hashes,
conversion parameters, and output hashes are in the font component's
`assets/provenance.json` and `assets/extended-text-provenance.json`. Generated
font names are `forearm_emoji_24` and `forearm_text_24`. Bitmap font derivatives
remain under OFL-1.1; the surrounding application can use a separate license.
The unmodified TTF inputs retain their original names and copyright notices.

## Firmware dependencies resolved by the build

These dependencies are downloaded using `firmware/main/idf_component.yml` and
`firmware/dependencies.lock`. Their working copies are ignored rather than
vendored into this source repository. Firmware binaries contain linked parts,
so their accompanying license bundles must include the applicable notices.

| Dependency / compiled material | License and attribution |
| --- | --- |
| LVGL 8.4.0 | [MIT](LICENSES/LVGL-8.4.0-MIT.txt), Copyright (c) 2021 LVGL Kft. Source: `lvgl/lvgl`, tag `v8.4.0`; registry content hash is in `firmware/dependencies.lock`. |
| LVGL's enabled `lv_font_montserrat_16/20/24/28.c` | Conversion of `scripts/built_in_font/Montserrat-Medium.ttf`, version 7.200, Copyright 2011 The Montserrat Project Authors; [OFL-1.1](LICENSES/Montserrat-7.200-OFL-1.1.txt). This older font's attribution is separate from the 2024 source above. |
| Symbols in those LVGL fonts | Conversion of `FontAwesome5-Solid+Brands+Regular.woff`, with embedded version Font Awesome 5.9.0 and copyright “Copyright (c) Font Awesome”; [OFL-1.1](LICENSES/FontAwesome-5.9.0-OFL-1.1.txt). Preserve the [upstream 5.9.0 license notice](LICENSES/FontAwesome-5.9.0-license.txt), from [Font Awesome 5.9.0](https://github.com/FortAwesome/Font-Awesome/tree/5.9.0). These are font-derived glyphs; the package's SVG/JS icons are not included. |
| LodePNG version 20201017, bundled by LVGL for PNG decoding | [zlib-style license](LICENSES/LodePNG-20201017.txt), Copyright (c) 2005–2020 Lode Vandevenne. Source: LVGL 8.4.0 `src/extra/libs/png/lodepng.c/.h`. |
| Project Nayuki QR Code generator, bundled by LVGL | [MIT](LICENSES/Nayuki-QR-MIT.txt), Copyright (c) Project Nayuki. Source: LVGL 8.4.0 `src/extra/libs/qrcode/qrcodegen.c/.h`; [upstream project](https://www.nayuki.io/page/qr-code-generator-library). |
| `espressif/esp_websocket_client` 1.8.0 | [Apache-2.0](LICENSES/Apache-2.0.txt). Original Espressif headers remain in the managed component; registry content hash is pinned by `firmware/dependencies.lock`. |
| ESP-IDF 5.3.6, including its Wi-Fi and PHY libraries | Original Espressif code is Apache-2.0, with third-party and per-file exceptions. The pinned SDK's `components/esp_wifi/lib/LICENSE` and `components/esp_phy/lib/LICENSE` are Apache-2.0; verbatim copies are in `LICENSES/esp-idf-5.3.6/`. Do not generalize that to every SDK subcomponent. |

The local dependency notices and actual font binaries' name tables were checked.
LVGL's MIT license does not replace font licenses. The pinned LVGL 8.4.0 package
lacks separate font-license texts, so the full font texts are supplied here.

## Firmware binary-release check

A binary release must pass the exact-input audit after its final build:

```sh
python3 tools/audit_firmware_licenses.py --build-dir PATH --output license-audit.json
```

The reviewed ESP-IDF 5.3.6 baseline is in
[`LICENSES/firmware-5.3.6/coverage.json`](LICENSES/firmware-5.3.6/coverage.json).
The tool resolves every loaded archive member, standalone startup/runtime
object, compiled source candidate and included header from both application and
bootloader link maps, compile databases and Ninja dependency evidence. Loaded
members later discarded are conservatively included. Prebuilt archives require
an exact reviewed hash. Unknown/changed inputs, missing/changed notices, stale
objects, unsupported SDK versions and custom CA sources fail closed.

A release report's `complete:true` applies only to its exact application and
bootloader hashes. Bundle that report, LICENSE, NOTICE, this inventory and the
complete LICENSES tree with those images. A source-only publication or another
build's report does not qualify a binary. The candidate-review helper never
approves its output automatically; dependency changes require renewed review.

Full file-level copyright/license comments are preserved in
[`linked-source-notices.txt`](LICENSES/firmware-5.3.6/linked-source-notices.txt).
Canonical texts include FreeRTOS, lwIP, wpa_supplicant, Newlib, cJSON, HTTP parser,
TLSF, UBSAN, Xtensa/Tensilica/Cadence support, and the GCC/C++ runtimes. **Mbed TLS
uses its Apache-2.0 alternative; wpa_supplicant uses BSD.** Historical alternative
notices are retained for provenance. GCC13.2 runtime code uses GPLv3 with the
GCC Runtime Library Exception3.1, and the target is compiled through an eligible
GCC process. LLVM PSTL headers retain their Apache/LLVM exception. Those runtime
licenses do not relicense independent application code as GPL.

The embedded Mozilla CA-store data uses **MPL-2.0**. Its exact unmodified source
is distributed in [`ca-source/cacrt_all.pem`](LICENSES/esp-idf-5.3.6/ca-source/cacrt_all.pem),
alongside the Espressif [`cacrt_local.pem`](LICENSES/esp-idf-5.3.6/ca-source/cacrt_local.pem)
supplement (SDK Apache-2.0 default), the full
[MPL](LICENSES/firmware-5.3.6/MPL-2.0.txt), and
[pinned provenance](LICENSES/firmware-5.3.6/provenance.json). The build converts
the public PEM certificates into subject/public-key records using the pinned
SDK generator; original source remains available for use and modification under
its respective licenses. Private/custom certificate inputs are not covered.

See the [binary coverage instructions](LICENSES/firmware-5.3.6/README.md) for
review/update procedure. ESP-IDF's official starting inventory is its
[5.3.6 copyright page](https://docs.espressif.com/projects/esp-idf/en/v5.3.6/esp32s3/COPYRIGHT.html);
actual file headers take precedence over summaries.

## Host and font-generation tools

Host requirements (`pyserial` 3.5, Pillow 13.0.0), Node.js, ESP-IDF's toolchain,
`lv_font_conv` 1.5.3 and FontTools 4.60.1 are build/development dependencies,
not vendored runtimes. Font-generation versions and integrity are pinned in
`firmware/components/forearm_fonts/tools/`; the directory retains the converter
and FontTools MIT notices. These tools do not change the fonts' OFL license.

Do not bundle ignored `node_modules/`, Python environments or SDK/toolchain
installations under this inventory alone. A tool bundle requires its complete
dependency notices, including the converter's FreeType/WebAssembly material,
npm transitive dependencies, and FontTools' additional upstream notices. Those
tools are not included in an ordinary firmware source distribution.

## Content and hardware references

No Twitch emote image, real chat recording, vendor schematic, factory firmware,
or manufacturer ZIP is distributed as a project asset. Runtime emotes and chat
remain third-party content subject to their rights and Twitch service terms.
Names and trademarks do not imply endorsement or grant a trademark license.

Hardware listing and download links do not grant permission to redistribute
their photographs, schematics, firmware or prose. Keep `.local/`, credentials,
factory backups, real chat captures and downloaded test assets out of source
and release archives. Screenshots intended for publication should use synthetic
content unless appropriate rights have been established.
