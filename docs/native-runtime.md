# Native runtime

Forearm Chat 0.5.0-dev is development firmware for the ESP32-S3 display. This document includes the new size control, startup animation and richer media pipeline. The latest qualified binary remains 0.4.0-beta.1. A release's attached qualification summary and notes identify the exact tested source/image and results. [Verification](verification.md) preserves historical revisions, including the rejected `54c1169c` soak; those records do not qualify another build. Host passes do not establish device results. Battery/power/thermal/mount qualification remains separate.

## Reading and controls

The 320 × 480 portrait screen gives chat the full panel, with floating settings and pause/Go live controls. Main text defaults to 24 px; visible circles occupy 52 px. The settings circle has a transparent 72 × 72 px corner target above chat rows and rejects a dragged contact. Settings show the channel, authorized account, profile and connection state, and offer the phone-assisted Twitch login flow.

The Text size slider previews nine native text families live: 67/75/80/90/100/115/125/133/150%, corresponding to 16/18/20/22/24/28/30/32/36 px body text. Author, control and emote geometry responds while controls retain minimum touch areas; long messages and authors reflow. Paused history keeps its message anchor, and an open message selection is invalidated when typography changes. The setting is saved on slider release in a separate NVS namespace; Wi-Fi/login/profile data is preserved. Settings can scroll when its preview grows.

Boot displays a nonblocking 2.2-second vector animation with a violet/teal signal resolving into a chat mark. Networking starts independently. Touch dismisses the overlay without selecting the chat beneath it; Settings has a Preview startup button. Automatic synthetic demo messages are off; demo/fixture workloads remain explicit diagnostics.

Live following stays at the newest retained message. Touching or dragging chat pauses following so content cannot move beneath a deliberate selection. Reception continues, unread increases and the viewport remains anchored by message sequence and pixel offset. Go live deliberately returns to the tail. Layout correction and new arrivals are not intended to reactivate pause after that action.

Dragging cancels tap selection for that contact. A clean tap opens a snapshot identified by channel, message and author IDs instead of a recycled row index. The selection screen enables Delete, Timeout and Ban only for verified permissions and channel roles. Unknown source identity, shared-chat sources, protected targets and stale sessions disable actions; a removed message disables Delete. Timeout presets are 30, 60 and 600 seconds. Each action has a separate target/effect confirmation at a different position, consumes the preceding contact and waits 450 ms before arming. Dragging, held contacts and repeated taps cannot bypass the fresh confirmation.

The touch-parser regression checks cover explicit release and nonzero-header no-data reports observed on this controller. After that fix, the user physically confirmed long-message Back. That is evidence for the specific interaction; broader gesture and location testing remains separate. A UI injection or host parser pass does not itself verify a finger gesture.

## Moderation results

Read-only is a saved channel-profile setting, independent of the token's granted
scopes. The UI, portable policy and network worker deny Delete, Timeout, Ban and
restoration while it is active, including when a saved token has broader grants.
Settings show READ ONLY and hide Enable moderation. Profile/channel changes
invalidate old confirmations and restoration targets.

A frozen target contains exact channel/message/user/source IDs and the account
session generation. It survives scrolling, history eviction, Back and a later
selection. The network layer repeats authorization/role/target checks before
sending. One request can be pending at a time, even if Back returns to chat.
No empty-message-ID room clearing, arbitrary duration, offline queue or automatic
mutation retry is exposed.

Pending means submitted, not successful. The result distinguishes Twitch
acknowledgement, permission denial, rate limiting, known failure and unknown
outcome. After an unknown outcome, check Twitch before making any new deliberate
attempt. Successful timeout/ban can offer End timeout/Unban for that same frozen
target, with a new request ID, fresh permission checks and a separate confirmation.
A failed, pending or unknown action does not offer Undo. Deleting a message cannot
be undone. Twitch remains authoritative if a role or protected target changes.

## History and Unicode

The PSRAM store holds 512 messages. Its 2,049-byte owned text field accommodates Twitch's 500-character maximum even when each codepoint uses four UTF-8 bytes. Stable IDs are rejected rather than truncated; text truncates only at a valid UTF-8 boundary. Selection is copied separately so buffer reuse cannot retarget it.

Each record separately owns up to 100 UTF-8 bytes of display name and the exact
ASCII user login of at most 25 characters. At 100%, supported Unicode names wrap at 24 px,
ASCII names use 20 px, and the author column is 224 px. Typography and column geometry
respond to the slider while staying clear of the settings target.
Names are not shortened. If a display name needs an unavailable glyph, the row and
selection use its full login fallback. Network normalization also uses that
login when the supplied display name is empty or exceeds the bound; it never
shortens a name into a partial identity. Moderation still binds the exact user
ID rather than either displayed name.

