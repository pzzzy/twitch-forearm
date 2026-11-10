# Native application verification — November 10, 2026

## Current beta development status

The 0.4.0-beta.1 hardware-beta candidate now implements guarded Delete,
30/60/600-second Timeout, Ban and separately confirmed End timeout/Unban.
Actual-LVGL host tests exercise fresh-contact arming, distinct confirmation
positions, drag/doubletap/held-contact rejection, target stability through Back,
600 arrivals/history eviction and a later selection, permission/session changes,
protected/shared-source identities, success-only Undo and unknown/no-auto-retry.
Those callbacks are fake network transport; these tests send no live action.

The current source pins ESP-IDF 5.3.6 (commit 79e3454c68248bd7d881721c9b2e94561378a8ce)
and Mbed TLS 3.6.7. The [dependency review](dependency-review.md) covers all 26
published ESP-IDF advisories, including the explicit DNS single-address
workaround, plus LVGL/WebSocket and host/font-tool scans. Host requirements are
PySerial 3.5 and Pillow 13.0.0. The exact-image license/input audit exists and must
pass again after the final beta build. Public release and hosted-CI gates remain
separate from these local results.

Authorized live moderation and a 20-point physical touch check now have candidate
device evidence, recorded below. The exact final image must repeat its relevant
checks and complete the one-hour active soak. A Twitch acknowledgement, an
injected contact and a physical finger interaction remain distinct evidence.

Battery, charging, temperature against skin and forearm mounting are
**unqualified**; the agreed hardware-beta scope discloses these limits rather
than representing a finished wearable.

### Preliminary beta warm-scroll performance

An uncommitted development candidate on the reference unit now meets the
numerical warm-scroll target. Its configuration uses 32 KiB/eight-way data cache
with 64-byte lines, asynchronous full-frame transport, row-sprite caching on,
opaque-background culling on and staged transport-copy mode off. The generated
build configuration was checked; these are not results inferred from defaults.

The workload contained 30 synthetic messages of 500 UTF-8 bytes each, covering
ASCII, emoji and mixed text. After cache warm-up, the reader was paused and
received 500 programmatic 12-pixel scroll steps per run. Authenticated TLS stayed
connected throughout.

| Preliminary candidate run | Completed physical frame transfers | Average fps | UI-frame p95 |
| --- | --- | --- | --- |
| 1 | 501 | 30.16 | 30,451 µs |
| 2 | 501 | 30.16 | 30,608 µs |

A shorter A/B measured 28.27 fps with culling off (UI-frame p95 32,377 µs), versus
approximately 30.4 fps with culling on. Sixty-four actual-LVGL host framebuffer
comparisons found culling on/off pixel-identical across the tested clipping,
scrolling, cache/fallback, deletion and overlay cases. Host pixels do not establish
the LCD's optical behavior.

These measurements count completed native panel transfers. They are paused,
warm-cache synthetic scroll results, not a claim of 30 fps during live arrivals,
a physical continuous drag, or finger-to-photon latency. UI-frame timing covers
the UI owner's work; transfer/TE stages overlap it and are reported separately.
The controller latency instrumentation measures a sampled physical Down delivered
to LVGL through completion of a causally subsequent frame transfer; injected
contacts are excluded. The separate physical check below records this metric
under a heavier arrival workload.

This is preliminary candidate evidence, not qualification of the final committed
release image. Repeat the workload against that exact image and retain its
version, source commit and ELF/image hashes before closing the release gate.

### Candidate live moderation and cleanup

The earlier candidate received Twitch acknowledgements for Delete and a
30-second Timeout. A first Undo attempt for that timeout was made after it had
already expired; the UI reported **Failed**, without claiming an acknowledgement
or automatically replaying the action.

On the current 64-byte-cache-line candidate, the following explicit actions were
acknowledged by Twitch in an owned test channel against an authorized disposable
account. Account, channel, user and message identities are redacted publicly.

| Action | Candidate result |
| --- | --- |
| Ban | Twitch acknowledged success |
| Unban | Twitch acknowledged success; test ban cleaned up |
| 60-second Timeout | Twitch acknowledged success |
| End timeout | Twitch acknowledged success; test timeout cleaned up |

For that four-action run, the device reported **4 requests, 4 succeeded and 0
unknown outcomes**. Separate target/effect confirmation screens were reviewed;
restoration required its own confirmation and request. These live API results do
not by themselves establish physical finger confirmation behavior.

Ban/Timeout ClearUser events also exposed a removed-row selection issue; candidate
source includes its fix. Repeat that behavior on the final image. One USB status
read timed out during the 60-second Timeout flow. The next status showed continued
uptime with no reboot and the acknowledged action; no moderation request was
replayed. The host reader now retains bounded partial replies across serial
timeouts; six new fake-wire regressions pass without automatic command retries.
That tooling must still complete final device qualification.

