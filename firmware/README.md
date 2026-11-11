# Native firmware

This is the **0.4.0-beta.1 hardware beta** for the Guition JC3248W535 family. It drives AXS15231B display/touch, renders portrait chat with LVGL and connects directly to Twitch. Saved read-only profiles, positioned inline static emotes and full sender-name/login fallback are implemented. Guarded Delete, Timeout, Ban and confirmed Undo/Unban require a moderation-enabled profile and authorized channel role. A download's release notes and attached `qualification-summary.json` identify its tested source, image hash, measured results and evidence scope; historical [verification records](../docs/verification.md) do not qualify another image. Battery, charging, skin temperature and mounting are unqualified; this beta does not claim readiness for forearm wear.

## Build and update

Use ESP-IDF **5.3.6**, with managed dependencies locked to **LVGL 8.4.0** and **esp_websocket_client 1.8.0**. Generated font C sources are included, so ordinary builds need neither npm nor FontTools. With the SDK environment active, run from the project root:

```sh
idf.py -C firmware build
```

Activate an ESP-IDF 5.3.6 development shell for the host operating system before building. The private reference-machine environment script is not required or distributed. `sdkconfig.defaults` selects ESP32-S3, 16 MiB DIO flash, 80 MHz octal PSRAM, native USB Serial/JTAG, 32 KiB instruction cache and a 32 KiB, eight-way data cache with 64-byte lines. Existing `sdkconfig` values take precedence over defaults; verify the generated cache settings when updating an older build directory.

First installation on a factory board changes its partition layout. Before writing, capture and verify the complete 16 MiB flash so factory recovery is possible. The new token NVS region at `0x12000` with size `0x10000` may contain bytes from the factory application; default NVS also changes size. Initialize the project's configuration/token partitions explicitly during first installation. Firmware deliberately reports NVS initialization errors rather than automatically erasing unknown data. Routine app updates must preserve paired NVS. Release instructions distinguish those two operations.

For an already initialized Forearm Chat board, identify its serial port and set `FOREARM_PORT` to that host-specific value. With diagnostic clients closed:

```sh
idf.py -C firmware -p "$FOREARM_PORT" flash
```

The helper auto-selects only when exactly one Espressif USB `303a:1001` device is present. Otherwise, supply an explicit serial port with the helper’s global `--port` option before its action. App-only updates preserve Wi-Fi/token NVS when the partition layout is unchanged. Do not mass-erase a paired board during routine updates. The current app is approximately 1.9 MiB within a 4 MiB slot; the build reports the exact size.

## Storage and recovery

The installed layout reserves two 4 MiB application slots, separate 64 KiB token NVS and 4 MiB assets. Reserved OTA slots do not themselves provide an OTA update service. Wi-Fi setup and the saved channel/read-only profile use default NVS. The configuration decoder accepts the deployed 196-byte record and the new 197-byte record with an explicit profile byte; subsequent writes are committed and read back without resetting saved credentials. Twitch stores one token-pair blob in token NVS and verifies it before accepting a rotation.

The reference unit's factory-to-project migration was performed after a verified full 16 MiB backup. Its image, original partitions, hashes and recovery notes remain private under `../.local/hardware-2026-11-10/`; they are not part of the public repository or transferable to another board. A flash image excludes the microSD card. Firmware does not mount, format or modify that card.

NVS and backups are unencrypted. A separate token partition does not prevent physical extraction. No secure-boot, flash-encryption or other eFuse setting was changed.

## Source ownership

