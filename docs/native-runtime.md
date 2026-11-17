# Native runtime

Forearm Chat 0.6.0-beta.1 hardware-beta source includes animated emotes, compact colored sender headers, size controls, startup graphics, saved-profile display and session recovery for the ESP32-S3 display. A release's attached qualification summary and notes identify the exact tested source/image and results; this runtime description does not establish a hardware pass. [Verification](verification.md) preserves dated development snapshots, including the rejected `54c1169c` soak; those records do not qualify another build. Host passes do not establish device results. Battery/power/thermal/mount qualification remains separate.

## Reading and controls

The 320 × 480 portrait screen gives chat the full panel, with floating pause/Go live controls at top left and Settings opposite at top right. Main text defaults to 24 px; visible circles occupy 52 px. The settings circle has a transparent 72 × 72 px corner target above chat rows and rejects a dragged contact. Settings show the configured channel and read-only profile before authentication, alongside the current account and connection or reconnect reason. An unconfigured device shows Not configured rather than an invented channel. Account loss removes stale account information and disables moderation without discarding the saved room or its local history. Settings refreshes wait for an active pointer contact to finish rather than replacing controls beneath a finger.

The Text size slider previews nine native text families live: 67/75/80/90/100/115/125/133/150%, corresponding to 16/18/20/22/24/28/30/32/36 px body text. Author, control and emote geometry responds while controls retain minimum touch areas; long messages and authors reflow. Paused history keeps its message anchor, and an open message selection is invalidated when typography changes. The setting is saved on slider release in a separate NVS namespace; Wi-Fi/login/profile data is preserved. Settings can scroll when its preview grows.

Boot displays a nonblocking 2.2-second vector animation with a violet/teal signal resolving into a centered chat mark. The startup canvas is entirely graphical, with no wordmark, tagline or status text. Networking starts independently. Touch dismisses the overlay without selecting the chat beneath it; Settings has a Preview startup button. Automatic synthetic demo messages are off; demo/fixture workloads remain explicit diagnostics.

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
ASCII user login of at most 25 characters. At 100%, supported Unicode names use 24 px,
ASCII names use 20 px, and the author column is 224 px. Typography and column geometry
respond to the slider while staying clear of the settings target.
Stored identities and selected-message names are not shortened. Chat headers use explicit ellipses when necessary. If a display name needs an unavailable glyph, the row and
selection use its full login fallback. Network normalization also uses that
login when the supplied display name is empty or exceeds the bound; it never
shortens a name into a partial identity. Moderation still binds the exact user
ID rather than either displayed name.

History is local RAM, not server history: messages arrive after reception begins, old messages are evicted at the bound and reboot clears them. Retention expiry is visible. Twitch does not replay an EventSub outage; the app reports a connection gap. Deletion/clear events update retained rows while paused.

The compiled families have accented Latin, Greek, Cyrillic, punctuation, currency and arrow fallbacks, plus 1,380 monochrome Noto Emoji glyphs. Unsupported characters receive a readable replacement. Joiners, variation selectors and emoji tag controls are zero width. These fonts remain codepoint-based fallbacks; the separate color-image matcher below combines supported sequences. This is not complete Unicode-script shaping. Font provenance and licenses are in [the font component](../firmware/components/forearm_fonts/README.md).

## Twitch, 7TV and color emoji

Up to eight image occurrences share a per-message budget, each with its own UTF-8 byte offset and length. Native Twitch fragments have priority: the parser checks that fragment text concatenates to the original message. Remaining positions can match 7TV global names or names from the configured channel's catalog, on word boundaries. A channel alias overrides a global alias. Other channels' aliases are not applied. The UI independently verifies each range, literal token and non-overlap before replacing it. Repeated IDs can occupy separate positions.

Color emoji use a pinned Twemoji 17.0.2 PNG-key allowlist. Supported joined families, skin tones, country flags and keycaps form one inline image; unsupported or malformed clusters stay literal. An explicit text-presentation selector is respected. Artwork is fetched from a fixed HTTPS CDN path pinned to an immutable upstream commit. [Provenance and licensing](media-provenance.json) records the source; Settings includes attribution.

Original message text remains stored. Verified known occurrences reserve a zero-ink inline slot immediately, including while downloading or retrying; their trigger words do not flash before an image appears. Unknown aliases, invalid positions, occurrences beyond the bound and failed metadata allocation remain literal. The first asset fetch can leave a blank slot; there is no promise of immediate graphics on a cold network. The UI remeasures when asset dimensions become known, preserving the paused message anchor and clamping an offset when its row shrinks. There is no separate emote strip.

