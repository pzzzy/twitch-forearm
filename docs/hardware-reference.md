# Connected hardware and board reference

Updated 2026-11-10 for the native 0.4.0-beta.1 development candidate on one reference unit, with historical v0.3.2 evidence retained below. Machine-readable observations and reference mappings are in [board.json](../hardware/board.json). Native panel output, physical touch communication and live Twitch reception have operated. Unique device/account identities are omitted publicly. Actual observations are distinct from manufacturer specifications and unqualified features.

## Identification and confidence

The USB and ROM audit identified **ESP32-S3 QFN56 revision 0.2, N16R8**, through Espressif USB `303a:1001`; unique unit identifiers are retained privately. Factory boot reported a **478 MB SDSC card**. The captured factory app contains `JC3248W535`, the path `/src/driver/screen_init/axs15231b/esp_bsp.c`, and AXS15231B display/touch symbols. Native firmware now uses the 8 MiB PSRAM, drives the full-frame panel path and communicates with physical touch at SDA4/SCL8/0x3B. No controller identification-register readback or exhaustive PSRAM stress test has been performed. Physical PCB revision, full touch-area calibration, battery installation and final performance qualification remain separate checks.

The product reference is the **Guition / Shenzhen Jingcai JC3248W535** family. The manufacturer specification names capacitive-touch SKU `JC3248W535C_I_Y`; `JC3248W535EN` is the English reference-package name, so it should not be treated as a unique hardware revision.

