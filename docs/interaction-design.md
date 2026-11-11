# Forearm chat interaction design

This guide describes the implemented 0.4.0-beta.1 hardware-beta interaction and broader wearable acceptance targets. The reference unit is an ESP32-S3 with 16 MB flash, 8 MB PSRAM and native 320×480, 3.5-inch AXS15231B capacitive-touch IPS display. The vendor's 49.0×73.4 mm active area gives approximately 166 ppi. Native touch communication confirms SDA4/SCL8/0x3B on that unit; complete physical calibration and the exact PCB revision remain separate. A release's attached qualification summary identifies its tested commit/image and measured coverage. [Verification](verification.md) preserves historical results; this design is not a blanket claim that every target has passed.

The native reader operates independently over Wi-Fi, including a phone hotspot, using runtime-supplied account/channel settings and a Public Client ID. USB provides provisioning and flashing; the user's browser approves Twitch authorization. Ongoing chat and token handling run on device. Saved read-only profiles, positioned inline emotes, full sender identities and guarded moderation in permission-enabled profiles are implemented. Historical source `54c1169c` completed five acknowledged API actions with cleanup, three green hosted-CI runs and a byte-identical four-image rebuild, but failed its active soak. Those historical results do not qualify another image.

## Reading at a glance

The implemented default is full-panel **320×480 portrait** chat, with **no persistent header**. Floating 52 px settings and pause circles preserve reading space; the settings circle has an invisible 72 × 72 px corner target above chat. The paused state offers an explicit Go live pill and unread count. Channel/account/network/profile details live in settings. Avoid animated backgrounds, decorative gradients behind text and permanent moderation controls on every row.

Sender names retain up to 100 UTF-8 bytes and use wrapped supported Unicode. When required glyphs are unavailable, use the sender's separately stored exact ASCII login. ASCII names use 20 px text; supported Unicode uses 24 px. The author column wraps within 224 px, clear of the settings target. Static emotes replace verified fragment occurrences inline; identical literal text elsewhere stays literal. Missing positions, unready images and occurrences beyond the eight-per-message bound retain their text. Asset arrival may compact a row, with the paused anchor preserved during reflow.

Use a near-black background, bright neutral text, restrained violet accents, and a clear warm accent for paused or disconnected states. Distinguish state with words and icons as well as color. Usernames have one consistent accent, with a small moderator/broadcaster badge when present; text readability takes priority over the sender's requested color. Mentions and replies receive a subtle marker without moving other rows. Incoming messages appear without a fade. Animate only scrolling and short state transitions.

Choose typography from the physical panel rather than a desktop screenshot. Calculate `px_per_mm = active_pixels / active_mm` on each axis; alternatively, `ppi = sqrt(width_px² + height_px²) / diagonal_inches`. At approximately 166 ppi, a 24 px main font em occupies about 3.7 mm, and a 52 px effective touch target occupies about 8 mm. Start with 24 px text, about 31 px line spacing, targets of at least 52 px, and roughly 10 px separation between distinct action targets. The actual glyph height depends on the font and must be inspected on the panel. Offer 22 px and 26 px alternatives if physical testing supports them. Message hit regions need at least 52 px height even when a message occupies one text line.

Portrait is implemented and is the current default. A later mounted-device comparison may evaluate 480×320 landscape for wider lines; landscape and runtime rotation are not currently native features. Preserve the requested floating controls and do not shrink text merely to fit more messages.

Fit controls by reducing their number, not by shrinking their targets. Offer two readable text sizes and a quick brightness control. Reserve compact secondary text for timestamps or status, never for essential message content. Confirm readability at the streamer's ordinary arm position and expected lighting before choosing a default. Rotation and left/right-arm layouts should preserve the same physical target sizes.

## Live following, pause and scrollback

| Event | Required behavior |
| --- | --- |
| New chat while LIVE | Follow the tail with a short, bounded transition. Coalesce arrivals during bursts; never animate once per incoming message. |
| Touch down in chat | Freeze following immediately, before rows can move beneath the finger. |
| Drag | Scroll history with bounded inertia. Any recognized drag cancels tap selection for that entire contact. |
| Clean tap on a message | Keep the viewport paused and open that exact message's action sheet. |
| New chat while PAUSED | Keep the same message ID and pixel offset anchored. Increment the unread count; do not reposition the viewport. |
| Reach the bottom by scrolling | Remain paused until the user deliberately chooses LIVE. |
| Tap `Go live · 23 new` | Close selection, clear unread state and return to the tail. If far behind, jump directly instead of animating through the entire backlog. |
| Message deleted while visible | Replace its contents with a same-height deletion placeholder until the viewport changes. Selection and neighboring rows must not jump. |
| Retention evicts the anchor | Move to the oldest retained message once, show `Older messages expired`, and preserve PAUSED. Never silently resume live following. |

