# Twitch integration

Updated 2026-11-10 for the 0.4.0-beta.1 hardware-beta development candidate. Public Device Code login, saved authorization and live EventSub have device evidence. Guarded moderation and confirmed End timeout/Unban are implemented and host-tested; final real-action and physical confirmation qualification is pending. New owners supply their own Public Client ID and channel. Historical v0.3.2 was read only. See [verification](verification.md) for revision-specific evidence and open performance, soak and power limits.

## Architecture decision

Use a native ESP32-S3 display interface with direct TLS connections to Twitch: EventSub WebSocket for incoming chat and Helix HTTPS for moderation. The connected chip is an ESP32-S3 with N16R8 hardware; native operation is a reasonable starting point, subject to display, memory, and network profiling. Rendering and touch handling must stay responsive while networking reconnects or parses a burst.

A Mac companion may help with provisioning, diagnostics, fixtures, and replay testing. It should not be a runtime requirement for the eventual wearable. A phone hotspot can provide Wi-Fi. Moving Twitch networking into a companion is a fallback if measured memory or power requirements justify it.

Twitch recommends EventSub plus its API over its older IRC interface. WebSocket transport is appropriate for a client running on the end user's device and uses a user access token. This is an installed client acting as the authorized streamer, rather than a cloud bot with a separate bot account.