### Candidate physical controls and settings hit area

The clean `57e684be6b03648cf31ecc34021b63aba8e8e5ea` image was checked with
actual finger swipes, Go live without intervening arrivals and long-message Back.
The user reported **“Looks and functions well.”** The interval recorded 19
physical contacts, zero panel-flush errors and zero touch-transport errors. This
was a manual interaction check, separate from injected-pointer replay.

A later candidate keeps the settings cog's visible **52 px** control unchanged
and extends its invisible corner hit region to **72 × 72 px**. Fifteen actual-LVGL
host pointer tests exercise edge/corner hit positions, with drag and held-contact
rejection. Those host contacts are not physical-finger evidence.

In the separate physical settings check, the user reported **“Much better.”**
Telemetry recorded 12 physical contacts and **zero chat-row taps** during that
interval. The touch-error counter was already **3** in the before snapshot at
approximately 24 seconds uptime; at approximately 56 seconds it was still **3**,
with no additional errors or contact cancellation. This is an unchanged counter
during the check, not a claim of zero absolute I2C errors on that boot.

### Candidate USB reply integrity during reconnects

The USB timeout investigation found an SDK `ESP_LOGI` certificate-validation
message interleaved inside a JSON reply at `"ok":true`. Partial-read handling
alone could not repair that malformed device output. The candidate now shares
an output mutex with the SDK log hook and sends each reply in a bounded bulk
transfer with a one-second deadline.

A running-device stress check issued **120 status requests and 3 explicit
disconnect/reconnect requests**. All **123 replies** were valid JSON, with **0
malformed replies, 0 timeouts and 0 `usb_reply_errors`**. Of the 120 status samples,
86 recorded an offline connection state and 34 recorded online; the final sample
was online. No additional I2C errors were observed. No moderation action was
replayed to obtain these results.

The full smoke check of this newer candidate is active. The final native commit,
clean reproducibility/CI checks and genuine one-hour active soak remain pending;
these candidate observations do not substitute for the exact final image's gates.

### Candidate recovery and refresh smoke

The later smoke qualification passed all ten checks after fixing refresh
admission: pause/anchor/Go live, short 20/s and 100/s fixture loads, EventSub and
actual Wi-Fi loss/recovery, and one saved-token rotation. Refresh reported one
attempt, one success, zero failures and restored live chat. The helper submitted
the rotating-token request once and observed its outcome without replay. This
short smoke run does not substitute for the final active one-hour soak.

### Candidate physical touch under arrival load

The user completed 20 actual finger contacts: four cycles through the four corner
targets and centre target. This was the physical controller path, with diagnostic
injection excluded from the latency samples. The user reported **“finished,
looked good.”**

| Physical check | Candidate observation |
| --- | --- |
| Accepted targets | 20 / 20 |
| Missed targets | 0 |
| I2C errors | 0 |
| Touch queue overflows | 0 |
| Maximum target-centre distance, including human aim | 22.36 px |
| Sampled physical Down to subsequent panel-transfer completion, p95 | 158,266 µs |
| Same latency metric, maximum | 169,888 µs |

The hidden reader was still in LIVE following during a synthetic **20 messages/s,
500-byte** background arrival load, with authenticated TLS active. The UI-frame
p95 during this cold-arrival overload was **81,453 µs**. This is not evidence of
30 fps during live arrivals. The target-centre distances include aiming error and
do not constitute an independent sensor calibration.

The latency begins when the controller report is sampled and ends when a
causally subsequent native frame transfer completes. It excludes pre-sample
finger detection and optical scanout, so it is not finger-to-photon timing.
The approximately 158 ms stress p95 remains a measured limitation. The final
diagnostic will pause the hidden reader and must repeat the physical check;
that later result must not replace or relabel this overload observation. Final
exact-image physical qualification and the genuine one-hour active soak remain
pending.

## Historical v0.3.2 and earlier qualification

The following records describe their named earlier revisions and the initial
source-preparation phase. Their numbers and statements about read-only scopes,
SDK versions or repository state are historical; they do not describe the
current beta's capabilities or current repository settings.

### Source publication preparation

The public source candidate now includes the exact hardware purchase link, Apache-2.0 for original code, explicit third-party/font license exceptions, contribution/security policies and CI configuration. Personal account/device defaults and host paths were removed from the public files. Original development records and all private material remain outside Git under ignored `.local/`.