- `board_display.cpp`: board initialization, full-frame transport, touch sampling and instrumentation. Panel initialization and RAMWR/RAMWRC continuation live in `components/forearm_panel`, with upstream notices retained.
- `app_ui.cpp`: single-owner LVGL UI, inline emote layout, complete sender-name fallback and bounded PSRAM history, with stable pause/selection identities and an independent read-only guard.
- `twitch_client.cpp`: Wi-Fi, SNTP, verified TLS, profile-aware Public Device Code OAuth, token lifecycle, exact fragment/sender metadata, EventSub and permission-gated asynchronous Helix moderation. The worker enforces read-only profiles independently of token grants. Copied events use a PSRAM-backed queue; network code does not call UI object APIs.
- `emote_cache.cpp`: fixed-origin static PNG downloads and bounded background decode, publishing immutable descriptors without evicting in-use pixels.
- `main.cpp`: UI owner and USB protocol dispatcher. Physical touch samples and injected diagnostic input have separate telemetry.
- `components/forearm_fonts`: precompiled 24 px text and monochrome emoji fallbacks, source hashes and licenses.

See [native runtime](../docs/native-runtime.md) for behavior and limits. Provision credentials at runtime with `../tools/device.py setup`; do not add them to source or build flags.

## Display and touch qualification

Native orientation is **320 × 480 portrait**. Landscape, runtime rotation and arbitrary partial LCD rectangle transfers are not qualified app features. Logical dirty areas compose into two full PSRAM buffers; an ownership handover lets drawing overlap an ordered native full-frame transfer through two internal DMA staging buffers. Source and staging buffers remain immutable until their respective consumers finish. A reversible internal-strip composition experiment remains in the adapter. Every mode retains full-frame panel transfers and the board-specific register table.

Touch is sampled independently of drawing. Down/Contact/Up parsing, no-data reports, transport-error cancellation and queued contact edges are instrumented. Regression checks include the observed nonzero-header idle packets. After the fix, the user physically confirmed long-message Back. Broader gesture and location testing remains separate; host parser tests alone do not establish all physical interactions.

The earlier v0.3.0 scrolling baseline was about 15.5 presented fps. A controlled v0.3.2 comparison on real received chat with TLS active measured about 15.34 fps with row-sprite caching off and 15.39 fps with it on; the small difference did not justify enabling its 271,728-byte allocation by default. That historical v0.3.2 build kept the experimental cache off.

The beta defaults enable bounded row-sprite caching and opaque-background culling, alongside 64-byte data-cache lines. An earlier uncommitted candidate completed warm-cache synthetic scrolling at **30.16 fps**, with UI-frame p95 **30,451 / 30,608 µs**. Source `54c1169c` later recorded its own two warm-scroll measurements, but failed its active soak. These are historical workloads, not a 30 fps claim during live 500-byte arrivals or physical input. See [verification](../docs/verification.md) for those boundaries and the matching release artifacts for a particular image's qualification.

Body sprites have 32 entries within 2 MiB. The independent author cache has 16
entries, a 256 KiB total budget and 64 KiB per-entry limit; it keys the complete
rendered name, font and dimensions and pins visible entries. Building happens
outside draw callbacks. Misses, allocation failures and unsupported pressed
styles retain normal drawing. ASCII author text is 20 px, supported Unicode 24 px,
wrapped within 224 px. These bounds do not establish frame rate.

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

These cover message/moderation/read-only policy, legacy configuration compatibility, UTF-8 fragment positions, actual LVGL font lookup, panel composition, touch parsing, guarded native confirmation/Undo and fail-closed binary license checks. UI tests use a fake network callback and never send moderation actions. `python3 tools/hardware_check.py --output .local/replay/report.json` explicitly enters synthetic fixture mode, clearing RAM history, and exercises pause, anchors, injected gestures, long Unicode/name bounds, login fallback and positioned emote rendering. It restores the prior viewing mode and distinguishes injection from real controller interaction. See the verification record for exact revision evidence.

The beta dependency baseline and vendor-advisory applicability are documented in
[dependency review](../docs/dependency-review.md). Every downloadable firmware
must pass the exact-input license audit after its final build and include the
matching license/source bundle. Follow the [installation guide](../docs/installing.md)
for version-specific first-install versus update operations.