The user-supplied [purchase reference is this AliExpress item](https://www.aliexpress.us/item/3256807380017698.html). The available page fetch redirected to AliExpress login, so its listing contents and specifications were not independently verified. This link identifies the reported purchase; technical evidence below comes from the manufacturer material and tested unit.

The [manufacturer specification](https://www.guition.com/icms/upload/fb081940d6fc11f09850077a33e1404f/FTPData/UEditor/file/2026121/1768961095054/JC3248W535%20Specifications-EN.pdf), pages 3-6, describes a 3.5-inch **320 x 480 AXS15231B IPS TFT**, capacitive touch, 16 MB flash, and 8 MB PSRAM. Active area is **49.0 x 73.4 mm in portrait**, or **73.4 x 49.0 mm in landscape**. The supplied module/enclosure outline is **94.5 x 62.0 mm** in landscape. The specification lists 5 V input, about 150 mA and about 80 g; these are reference figures, not measurements of this unit with Wi-Fi/chat running. It also claims lithium charging/discharging and overcharge/overdischarge protection.

## Source provenance

The [manufacturer ZIP URL](http://pan.jczn1688.com/directlink/1/HMI%20display/JC3248W535EN.zip) returned HTTP 403 in this session. Its bytes were not downloaded or authenticated. A public [NorthernMan54/JC3248W535EN mirror](https://github.com/NorthernMan54/JC3248W535EN/tree/56161e8269dbb2c32296dcc6e44d4a00f1a2b97e) contains a vendor-package directory as well as a separately modified top-level project. This audit uses the **vendor `JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL` files**, not the top-level `src` replacements.

Eleven selected source/reference files were downloaded to `.local/vendor-reference`, pinned to commit `56161e8269dbb2c32296dcc6e44d4a00f1a2b97e`. Their Git blob SHA-1 values were checked against the API tree; SHA-256 values and exact encoded download URLs are recorded in `.local/vendor-reference/manifest.json` and [board.json](../hardware/board.json). This verifies the mirror snapshot, not equivalence with the unavailable manufacturer ZIP. No vendor executable was run.

The current official specification was read through the web PDF reader. A direct local download returned HTTP 403, so the locally rendered PDF is the pinned mirror copy. Its relevant specification and mechanical pages were visually inspected alongside the official PDF; byte equality is not asserted. Both schematic PNGs were also visually inspected.

Primary reference files:

- [Original BSP pins](https://raw.githubusercontent.com/NorthernMan54/JC3248W535EN/56161e8269dbb2c32296dcc6e44d4a00f1a2b97e/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/esp_bsp.h) and [BSP initialization](https://raw.githubusercontent.com/NorthernMan54/JC3248W535EN/56161e8269dbb2c32296dcc6e44d4a00f1a2b97e/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/esp_bsp.c).
- [Display/touch interface definitions](https://raw.githubusercontent.com/NorthernMan54/JC3248W535EN/56161e8269dbb2c32296dcc6e44d4a00f1a2b97e/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/esp_lcd_axs15231b.h), [panel transfer implementation](https://raw.githubusercontent.com/NorthernMan54/JC3248W535EN/56161e8269dbb2c32296dcc6e44d4a00f1a2b97e/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/esp_lcd_axs15231b.c), and [LVGL port](https://raw.githubusercontent.com/NorthernMan54/JC3248W535EN/56161e8269dbb2c32296dcc6e44d4a00f1a2b97e/JC3248W535EN/1-Demo/Demo_Arduino/DEMO_LVGL/lv_port.c).
- Manufacturer-drawn schematic [sheet 1](https://raw.githubusercontent.com/NorthernMan54/JC3248W535EN/56161e8269dbb2c32296dcc6e44d4a00f1a2b97e/JC3248W535EN/5-IO%20pin%20distribution/JC3248W535-1.png) and [sheet 2](https://raw.githubusercontent.com/NorthernMan54/JC3248W535EN/56161e8269dbb2c32296dcc6e44d4a00f1a2b97e/JC3248W535EN/5-IO%20pin%20distribution/JC3248W535-2.png), both marked JC3248W535 V1.0.

## Display and touch mapping

| Function | GPIO/reference configuration | Evidence |
| --- | --- | --- |
| QSPI chip select | 45 | BSP and schematic |
| QSPI clock | 47 | BSP and schematic |
| QSPI D0 / D1 / D2 / D3 | 21 / 48 / 40 / 39 | BSP and schematic |
| Tearing-effect input | 38, falling-edge synchronization | BSP and schematic |
| Backlight | 1, active high | BSP PWM and schematic MOSFET |
| Panel reset | No dedicated GPIO in BSP | Schematic panel RST follows board RST/EN |
| QSPI D/C | None | Actual QSPI IO config sets `dc_gpio_num=-1` |
| Touch SDA / SCL | **4 / 8 operational on this unit** | Native physical controller reads; schematic labels conflict |
| Touch address | **7-bit 0x3B** | Native reads and AXS15231B touch definition |
| Touch bus | I2C port 0, 400 kHz | Native driver and BSP |
| Touch interrupt/reset | Both disabled (`-1`) in BSP | BSP polls touch; schematic separately shows TP_INT from GPIO3 |

**SDA/SCL discrepancy:** schematic sheet 2 labels GPIO4 as `TP_SCL` and GPIO8 as `TP_SDA`. The BSP initializes **SDA=4 and SCL=8**, which the native application's physical controller communication confirms on this unit. Keep the schematic-label conflict documented rather than swapping the operational mapping. This does not replace four-corner coordinate calibration. The schematic shows 4.7 kOhm pull-ups on both lines.

The BSP header also contains `QSPI_DC=GPIO8`, but its QSPI transport does **not** use that constant. Configuring GPIO8 as a display D/C output would conflict with the touch-clock assignment. Likewise, stale comments such as “ESP-BOX-3” and unrelated I80 resolution macros are not evidence of this board's hardware.

Backlight uses LEDC at 5 kHz with 10-bit duty resolution. The installed native UI is **320 x 480 portrait**. The original vendor demo's software-rotated **480 x 320 landscape UI** is a reference, not an implemented native orientation. Full corner calibration and alternate forearm orientations remain to be qualified.

## Refresh behavior that affects smooth chat

The vendor QSPI configuration is **SPI2, mode 3, 40 MHz, quad data**, with 32-bit commands and 8-bit parameters. The vendor LVGL port forces `full_refresh=1`, uses one full RGB565 draw buffer in PSRAM (**307,200 bytes**), and copies/rotates through two internal DMA staging buffers of **30,720 bytes each**. It waits on tearing-effect synchronization before a frame transfer.

The panel driver's QSPI path skips the row-address command (`RASET`). It starts writing at native `y_start=0` with `RAMWR` and uses `RAMWRC` to continue later chunks. Therefore, **arbitrary independent partial rectangles are not a qualified behavior of this source**. Begin with ordered full-frame transfers and the board-specific initialization table. Evaluate partial updates only with a driver path proven to address the required regions correctly. Changing a generic LVGL flush flag is insufficient evidence.

At 40 MHz over four data lines, a 307,200-byte frame has an ideal wire-time lower bound of **15.36 ms**. This calculation excludes commands, copy/rotation, synchronization, scheduling, and network work; it is not a measured 65 fps capability. The reference TE timing constants are 13/3 ms, not a guarantee of delivered animation rate. Profile sustained frame time and touch latency while TLS receives chat bursts. A stable 30 fps with readable motion is a sensible first qualification target, with higher rates dependent on measurements.

The adaptation preserves this full-frame continuation protocol. A visually inspected RGB565 framebuffer capture shows native portrait chat, Unicode and emotes. The v0.3.0 running-device replay completed with zero flush/I2C errors and TE edges present; its injected-scroll baseline was 15.503 fps. A controlled v0.3.2 comparison using real chat and TLS measured about 15.34 fps with row-sprite caching off versus 15.39 fps on, with identical captured pixels. That historical build kept row-sprite caching off because its gain was negligible.

The beta uses two full 307,200-byte PSRAM composition buffers and the two 30,720-byte internal DMA strips, with explicit ownership while drawing overlaps transfer. Its defaults enable row-sprite caching and opaque-background culling, with a 32 KiB/eight-way data cache and 64-byte lines. A preliminary candidate completed two paused warm-scroll runs at **30.16 physical frame transfers/s**, with UI-frame p95 **30,451 / 30,608 µs** and authenticated TLS connected. The fixture was 30 synthetic 500-byte ASCII/emoji/mixed messages and 500 programmatic 12-pixel steps per run. These are not live-arrival, physical-drag or photon-latency results; the exact committed release image must repeat them. See [verification](verification.md) for candidate evidence, historical measurements and remaining physical/soak gates.

Physical touch traces distinguish Down/Contact/Up from nonzero-header no-data packets. The parser now retains state through idle reports and releases on explicit Up. After this fix the user physically confirmed long-message Back; this is a narrow interaction check, not complete gesture/location qualification.

The display sources carry Apache-2.0/Espressif copyright headers. Preserve their notices if adapting them; the mirror's top-level license does not remove file-level obligations.

## SD card, audio, expansion, and power

| Peripheral | Reference wiring |
| --- | --- |
| microSD SPI | CS=10, CLK=12, MOSI=11, MISO=13 |
| Speaker amplifier I2S | LRCLK=2, BCLK=42, DATA out=41 |
| Speaker connector | P6, 2-pin JST 1.25 mm in schematic |
| Native USB | D-=19, D+=20 |
| P2 expansion GPIO order | 5, 6, 7, 15, 16, 46, 9, 14 |
| P3/P4 | GND, 3.3 V, GPIO17, GPIO18 |
| Battery voltage sense | GPIO5; 33 kOhm upper / 100 kOhm lower divider |
| Battery connector | P5, JST 1.25 mm 2-pin; schematic pin 1 BAT+, pin 2 GND |

The GPIO5 divider gives a nominal battery-to-ADC multiplier of **1.33**, not 2.0. It needs calibrated ADC conversion and voltage comparison before being used for charge percentage or cutoff decisions. GPIO5 is also present on the expansion connector, so it is not an unconstrained spare input.

The reference battery circuit has a power controller with a `KEY` input operated by SW1, plus a separate 3.3 V regulator. The drawing does not identify a readable controller part number. Do not treat the power button as a known ESP32 input, assume a charging current, or infer fuel-gauge support. Physical connector polarity, installed cell, charge/protection behavior, current consumption, and runtime remain unverified. Confirm the actual hardware before choosing a battery or enclosure. The 478 MB card observed at factory boot must remain intact; no formatting or format-on-mount-failure is part of bring-up.

## Recovery and remaining qualification

The full **16,777,216-byte factory flash backup is verified** against a raw device digest, with SHA-256, factory partitions and recovery notes in private `.local/hardware-2026-11-10/manifest.json`. The initial capture issued no flash-write command; later authorized installation did replace factory firmware and initialize project NVS. The microSD card is outside the backup and remains unmodified. No eFuse changes were made.

Remaining work is complete physical PCB/revision matching, four-corner/alternate-orientation touch checks, repeated gestures, final frame/input timing under TLS, extended memory/radio soak, and power/battery/thermal/enclosure qualification. Native PSRAM use, ordered panel transfers, live Twitch login/reception, actual CDN emote rendering and the specific physical Back interaction already have evidence; they do not establish those broader targets.