Local publication checks passed: 123 candidate source files passed the public-tree check; Gitleaks 8.30.1 reported no leaks in the source-only export; all 14 Python host-tool tests passed, including the Node suite's 13 mocked provisioning-handler checks; portable core CTest, native font lookup and both real-LVGL composition/touch checks passed. Actionlint 1.7.12 and YAML parsing accepted the prepared GitHub configuration. A clean source export built successfully with ESP-IDF 5.3.5 in network-on and display-only configurations. License texts have recorded hashes and the generated font payloads are unchanged beneath their new attribution headers.

The revised USB helper also identified the running v0.3.2 board and observed its saved login/live connection without resetting it. Publication preparation did not reflash the device. No remote, GitHub push or release was created. Hosted CI remains unrun; the container workflow was linted, not executed locally. The complete binary license inventory and the product gates in [releasing](releasing.md) remain open.

One reference unit ran **Forearm Chat v0.3.2**, a development/pre-release. This record supersedes its earlier preparation-only state: native firmware, display/touch and Twitch login/live reception were exercised. It is a historical qualification record, not a guarantee for every board or a production-readiness claim. Unique device/account identifiers are redacted; the original verified factory recovery image remains private and intact.

### Connected unit and native output

USB enumeration and ROM probing identify Espressif `303a:1001`, ESP32-S3 QFN56 rev 0.2, a 40 MHz crystal, 16 MiB flash and 8 MiB PSRAM. The captured factory app identifies `JC3248W535` and AXS15231B display/touch drivers. Factory boot reported a 478 MB SDSC card; its contents and capacity have not been remeasured.

Native firmware successfully allocates PSRAM for chat, the RGB565 framebuffer, TLS and cached images. This is functional use, not an exhaustive memory stress test or long soak. Checksummed 320 × 480 RGB565 framebuffer captures have been visually inspected for correct rendering and color. Native full-frame panel transfers and the touch sampler run without transport errors in the recorded checks. A framebuffer capture is rendered-output evidence, not a photograph of LCD pixels.

Physical AXS15231B touch communicates at **SDA GPIO4, SCL GPIO8, 7-bit address 0x3B**. Actual controller events have been observed; the BSP mapping is operational on this unit despite the schematic's reversed SDA/SCL net labels. No controller identification-register readback was performed. The exact physical PCB revision and complete four-corner calibration remain distinct from this working mapping.

### Scripted running-device checks: v0.3.0

The private report `.local/hardware-check-030/report.json` reports **passed**. It runs the real ESP32 application and display path, but uses explicit LVGL diagnostic touch injection. Its physical-contact count was zero and injected samples were recorded separately; these results must not be relabeled as physical finger tests.

| Check | Recorded result |
| --- | --- |
| Pause during arrivals | Anchor sequence, pixel offset and scroll position preserved; unread matched arrivals |
| Go live | Twenty single-tap cycles passed, including eighteen cycles with no intervening message |
| Drag gesture | Paused and scrolled back without opening selection |
| Long ASCII | 500 characters / 500 UTF-8 bytes accepted |
| Long emoji | 500 four-byte codepoints / 2,000 UTF-8 bytes accepted |
| Mixed Unicode | Emoji and accented text accepted and rendered in captured output |
| Static Twitch emote | Kappa ID `25`: one actual CDN download, one ready cache entry, one visible image |
| Panel and touch transport | Zero flush errors and zero I2C errors in this run |

The measured v0.3.0 scroll-load baseline was **15.503 presented frames/s**: 170 frames over 10.9656 seconds, UI frame p95 **64,075 µs**, panel-flush p95 **32,322 µs**. These are baseline measurements from that injected-load scenario, not a final v0.3.2 result or a pass of the 30 fps target.

The cache separately reported a 1,242-byte Kappa source and 2,100 decoded RGB565-plus-alpha bytes for its 25 × 28 image. The report's `downloads=1` distinguishes this CDN test from the offline PNG-seed diagnostic.

### Physical interaction and live Twitch

Controller traces exposed explicit Up events interspersed with nonzero-header idle packets. The touch state parser now retains contact through no-data reports, releases on explicit Up, and cancels on repeated transport failure instead of creating phantom clicks. Parser regression tests cover the observed packets. After the fix, the user physically confirmed: **“Long-message Back works now.”** That confirms the specific long-message Back interaction; it does not establish every possible gesture, mounting condition or touch location.

Public Device Code OAuth completed for the authorized test account/channel. The device validated the account, opened EventSub subscriptions, reported authenticated/chat-connected state and received actual chat messages. Saved Wi-Fi configuration and token NVS survived subsequent application flashes. The Mac was used for local setup and browser consent; the ESP32 owns the TLS and Twitch session, with no runtime Mac bridge.