A lower-priority worker uses verified HTTPS to fixed Twitch, 7TV and pinned Twemoji CDN paths with validated identifiers. Arbitrary asset URLs and redirects are rejected; Twitch credentials are never sent to 7TV or the emoji CDN. Source PNGs are limited to 32 KiB and GIFs to 128 KiB; dimensions must not exceed 128 × 128. The original GIF parser bounds every read, dictionary expansion, frame count and work budget, and supports local palettes, interlace, transparency and disposal modes 1–3. Decompression/metadata are bounded. RGB565-plus-alpha output lives in PSRAM, preserving aspect ratio. Static images are downsampled to at most 48 px high / 128 px wide; animations to at most 32 px high / 128 px wide. Inline layout scales images with the selected typography and preserves wide emotes.

Sixty-four reusable slots share a 512 KiB decoded-pixel budget. Visible image objects acquire leases; they detach their LVGL source before releasing the lease, and the last release invalidates the decoder before a slot becomes evictable. Ready descriptors and pixels remain immutable while leased. Least-recently-used unpinned entries can be retired, so using 64 distinct assets does not exhaust the cache for the rest of the boot. If all candidates are pinned or an allocation fails, a recognized occurrence keeps its reserved slot and can retry later. Successful acquisition is required before attaching a graphic; a readiness snapshot never permits an unleased descriptor.

Downloads yield to authorization and queued chat and start no faster than once per second. The UI renews only visible asset demand approximately once per second. Failed assets can retry with 30/120/300-second backoff while demand remains recent; provider rate-limit backoff is also respected. Cache-generation changes restore newly ready images without another chat arrival. Twitch default-format assets and animated 7TV channel/global assets can supply GIFs. Up to 512 source frames / 30 seconds are grouped into at most 32 cached frames while retaining the full cycle duration; each animation uses at most 96 KiB of pixels within the shared 512 KiB budget. Frames are immutable under the same row lease, and the UI advances only existing image sources at up to 20 Hz without remeasuring text. Oversized, unsupported or unaffordable animations try a static poster, respecting rate-limit backoff. Twemoji remains static; other third-party providers are not implemented.

The public 7TV global and configured-channel catalogs are fetched after Twitch resolves the selected broadcaster ID. Streaming JSON parsing avoids a complete in-memory document. Catalog storage is bounded to 512 aliases and a 4 MiB response ceiling; statistics expose fetch failures and truncation. A successful snapshot is refreshed after about 15 minutes. Transient failures retain the previous validated global aliases and same-channel aliases, with bounded retry; another room's aliases never carry over. Native Twitch and emoji matching remain available independently. Catalog changes revisit at most one retained message per UI tick while preserving native Twitch spans and paused anchors; rows already displayed keep their original interpretation. Late events collected under an earlier catalog are revisited too. Only visible rows request image downloads, so history enrichment does not fill the asset queue with all 512 messages. Catalogs and media remain RAM caches and reload after reboot. Initial chat connection allows up to eight seconds after account validation for the matching channel catalog to load (global readiness alone is insufficient), then proceeds even if the provider is unavailable. Once a row has been displayed, later catalog changes preserve its original text/image interpretation: words already seen never pop into graphics. New and not-yet-displayed rows can use a late catalog. Known image slots can still fill, retry and animate normally. Unknown aliases in already displayed rows remain literal.

The diagnostic image-seed path accepts bounded PNG/GIF fixtures using the same decoder without a CDN request and must be labeled as an offline fixture. The actual CDN path has separately downloaded and displayed Kappa, ID `25`.

Username headers occupy a single line. Original native vector tiles show a red broadcaster camera, green moderator sword, magenta VIP diamond, violet subscriber/founder star, blue Prime crown and other role symbols. Up to three icons are displayed in role-priority order; a teal plus represents overflow, and other supplied channel badge sets use a gold medal symbol. These are original role symbols rather than downloaded official channel artwork. Icons are noninteractive; the message remains the touch target. Immutable icon atlases are prepared once per native size in PSRAM, using at most 50 KiB across sizes, and blit without cache allocations during drawing. Opaque sprites are used only on the proven ink background; pressed, masked or layered cases retain native vector rendering. Username/body sprites are prepared after row geometry settles so canvas strides match the final label widths.