History is local RAM, not server history: messages arrive after reception begins, old messages are evicted at the bound and reboot clears them. Retention expiry is visible. Twitch does not replay an EventSub outage; the app reports a connection gap. Deletion/clear events update retained rows while paused.

The compiled families have accented Latin, Greek, Cyrillic, punctuation, currency and arrow fallbacks, plus 1,380 monochrome Noto Emoji glyphs. Unsupported characters receive a readable replacement. Joiners, variation selectors and emoji tag controls are zero width. These fonts remain codepoint-based fallbacks; the separate color-image matcher below combines supported sequences. This is not complete Unicode-script shaping. Font provenance and licenses are in [the font component](../firmware/components/forearm_fonts/README.md).

## Twitch, 7TV and color emoji

Up to eight image occurrences share a per-message budget, each with its own UTF-8 byte offset and length. Native Twitch fragments have priority: the parser checks that fragment text concatenates to the original message. Remaining positions can match 7TV global names or names from the configured channel's catalog, on word boundaries. A channel alias overrides a global alias. Other channels' aliases are not applied. The UI independently verifies each range, literal token and non-overlap before replacing it. Repeated IDs can occupy separate positions.

Color emoji use a pinned Twemoji 17.0.2 PNG-key allowlist. Supported joined families, skin tones, country flags and keycaps form one inline image; unsupported or malformed clusters stay literal. An explicit text-presentation selector is respected. Artwork is fetched from a fixed HTTPS CDN path pinned to an immutable upstream commit. [Provenance and licensing](media-provenance.json) records the source; Settings includes attribution.

Original message text remains stored. Missing/invalid positions, occurrences beyond the bound, allocation/cache limits and unavailable assets retain readable trigger text. Images compact their matching tokens when ready, so affected rows are remeasured while preserving the paused message anchor; there is no separate emote strip. A shrinking anchor row clamps its offset to the remaining row.

A lower-priority worker uses verified HTTPS to fixed Twitch, 7TV and pinned Twemoji CDN paths with validated identifiers. Arbitrary asset URLs and redirects are rejected; Twitch credentials are never sent to 7TV or the emoji CDN. Source PNGs are limited to 32 KiB and inspected before decode; dimensions must not exceed 128 × 128. Decompression/metadata are bounded. RGB565-plus-alpha output is downsampled to at most 48 px high / 128 px wide, preserving aspect ratio, and lives in PSRAM. Inline layout scales images with the selected typography and preserves wide emotes.

Sixty-four immutable entries per boot share a 512 KiB decoded-pixel budget. Ready pointers are never overwritten or evicted while LVGL or display transfers may reference them. Downloads yield to authorization and queued chat, retry with bounds/backoff and start no faster than once per second. Full caches and download failures retain text fallbacks. Animated 7TV sources use static poster PNGs; animation and other third-party providers are not implemented.

The public 7TV global and configured-channel catalogs are fetched after Twitch resolves the selected broadcaster ID. Streaming JSON parsing avoids a complete in-memory document. Catalog storage is bounded to 512 aliases and a 4 MiB response ceiling; statistics expose fetch failures and truncation. Failed catalogs preserve text and native Twitch/emoji operation. Catalogs and media are RAM caches and reload after reboot.

The diagnostic PNG-seed path uses the same decoder without a CDN request and must be labeled as an offline fixture. The actual CDN path has separately downloaded and displayed Kappa, ID `25`.

## Account and connection lifecycle

Wi-Fi station mode supports ordinary networks and phone hotspots. The local USB setup form writes SSID/password, Public Twitch Client ID and channel to NVS. The Mac helper is not a network bridge; it is unnecessary after setup. Time synchronization precedes certificate-verified Twitch requests.

Public Device Code OAuth displays Twitch's validated Public activation link/QR and code for browser approval; there is no client secret. Read-only profiles request only `user:read:chat` on future login. Moderation-enabled profiles request that scope plus `moderator:manage:chat_messages`, `moderator:manage:banned_users` and `user:read:moderated_channels`. A narrow saved grant still receives chat; Enable moderation is available only in a moderation-enabled profile and requires fresh consent and role verification. Access/refresh tokens are delivered directly to the device, not to the Mac setup form.

The trusted USB `watch` command changes the saved channel and read-only flag while the worker retains Wi-Fi settings, Client ID and tokens. It removes action permission immediately, saves/read-backs the profile and resolves the new destination before reconnecting. The legacy 196-byte configuration decodes without losing credentials; new writes include the explicit profile byte. A saved read-only profile survives restart. Existing broader Twitch grants are retained, not revoked; the client enforces the viewing-only boundary and skips moderation-role lookups. Same-room reconnects preserve history; an actual room change clears the prior room's history and selection. The command reply confirms queue admission; status confirms the resulting channel/profile and connection.

