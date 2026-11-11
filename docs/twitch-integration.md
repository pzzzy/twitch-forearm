# Twitch integration

This guide describes the 0.4.0-beta.1 hardware-beta integration: Public Device Code login, saved authorization, live EventSub, saved read-only profiles, positioned inline emotes and complete sender identities. New owners supply their own Public Client ID and channel. A release's attached qualification summary identifies the tested commit/image and coverage. [Verification](verification.md) preserves historical evidence, including source `54c1169c` with five acknowledged API actions and cleanup but a failed active soak; those results do not qualify another image.

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

The saved profile controls consent and action availability. Read-only profiles request only `user:read:chat` on future login, hide Enable moderation and deny all mutations in the UI, core policy and worker even when a previously granted token has broader scopes. Switching to read-only retains that token pair; it does not revoke the earlier Twitch grant or require reauthorization merely to change rooms. Moderation-role lookup is skipped in this mode.

Moderation-enabled profiles request all four implemented scopes: reception, deletion, banned-user management and moderated-channel lookup. An existing `user:read:chat` grant can continue reading, with actions disabled until fresh consent and role verification; Settings offers Enable moderation only in this profile. A user-token WebSocket client does not need cloud-bot scopes `user:bot` or `channel:bot`. The setup form requires an explicit channel; the network layer can fall back to the authorized account's channel for an empty stored value. Reading another channel does not imply permission to moderate it. Actions are gated on granted scopes, profile, fresh account/channel generation, online state and verified broadcaster/moderator role. The network layer rechecks these before sending and still handles authoritative API rejection.

The trusted USB `watch` command queues only the requested channel and profile flag. Saved Wi-Fi/client/token state remains worker-owned. Permission removal and session invalidation happen immediately; the worker persists the profile and resolves the exact new broadcaster ID. Check status for the completed channel switch. Reconnecting to the same channel preserves RAM history; changing channels clears the previous room and selection.

Source: [Scope reference](https://dev.twitch.tv/docs/authentication/scopes/).

## Chat and scrollback

Subscribe to `channel.chat.message`, `channel.chat.message_delete`, `channel.chat.clear_user_messages`, and `channel.chat.clear`. Add `channel.chat.notification` for subscriptions and similar chat notices when the basic reader is stable. Conditions identify both the broadcaster and the authorized reading user.

The bounded ring stores exact channel/message/author IDs, a 100-byte UTF-8 display name, a separate 25-character ASCII login, received time, text and deletion state. Unsupported display-name glyphs select the exact login fallback; names are not truncated into partial identities. Source identity, moderation badges and emote metadata are retained separately. Shared Chat source identity stays explicit so a moderation tap cannot silently target the wrong channel.

For inline emotes, validate the complete fragment-text concatenation against `message.text`, then retain up to eight actual occurrences with UTF-8 byte offset/length. The UI validates each non-overlapping span against its literal token before an available static image replaces it. Other identical tokens stay literal. Missing/mismatched metadata, unavailable assets and overflow keep original text; no global trigger-name replacement is performed.

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

## Integration validation

1. Confirm display/touch controllers, orientation, PSRAM, backlight, and power hardware.
2. Exercise pause, scrollback, row selection, clear events, and overload using synthetic fixtures.
3. Build networking with certificate verification, bounded parsing, reconnect/gap behavior, and credential storage.
4. Verify the configured Public Client ID, browser authorization, saved profile and read-only live chat on the identified image. Record token/radio/soak results separately from initial connection.
5. Exercise native moderation policy, exact-target UI, separate confirmation/Undo, read-only denial and unknown-outcome behavior with fake network callbacks. Identify the source of any live API evidence and completed cleanup; do not represent historical acknowledgements as actions repeated on another image. A read-only watched channel is never a target for mutation testing.

Unresolved physical requirements include battery capacity, charging protection, enclosure/strap ergonomics, heat, and usable outdoor brightness. Establish the runtime target after measuring actual current draw.

The firmware baseline and vendor-advisory applicability are in
[dependency review](dependency-review.md). This hardware beta does not qualify
battery/charging, skin-contact temperature or forearm mounting.
