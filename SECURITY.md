# Security policy

Forearm Chat is hardware-beta software. There is no supported stable release or promised response SLA yet. Security fixes target the current development branch; release support policy must be set before the first stable release.

## Reporting

After this repository is published and private vulnerability reporting is enabled, use **Security → Report a vulnerability** on GitHub. Do not include vulnerabilities, credentials, flash images or private chat in a public issue. If private reporting is unavailable, open a minimal issue requesting a private contact method without disclosing the vulnerability or any sensitive data.

Include the affected commit/version, board model, reproducible steps, impact and a redacted log if useful. Do not send Wi-Fi credentials, access/refresh tokens, Twitch passwords, Client Secrets, complete device status dumps with identifiers, or factory/user flash backups.

## Current boundaries

- The saved channel profile controls fresh login scope requests. Read-only profiles request only `user:read:chat`; permission-enabled profiles also request message deletion, ban/timeout management and moderated-channel role lookup scopes. A read-only profile independently blocks mutations in the UI, portable policy and network worker even when a saved token has broader grants. Selecting it retains that token pair and does not revoke the earlier Twitch grant. Existing chat-only grants cannot moderate without fresh consent and role verification. Action gates check exact target/source identity, session generation, granted scopes, current channel role and protected targets. Every action and Undo needs separate confirmation; no action is queued offline or automatically retried after an unknown outcome.
- Wi-Fi credentials and OAuth tokens live in unencrypted device flash. A separate token partition does not prevent physical extraction. No secure-boot or encryption eFuses are configured by this project.
- USB diagnostics are a trusted physical administration interface. An attached host can configure the device, request login/logout and capture the framebuffer. They are not exposed over Wi-Fi.
- The provisioning form binds to loopback and requires its session nonce and origin/Host checks. Stop it after use; do not forward its port to another computer.
- Twitch API, EventSub and asset requests use certificate-verified TLS after time synchronization. Keep dependency versions and the certificate bundle reviewed as part of releases.

If a credential is exposed, revoke/rotate it through the relevant provider. Removing a file or rewriting Git history does not invalidate an exposed token. Device `logout` removes local tokens; Twitch account settings control revoking the application's grant.

## Dependency review

The beta pins ESP-IDF 5.3.6 / Mbed TLS 3.6.7, LVGL 8.4.0 and WebSocket client 1.8.0.
[The dated advisory review](docs/dependency-review.md) records vendor findings,
feature applicability, host/font-tool scans and remaining qualifications. The
SDK's multi-address DNS overflow is mitigated by a pinned single-address cache
and a fatal build guard; changing that configuration requires a new review.
Passing scans or a vendor feature exclusion is not a guarantee of security.

Hardware-beta status does not qualify battery charging, contact temperature,
strap/mount integrity or use against skin. Those limits are distinct from
software dependency and license audits.
