# Native firmware

This is the **0.4.0-beta.1 hardware-beta development candidate** for the Guition JC3248W535 family. It drives AXS15231B display/touch, renders portrait chat with LVGL and connects directly to Twitch. Guarded Delete, Timeout, Ban and confirmed Undo/Unban are implemented for authorized channel roles. A preliminary candidate sustained 30.16 completed panel frames/s in paused warm-scroll tests; live Delete and Timeout have Twitch acknowledgements. Exact-release performance repetition, Ban/Undo cleanup, physical checks and the one-hour active soak remain open in the [verification record](../docs/verification.md). Battery, charging, skin temperature and mounting are unqualified; this beta does not claim readiness for forearm wear.

## Build and update

Use ESP-IDF **5.3.6**, with managed dependencies locked to **LVGL 8.4.0** and **esp_websocket_client 1.8.0**. Generated font C sources are included, so ordinary builds need neither npm nor FontTools. With the SDK environment active, run from the project root:

```sh
idf.py -C firmware build
```

Activate an ESP-IDF 5.3.6 development shell for the host operating system before building. The private reference-machine environment script is not required or distributed. `sdkconfig.defaults` selects ESP32-S3, 16 MiB DIO flash, 80 MHz octal PSRAM, native USB Serial/JTAG and a 32 KiB, eight-way data cache with 64-byte lines. Existing `sdkconfig` values take precedence over defaults; verify the generated cache settings when updating an older build directory.

First installation on a factory board changes its partition layout. Before writing, capture and verify the complete 16 MiB flash so factory recovery is possible. The new token NVS region at `0x12000` with size `0x10000` may contain bytes from the factory application; default NVS also changes size. Initialize the project's configuration/token partitions explicitly during first installation. Firmware deliberately reports NVS initialization errors rather than automatically erasing unknown data. Routine app updates must preserve paired NVS. Release instructions distinguish those two operations.

For an already initialized Forearm Chat board, identify its serial port and set `FOREARM_PORT` to that host-specific value. With diagnostic clients closed:

```sh
idf.py -C firmware -p "$FOREARM_PORT" flash
```

The helper auto-selects only when exactly one Espressif USB `303a:1001` device is present. Otherwise, supply an explicit serial port with the helper’s global `--port` option before its action. App-only updates preserve Wi-Fi/token NVS when the partition layout is unchanged. Do not mass-erase a paired board during routine updates. The current app is approximately 1.9 MiB within a 4 MiB slot; the build reports the exact size.

## Storage and recovery

The installed layout reserves two 4 MiB application slots, separate 64 KiB token NVS and 4 MiB assets. Reserved OTA slots do not themselves provide an OTA update service. Wi-Fi setup uses default NVS. Twitch stores one token-pair blob in token NVS and verifies it before accepting a rotation.

The reference unit's factory-to-project migration was performed after a verified full 16 MiB backup. Its image, original partitions, hashes and recovery notes remain private under `../.local/hardware-2026-11-10/`; they are not part of the public repository or transferable to another board. A flash image excludes the microSD card. Firmware does not mount, format or modify that card.

NVS and backups are unencrypted. A separate token partition does not prevent physical extraction. No secure-boot, flash-encryption or other eFuse setting was changed.

## Source ownership

- `board_display.cpp`: board initialization, full-frame transport, touch sampling and instrumentation. Panel initialization and RAMWR/RAMWRC continuation live in `components/forearm_panel`, with upstream notices retained.
- `app_ui.cpp`: single-owner LVGL UI, virtualized visible rows and bounded PSRAM history, with stable pause/selection identities.
- `twitch_client.cpp`: Wi-Fi, SNTP, verified TLS, Public Device Code OAuth, token lifecycle, EventSub and permission-gated asynchronous Helix moderation. Copied events use a PSRAM-backed queue; network code does not call UI object APIs.
- `emote_cache.cpp`: fixed-origin static PNG downloads and bounded background decode, publishing immutable descriptors without evicting in-use pixels.
- `main.cpp`: UI owner and USB protocol dispatcher. Physical touch samples and injected diagnostic input have separate telemetry.
- `components/forearm_fonts`: precompiled 24 px text and monochrome emoji fallbacks, source hashes and licenses.

See [native runtime](../docs/native-runtime.md) for behavior and limits. Provision credentials at runtime with `../tools/device.py setup`; do not add them to source or build flags.

## Display and touch qualification

Native orientation is **320 × 480 portrait**. Landscape, runtime rotation and arbitrary partial LCD rectangle transfers are not qualified app features. Logical dirty areas compose into two full PSRAM buffers; an ownership handover lets drawing overlap an ordered native full-frame transfer through two internal DMA staging buffers. Source and staging buffers remain immutable until their respective consumers finish. A reversible internal-strip composition experiment remains in the adapter. Every mode retains full-frame panel transfers and the board-specific register table.

Touch is sampled independently of drawing. Down/Contact/Up parsing, no-data reports, transport-error cancellation and queued contact edges are instrumented. Regression checks include the observed nonzero-header idle packets. After the fix, the user physically confirmed long-message Back. Broader gesture and location testing remains separate; host parser tests alone do not establish all physical interactions.

The earlier v0.3.0 scrolling baseline was about 15.5 presented fps. A controlled v0.3.2 comparison on real received chat with TLS active measured about 15.34 fps with row-sprite caching off and 15.39 fps with it on; the small difference did not justify enabling its 271,728-byte allocation by default. That historical v0.3.2 build kept the experimental cache off.

The beta defaults now enable bounded row-sprite caching and opaque-background culling, alongside 64-byte data-cache lines. On a preliminary candidate, two runs of 500 programmatic 12-pixel scroll steps each completed 501 physical frame transfers at **30.16 fps**, with UI-frame p95 **30,451 / 30,608 µs**. The workload was 30 synthetic 500-byte ASCII/emoji/mixed messages, paused and warm-cached, with authenticated TLS connected. This does not establish 30 fps during live arrivals, physical input latency or the final release image's performance. Sixty-four actual-LVGL host framebuffer comparisons found culling on/off pixel-identical. See [verification](../docs/verification.md) for the candidate evidence and remaining exact-image gates.

## Checks

From the project root:

```sh
cmake -S core -B build/core -DCMAKE_BUILD_TYPE=Debug
cmake --build build/core
ctest --test-dir build/core --output-on-failure
sh firmware/components/forearm_fonts/tools/check-font.sh
python3 firmware/components/forearm_panel/tests/run_host.py
python3 tools/tests_ui/run_host.py
python3 -m unittest discover -s tools/tests_licenses -v
```

These cover the message/moderation policy, actual LVGL font lookup, panel composition, touch parsing, guarded native confirmation/Undo and fail-closed binary license checks. UI tests use a fake network callback and never send moderation actions. In a deliberate demo session, `python3 tools/hardware_check.py --output .local/replay/report.json` additionally exercises running-device pause, anchors, injected gestures, long Unicode messages and emote rendering. It expects demo mode and explicitly distinguishes injected input from real controller interaction. See the verification record for exact revision evidence.

The beta dependency baseline and vendor-advisory applicability are documented in
[dependency review](../docs/dependency-review.md). Every downloadable firmware
must pass the exact-input license audit after its final build and include the
matching license/source bundle. Follow the [installation guide](../docs/installing.md)
for version-specific first-install versus update operations.