The EventSub username color is validated and dim colors are lifted for the dark background; absent/invalid colors use a stable readable palette. The author bitmap cache keys include font, foreground and available label width, so colored names remain compatible with cached scrolling. Typography controls scale icons and text together. Names that exceed the remaining line width are shortened at a UTF-8 boundary with an explicit ellipsis. Tapping a message preserves the full supported name/login in its heading, with complete role/subscription-month/channel badge captions beside the channel in the detail line. Display icons/colors never affect verified moderation authority.


## Account and connection lifecycle

Wi-Fi station mode supports ordinary networks and phone hotspots. The local USB setup form writes SSID/password, Public Twitch Client ID and channel to NVS. The Mac helper is not a network bridge; it is unnecessary after setup. Time synchronization precedes certificate-verified Twitch requests.

Public Device Code OAuth displays Twitch's validated Public activation link/QR and code for browser approval; there is no client secret. Read-only profiles request only `user:read:chat` on future login. Moderation-enabled profiles request that scope plus `moderator:manage:chat_messages`, `moderator:manage:banned_users` and `user:read:moderated_channels`. A narrow saved grant still receives chat; Enable moderation is available only in a moderation-enabled profile and requires fresh consent and role verification. Access/refresh tokens are delivered directly to the device, not to the Mac setup form.

The trusted USB `watch` command changes the saved channel and read-only flag while the worker retains Wi-Fi settings, Client ID and tokens. It removes action permission immediately, saves/read-backs the profile and resolves the new destination before reconnecting. The legacy 196-byte configuration decodes without losing credentials; new writes include the explicit profile byte. A saved read-only profile survives restart. Existing broader Twitch grants are retained, not revoked; the client enforces the viewing-only boundary and skips moderation-role lookups. Same-room reconnects preserve history; an actual room change clears the prior room's history and selection. The command reply confirms queue admission; status confirms the resulting channel/profile and connection.

Authorization is validated at startup and hourly. A versioned, checksummed NVS journal stores the token pair, generation and Ready/RefreshPending/SignedOut phase in one blob. Refresh is serialized: a pending phase is committed before sending a refresh, and the replacement pair is committed and read back before use. A durable authority marker prevents fallback to older credentials after journal migration, even if the journal later disappears. Logout writes a signed-out tombstone before removing legacy keys. The checksum detects record corruption; it does not encrypt or authenticate stored secrets against physical access.

The loader accepts the deployed legacy pair only when no journal has become authoritative and the old pending marker permits reuse. Expired access credentials can still load for validation and refresh. Interrupted, corrupt, mismatched-app, missing migrated or unavailable storage produces a specific reconnect reason rather than silently replaying potentially consumed credentials. An ambiguous refresh also requires fresh consent. Settings retains the configured channel/read-only profile throughout this state and offers Connect Twitch; erasing configuration is not a recovery requirement. These contracts have host coverage; reboot, update and recovery results for a downloadable image belong to its release evidence.

Without a configured channel, the authorized user's channel is the default. Helix resolves the destination; EventSub subscribes to messages, deletion, per-user clear and room-clear events. Those EventSub subscriptions are receive-only; separate confirmed Helix mutations implement moderation. Bounded queues and counters expose malformed messages and overflow instead of growing memory indefinitely.

An ordinary disconnect reconnects with backoff and new subscriptions, displaying a gap. A Twitch-directed handoff retains the old socket until the replacement welcome and transfers subscriptions. The library answers server pings automatically. Its client-ping interval is placed beyond the application's bounded connection lifetime so this client does not send Twitch application messages.

Credentials and tokens are stored in unencrypted flash and are extractable with physical access. No secure-boot or flash-encryption eFuse was changed. Local `logout` removes saved tokens; it does not revoke the app grant in the Twitch account's browser settings.

## Ownership, diagnostics and limits

One UI owner controls LVGL. Networking publishes copied events through a PSRAM-backed queue with an internal FreeRTOS control block. Physical touch is sampled independently. Two full PSRAM composition buffers permit logical dirty drawing to overlap ordered complete native LCD transfers; neither source nor internal DMA staging storage is reused while its consumer owns it. Row-sprite caching and opaque-background culling are enabled by default, with 32 KiB instruction cache and a 32 KiB/eight-way data cache with 64-byte lines. Uncached or uncertain coverage falls back to ordinary drawing. The row cache is separate from the leased media-image cache.

The 512 layout records use one lifetime PSRAM allocation: 16 KiB for the current
32-byte Xtensa record, including its catalog-generation tag. The host ABI can
have a different record size. Visible-row bookkeeping
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