Unread means messages received since entering pause, not a claim that the streamer has read everything else. Entering pause with zero arrivals still shows PAUSED. Connection gaps are represented by a visible history marker; locally captured history must not imply that missed Twitch messages were recovered.

Keep the receive path active while paused. A lost connection leaves readable cached history on screen with an explicit disconnected indicator. Returning connectivity must not disturb the paused anchor. Reconnect status and token reauthorization must remain distinguishable.

## Touch and moderation

Physical touch, pause, scrolling and stable selection operate on the reference unit. Physical Back/settings checks and live API tests remain revision-specific evidence. Changes to I2C handling, task placement or layout require the corresponding device checks on the identified image. Host pointer tests cover guarded confirmation/Undo and read-only denial without live sends. Touch-lock mode remains a future mounted-device requirement.

A read-only channel profile overrides existing token grants in both UI and worker. It hides permission-upgrade controls and disables Delete, Timeout, Ban and restoration. Future login requests only chat-read scope. Switching channel/profile invalidates confirmations and restoration targets; a room change clears the previous room's history, while a same-room reconnect preserves it. Earlier broader server grants are retained rather than revoked by this client setting.

Use a small gesture state machine: idle → contact → tap candidate or scrolling → released. Convert distances to physical units after touch calibration. Start with about 2 mm of movement to recognize a drag; once crossed, that contact cannot become a tap. A clean tap requires release inside the original message hit region with no intervening layout change. Reject a long stationary contact as a tap after a configurable threshold; no hold gesture executes moderation. Tune these thresholds using the mounted device, not the Mac pointer.

The action sheet freezes the selected target as `{channel_id, message_id, user_id, display_name}`. Never derive a moderation target from a visible row index or a recycled UI object. Show the sender, message excerpt and destination channel. New chat, message deletion or list recycling cannot change that target. Cancel, BACK and an optional hardware button return to the same paused anchor.

The first sheet offers Delete message, Timeout, Ban, and Cancel. Timeout presents a few large duration presets, initially 30 s, 1 min and 10 min. Every destructive action has a distinct confirmation step naming the user and the exact effect: `Delete this message?`, `Timeout name for 1 minute?`, or `Ban name from #channel?`. Do not combine choosing a timeout duration with executing it. The final confirmation control must not occupy the same hit area that opened the confirmation. Ignore the previous contact until it has fully released, preventing a double tap from crossing both steps.

Only expose enabled moderation actions when authorization and the channel role allow them. Explain disabled actions briefly. If a selected message was deleted, disable Delete; do not redirect it to a newer message. Ban/timeout operate on the selected user, and their confirmation must make that difference clear. Protected targets and permissions can change: the service's response remains authoritative.

After confirmation, show a pending state and accept at most one request for that action. Display success only after the service acknowledges it. Show permission, expired-login, rate-limit and network failures distinctly. An ambiguous timeout is `Outcome unknown`; do not automatically replay the request. A subsequent explicit retry must require a fresh confirmation after reconciliation. No moderation requests may be queued offline for later execution.

Sweat, sleeve contact and accidental touches are normal usage conditions. Provide a visible touch-lock mode with an explicit unlock sequence; a hardware unlock is preferable if the board exposes a suitable button. No edge swipe, long press, wake touch or unlock gesture should execute moderation. Validate this with the final strap and enclosure.

## Rendering and memory

The UI owns all display objects. Networking and parsing run outside the UI task and publish bounded immutable events through a bounded queue. The UI consumes them within a per-frame budget and may limit composition to dirty regions in its framebuffer. The board adapter uses two 307,200-byte PSRAM composition buffers and sends ordered complete native frames while ownership protects in-flight data. Arbitrary partial LCD rectangles remain unqualified, so budget physical transfers as full frames. Defaults select 32 KiB instruction cache and 32 KiB data cache with 64-byte data-cache lines. Bounded row-sprite caching and proven-opaque background culling retain ordinary drawing when cached or opaque coverage is uncertain.