Authorization is validated at startup and hourly. Refreshes are serialized, with the new token pair saved and read back before use. A persistent refresh-pending marker requires fresh login after an interrupted or ambiguous refresh, instead of reusing a potentially consumed one-time token. Saved authorization has survived an application firmware update on this unit. Revocation, expiry and storage errors produce explicit login state.

Without a configured channel, the authorized user's channel is the default. Helix resolves the destination; EventSub subscribes to messages, deletion, per-user clear and room-clear events. Those EventSub subscriptions are receive-only; separate confirmed Helix mutations implement moderation. Bounded queues and counters expose malformed messages and overflow instead of growing memory indefinitely.

An ordinary disconnect reconnects with backoff and new subscriptions, displaying a gap. A Twitch-directed handoff retains the old socket until the replacement welcome and transfers subscriptions. The library answers server pings automatically. Its client-ping interval is placed beyond the application's bounded connection lifetime so this client does not send Twitch application messages.

Credentials and tokens are stored in unencrypted flash and are extractable with physical access. No secure-boot or flash-encryption eFuse was changed. Local `logout` removes saved tokens; it does not revoke the app grant in the Twitch account's browser settings.

## Ownership, diagnostics and limits

One UI owner controls LVGL. Networking publishes copied events through a PSRAM-backed queue with an internal FreeRTOS control block. Physical touch is sampled independently. Two full PSRAM composition buffers permit logical dirty drawing to overlap ordered complete native LCD transfers; neither source nor internal DMA staging storage is reused while its consumer owns it. Row-sprite caching and opaque-background culling are enabled by default, with 32 KiB instruction cache and a 32 KiB/eight-way data cache with 64-byte lines. Uncached or uncertain coverage falls back to ordinary drawing. The row cache is separate from the static Twitch-emote cache.

The 512 layout records use one lifetime PSRAM allocation: 12 KiB on the Xtensa
target, whose record size differs from the host test ABI. Visible-row bookkeeping
and cache metadata stay in internal memory. Allocation failure prevents normal chat
initialization rather than substituting an unbounded allocation.

Body sprites are bounded to 32 entries and 2 MiB. A separate 16-entry author
cache has a 256 KiB total and 64 KiB per-entry cap, keyed by exact rendered name,
font and dimensions. Visible entries stay pinned. Allocation and sprite building
happen outside draw callbacks; a miss, allocation failure or unsupported pressed
style falls back to ordinary LVGL drawing. These are memory and ownership bounds,
not a measured performance result. USB diagnostics expose separate author-cache
enablement and counters.

An earlier uncommitted candidate sustained 30.16 completed frame transfers/s in paused warm-scroll runs of synthetic 500-byte messages with TLS connected. Source `54c1169c` has separate historical warm-scroll results and a failed soak. Neither result establishes 30 fps for live arrivals, physical dragging or another image. Revision-specific results are in [verification](verification.md) and release-attached qualification artifacts; the historical v0.3.2 cache-off default does not describe this implementation.

Twitch, WebSocket and asset tasks have separate stack budgets and headroom telemetry. This board configuration puts TLS allocations in PSRAM. Status exposes Wi-Fi/time/auth/subscription/profile state, received/dropped events, touch packets and queued edges, UI/render/sync/transfer timing, heap/DMA headroom and cache counts. Render and transport stages overlap; adding their times does not give end-to-end frame time. Physical input latency links a sampled controller Down delivered to LVGL with a subsequent composition and its completed transfer, excluding injection. It is not a finger-to-photon measurement. A release records which physical checks apply to its image; earlier checks do not qualify changed transport/UI code. Status excludes passwords and OAuth tokens.

`tools/device.py` provides status, pause, live, scroll, login, logout and screenshot commands over native USB without resetting the board. Automatic selection requires exactly one Espressif `303a:1001` device; otherwise, supply the global `--port` option before the action. Captures freeze a framebuffer and check each RGB565 row. They show rendered firmware output, not a camera observation of the LCD. Diagnostic touch injection has separate counters and must not be presented as physical interaction evidence.

The original factory flash backup remains verified and private. The microSD card is outside that image and is not modified by the app. Battery current, charging, heat against skin, runtime and final enclosure/strap ergonomics remain separate physical qualification work. Host tests do not establish final scrolling speed or battery life.

Touch-lock mode is future work. Hardware-beta use does not qualify forearm
mounting, battery/charging safety, skin-contact temperature or outdoor readability.
Dependency findings and the explicit DNS workaround are tracked in
[the advisory review](dependency-review.md).
