# Changelog

## Unreleased

The first public release candidate is **0.4.0-beta.1**, a hardware beta. Publication remains subject to the exact-image checks in [release gates](docs/releasing.md).

- Native 320 × 480 LVGL chat for the JC3248W535 / ESP32-S3 N16R8 display.
- Floating settings/pause controls, anchored scrollback, unread counts and explicit Go live.
- Standalone Wi-Fi, Public Device Code Twitch login, persisted authorization and EventSub chat reception.
- Bounded 512-message history, full-length Unicode messages, monochrome emoji and static Twitch emotes.
- Correct explicit touch release/no-data handling and deferred navigation from selected messages.
- Guarded message deletion, 30/60/600-second timeouts, bans and separately confirmed End timeout/Unban, with frozen targets and no automatic mutation replay.
- Asynchronous complete-frame transport, cached chat rows and conservative backdrop culling. Preliminary warm-scroll tests reached 30.16 completed panel frames/s; final-image qualification is recorded separately.
- ESP-IDF 5.3.6 / Mbed TLS 3.6.7, bounded network recovery, saved-token refresh diagnostics and an active hardware-soak tool.
- Portable provisioning tools, source privacy checks, CI configuration and open-source license notices.

Known limits include slower cold rendering under continuous full-length message stress, incomplete complex-emoji shaping, static emotes only and RAM-only history. Battery, charging, contact temperature and forearm mounting are unqualified. See [verification](docs/verification.md) for measured conditions; this release does not qualify the device for skin-contact wear.