Sources: [Chat overview](https://dev.twitch.tv/docs/chat/), [Client authentication and transport](https://dev.twitch.tv/docs/chat/authenticating/).

## Account login

Register a **Public** OAuth application and use **Device Code Grant**. The device requests authorization, displays the returned verification URL as a QR code plus a readable user code, and the streamer authorizes it in a phone or computer browser. The wearable never asks for a Twitch password and contains no application client secret.

Honor the returned polling interval and expiry. Show pending, denied, expired, disconnected, and completed states. Accept only Twitch's HTTPS `www.twitch.tv/activate` origin/path, preserve the returned code/query and ensure the Public activation-route flag `public=true`. Reject conflicting/malformed flags or another origin; never replace the returned code or accept arbitrary activation URLs. Store access and refresh tokens privately; exclude them from source, logs, diagnostics, and exported fixtures.

Public-client device-flow refresh tokens rotate after use. Persist the new token pair atomically. The device-flow documentation describes a 30-day inactive refresh-token expiry, so support reauthorization after extended inactivity. The generic refresh page contains older authorization-code-only wording; the specific device-flow section explicitly documents refresh-token exchange for public clients.

Source: [Device Code Grant](https://dev.twitch.tv/docs/authentication/getting-tokens-oauth/#device-code-grant-flow).

## Permissions

| Feature | OAuth scope |
| --- | --- |
| Read chat and receive chat deletion/clear events | `user:read:chat` |
| Ban, timeout, and unban a user | `moderator:manage:banned_users` |
| Delete a message | `moderator:manage:chat_messages` |
| Optional future send/reply capability | `user:write:chat` |
| Verify a non-broadcaster account moderates the selected channel | `user:read:moderated_channels` |

New beta consent requests all four implemented scopes: chat reception, message deletion, banned-user management and moderated-channel lookup. An existing `user:read:chat` grant can continue reading, with actions disabled until fresh consent and role verification. Settings offers Enable moderation. A user-token WebSocket client does not need cloud-bot scopes `user:bot` or `channel:bot`. The setup form requires an explicit channel; the network layer can fall back to the authorized account's channel for an empty stored value. Reading another channel does not imply permission to moderate it. Actions are gated on granted scopes, fresh account/channel generation, online state and verified broadcaster/moderator role. The network layer rechecks these before sending and still handles authoritative API rejection.

Source: [Scope reference](https://dev.twitch.tv/docs/authentication/scopes/).

## Chat and scrollback

Subscribe to `channel.chat.message`, `channel.chat.message_delete`, `channel.chat.clear_user_messages`, and `channel.chat.clear`. Add `channel.chat.notification` for subscriptions and similar chat notices when the basic reader is stable. Conditions identify both the broadcaster and the authorized reading user.

Normalize messages into a bounded in-memory ring buffer. Retain stable message ID, author ID/login/display name, timestamp, name color, badges, text/emote fragments, channel and source-channel identity, and deletion state. Shared Chat source identity must remain explicit so a moderation tap cannot silently target the wrong channel.

Scrollback covers retained messages received by this client. Twitch does not replay events missed during a network outage. Show a gap marker after a disconnection; do not invent recovered history. Apply deletion and clear events to stored rows even while the viewport is paused. Pause stops following the live edge while reception continues. Pin the selected message by ID when opening moderation tools so new arrivals cannot change its target.

Sources: [Subscription types](https://dev.twitch.tv/docs/eventsub/eventsub-subscription-types/), [Messages, fragments, and emotes](https://dev.twitch.tv/docs/chat/send-receive-messages/).

## Native moderation contract

| Action | Helix request |
| --- | --- |
| Delete one message | `DELETE /helix/moderation/chat` with a nonempty `message_id` |
| Timeout | `POST /helix/moderation/bans` with target `user_id` and integer `duration` |
| Permanent ban | Same POST, omitting `duration` |
| Unban/end timeout | `DELETE /helix/moderation/bans` |

Use the authenticated user's ID as `moderator_id` and the intended channel as `broadcaster_id`. Allowed timeouts range from 1 to 1,209,600 seconds. Deletion has a six-hour age limit and protected broadcaster/moderator messages cannot be deleted.

**Reject an empty message ID locally.** Twitch interprets omission as clearing the entire chat room. Do not expose room clearing in the initial action sheet.

A tap opens an action sheet with frozen exact author/channel/message/source identities and a quoted message. Delete, Timeout 30/60/600 seconds and Ban each receive a separate confirmation. Final confirmation is placed away from the initial action and requires a fresh contact after 450 ms. A drag or repeated initial tap cannot send an action. Show pending until Twitch acknowledges success. On a network timeout after submitting a mutation, the outcome is unknown: do not blindly retry or report failure as certain. Show Outcome unknown and ask the owner to check Twitch before any new deliberate action. There is no one-tap retry or offline replay. Pending, permission denial, rate limit and known failure remain distinct. One pending request locks additional actions even after Back or a later selection. Successful timeout/ban can expose a separately confirmed End timeout/Unban bound to the acknowledged target, with a new request ID and fresh permissions; unsuccessful or unknown outcomes cannot expose Undo. Do not retain moderation commands for replay after reconnection.

Sources: [Ban/timeout API](https://dev.twitch.tv/docs/api/reference/#ban-user), [Delete API](https://dev.twitch.tv/docs/api/reference/#delete-chat-messages).

## Session lifecycle

Validate the token at startup and hourly. End sessions whose authorization is revoked. On 401, invalidate moderation permission and serialize refresh work, persisting rotated credentials before resuming reads; if refresh fails, return to login. A submitted moderation mutation is never replayed after 401, refresh, timeout, 5xx or another ambiguous transport result. Never allow parallel refresh attempts to consume the same rotating token.

Sources: [Required token validation](https://dev.twitch.tv/docs/authentication/validate-tokens/), [Refresh handling](https://dev.twitch.tv/docs/authentication/refresh-tokens/).

Use one EventSub connection. Create subscriptions after welcome within the server's allowed deadline (ten seconds by default), track negotiated keepalive, and answer WebSocket pings. For `session_reconnect`, use the provided URL unchanged, retain the old socket until the new welcome, then close the old connection; subscriptions transfer. For actual network loss, reconnect with bounded jittered backoff and recreate subscriptions. There is no replay for the missed interval.

Source: [WebSocket lifecycle](https://dev.twitch.tv/docs/eventsub/handling-websocket-events/).

Deduplicate notifications using the EventSub metadata message ID; Twitch may redeliver an event. Keep that identity separate from the chat message ID used for deletion and row selection.

Source: [Duplicate event handling](https://dev.twitch.tv/docs/eventsub/#handling-duplicate-events).

## Implementation gates

1. Confirm display/touch controllers, orientation, PSRAM, backlight, and power hardware.
2. Exercise pause, scrollback, row selection, clear events, and overload using synthetic fixtures.
3. Build networking with certificate verification, bounded parsing, reconnect/gap behavior, and credential storage.
4. The registered Public Client ID, browser authorization and read-only live chat gate have passed for the supplied account/channel. Longer token/radio/soak qualification remains separate.
5. Native moderation policy, exact-target UI, separate confirmation/Undo and unknown-outcome behavior pass host checks using fake network callbacks. Real Delete, Timeout, Ban and Undo validation remains a beta gate in an owned channel with explicitly authorized disposable targets; record only sanitized results.

Unresolved physical requirements include battery capacity, charging protection, enclosure/strap ergonomics, heat, and usable outdoor brightness. Establish the runtime target after measuring actual current draw.

The firmware baseline and vendor-advisory applicability are in
[dependency review](dependency-review.md). This hardware beta does not qualify
battery/charging, skin-contact temperature or forearm mounting.