Authorization requests only **`user:read:chat`**. Delete, Timeout and Ban are visibly disabled, and no real moderation was performed. Long-duration refresh/revocation, repeated radio-failure recovery and a one-hour active soak are not established by the initial successful login/reconnect observations.

### Sprite-cache performance: v0.3.2

The controlled comparison in `.local/cache-hardware-032/report.json` used the same three actually received messages, an active authenticated TLS/EventSub connection and repeated six-pixel programmatic scroll steps. Each run presented 150 frames; no Twitch message was sent for this test.

| Row-sprite cache | Run 1 fps | Run 2 fps | UI frame p95, runs 1 / 2 |
| --- | --- | --- | --- |
| Off | 15.3362 | 15.3374 | 63,392 / 63,408 µs |
| On | 15.3907 | 15.3905 | 63,379 / 63,380 µs |

The approximately 0.35% frame-rate difference is negligible in this comparison. Cached rendering reduced text-draw time, but full-frame/TE transfer and scheduling absorbed that saving. Cache-on retained 271,728 additional bytes. Captures compared at the same scroll position had no pixel difference (`pixel_diff_bbox=null`). This is programmatic scrolling of real chat, not a physical-finger latency test or a busy-channel soak.

The shipped v0.3.2 default is therefore **row-sprite cache off**; an explicit diagnostic command retains the experimental option. Static Twitch emote caching is a separate feature and remains enabled. The approximately 15.34 fps cache-off result does **not** meet the 30 fps target.

### Final installed handoff

After the final v0.3.2 flash, `.local/final-native-status.json` confirms the authorized test account/channel, authenticated and chat-connected state, demo off, live following, and row-sprite cache off. Five additional single-tap Go live cycles passed using diagnostic input. Injection was disabled afterward; its cumulative sample counter does not imply an active physical or injected contact.

The snapshot reports zero flush and touch-transport errors, **61,251 bytes free internal memory**, **6,701,792 bytes free PSRAM**, a **24,576-byte largest DMA block**, and **11,648 bytes of Twitch-worker stack headroom**. These are snapshot values, not soak minima. This fresh boot had not received a new chat message at the snapshot; earlier sessions establish real reception, and old chat is intentionally not replayed across boots.

### Build and host validation

The native build pins **ESP-IDF 5.3.5**, **LVGL 8.4.0** and **esp_websocket_client 1.8.0**. The tested app is approximately 1.9 MiB within a 4 MiB slot; exact size depends on revision. Qualification used the SDK's compatible Python environment, independent of the host's unrelated default interpreter.

- Portable core CMake/CTest checks pass with warnings treated as errors: pause/unread/eviction, per-channel duplicate identity, tombstones, stable selection, exact identifiers, UTF-8 validation and full-length messages.
- Font checks use actual LVGL lookup and bitmap functions, including 500 emoji lookups, extended Latin/Greek/Cyrillic/punctuation, fallback order, zero-width selectors and readable replacements.
- Panel host checks cover the real LVGL composition paths, complete-frame staging and persistence, plus the touch Down/Contact/Up/no-data state parser.
- The offline browser concept passed its earlier interaction checks, but remains a separate simulated implementation. Its moderation behavior is not native moderation functionality.
- Eleven selected vendor-reference files remain pinned to a public mirror commit with Git-blob and SHA-256 checks. The manufacturer ZIP returned HTTP403, so byte equality between that ZIP and the mirror is not asserted.

### Preserved factory recovery evidence

Before project installation, all **16,777,216 bytes** were read with esptool's checksummed transport and independently compared with a raw device-computed MD5. Factory reboot changed only 2,002 NVS bytes at 0xb000–0xc47d. That block was reread with the app stopped, after which the complete raw device digest matched the saved image. Its SHA-256, original partitions and recovery notes are in `.local/hardware-2026-11-10/manifest.json`; the initial capture is retained too.

The capture phase issued no flash write/erase. Later authorized installation and app updates did write project firmware and initialize its configuration partitions. The factory app is now a recovery image, not the installed app. Normal later NVS changes do not invalidate that snapshot. No eFuse programming, SD formatting or SD-file operation was performed. The microSD card is excluded from the flash backup.

### Remaining qualification

The first native version supports portrait only. Full motion/input timing, repeated physical gestures, radio recovery, long-run allocation stability, battery/charging current, thermal behavior, runtime and forearm enclosure ergonomics remain bounded by the evidence above. Monochrome emoji has known sequence-shaping limits, and static emotes use a bounded per-boot cache with text fallback. The design's performance and wearable-use requirements remain acceptance targets wherever no measurement is recorded.

This historical qualification concerns one reference device. Unique device/account identities and original local development context are retained in private records, outside the public source release.