Virtualize the message list: keep UI objects for the visible region plus a small overscan area, not the complete history. Layout each message once for the current width/font and reuse its measured height. A rotation or font change reflows history while restoring the same anchor ID. Resolve tap hit testing against the layout version observed at contact down.

The 512-record layout array is allocated once for the UI lifetime in PSRAM,
occupying 12 KiB on Xtensa. Host record sizes are not the target's memory cost.
Visible-row and cache metadata remain internal for the active rendering path.

Body sprites have 32 entries within a 2 MiB budget. Author sprites use a separate 16-entry least-recently-used cache, capped at 256 KiB total and 64 KiB per entry. Keys include the exact rendered name, font and dimensions. Visible entries stay pinned, and cache allocation happens outside draw callbacks. A miss, allocation failure or unsupported pressed style uses normal LVGL drawing. These are implementation bounds, not a frame-rate claim.

The running store is fixed at 512 messages, with a 2,049-byte text field sufficient for 500 four-byte codepoints, complete 100-byte sender names, separate ASCII logins and exact stable IDs. Up to eight positioned emote occurrences share sixteen non-evicting assets per boot with a 512 KiB decoded-pixel ceiling and text fallback. These bounded allocations live alongside the frame, TLS and fonts in the 8 MiB system; further budgeting must use measured headroom. Pin at most the selected record separately; a paused viewport must not pin unbounded history.

Use bounded input lengths and valid UTF-8 truncation with an explicit ellipsis. Keep essential font glyphs available without network access. Render unavailable glyphs or emotes as readable fallback text. Emote downloads and decoding cannot block touch or scrolling. Begin with static, size-limited emotes; animated emotes are a later enhancement subject to a measured frame and memory budget. Never evict a glyph or bitmap while a pending display transfer references it.

Deduplicate transport deliveries by event/message identity. Preserve the received message order instead of reordering a paused viewport by timestamps. Apply deletion/ban events by stable IDs and document the chosen handling of a banned user's retained messages. Queue overflow must surface a gap/drop indicator and a diagnostic count; silently losing chat is unacceptable. Respect service rate-limit headers and bounded reconnect backoff without blocking rendering.

## Acceptance targets for the first hardware prototype

The table below records broader product acceptance targets. Release notes must state the exact workload for any measured result: warm-cache scrolling does not establish live-arrival speed or physical-drag latency. Caching and background culling are implementation choices whose effect must be measured on the identified image. Historical warm-scroll, injected and physical checks remain separate in [verification](verification.md).

| Area | Pass criterion |
| --- | --- |
| Touch response | Visual feedback within 100 ms at the 95th percentile, including during incoming-chat bursts. |
| Scroll rendering | At least 30 presented frames/s during a continuous drag; 95th-percentile UI frame time at or below 33 ms. Record the display transfer time separately. |
| Pause stability | Over 1,000 new arrivals, retain the same anchor and pixel offset unless retention expires it; show the correct unread count. |
| Gesture separation | Across deliberate taps, drags, flicks, long contacts and canceled contacts, no scroll contact opens moderation and no first tap executes an action. |
| Target identity | While rows recycle and incoming/deletion events arrive, every confirmed request contains exactly the IDs displayed in its confirmation. |
| Load | Sustain 20 messages/s for ten minutes and a synthetic 100 messages/s burst for five seconds without resets, growing memory usage or stalled touch. Report any queue drops explicitly. |
| Moderation failure | Exercise 401, 403, 429, revoked role, disconnect and ambiguous response. No false success, duplicated request or delayed offline execution. |
| Connectivity | Pause, disconnect, reconnect and reauthorize without losing the retained viewport; visibly mark any unrecovered interval. Repeat with phone-hotspot loss/recovery and with the Mac powered off. |
| Physical use | Check both arms, chosen rotations, brightness settings and text sizes with the mounted enclosure. Essential controls remain reachable and chat readable at the user's normal viewing distance. |

Hardware backup, native reader/replay and authenticated Twitch operation are implemented. Keep historical API results separate from release-image physical, dense-arrival/performance, active-soak and CI results. A viewing-only channel is never used for mutation testing. Power, thermal and enclosure validation remain future wearable work.
