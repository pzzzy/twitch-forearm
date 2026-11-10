# Hardware-beta release and publication gates

The agreed first release is **0.4.0-beta.1, a hardware beta**. It includes native
chat and guarded moderation, with explicit limitations. It is not a claim of a
finished forearm wearable. The `pzzzy/twitch-forearm` repository is held private
for CI qualification; public visibility and a downloadable pre-release follow
successful validation. The exact [purchase listing](https://www.aliexpress.us/item/3256807380017698.html)
and qualified board variant are documented in the README and hardware reference.

## Hardware-beta gates

- [x] Native portrait display, touch sampling, pause, scrollback and explicit Go live operate on the reference unit; historical revision-specific results are preserved.
- [x] Public Twitch login, saved authorization and live EventSub work directly over Wi-Fi.
- [x] Full-length Unicode and a real static Twitch emote have running-device evidence.
- [x] Guarded Delete, 30/60/600-second Timeout, Ban and separately confirmed End timeout/Unban are implemented. Policy and actual-LVGL host tests cover immutable targets, permissions, contact isolation, pending and unknown/no-retry behavior without live sends.
- [ ] Record actual beta Delete, Timeout, Ban and Undo results in an explicitly authorized owned channel with disposable targets. Confirm the intended effect and clean up successful test timeouts/bans. Distinguish API acknowledgement from input injection and physical finger evidence.
- [ ] Record the final beta's physical confirmation/Back/Go live checks and synthetic burst/long-message replay, including memory, drops, frame/input timing and failures. Keep injected and physical contacts separate.
- [ ] Measure at least 30 completed panel frames/s during sustained scrolling, with UI-frame p95 at most 33 ms on the chosen release configuration.
- [ ] Pass a genuine one-hour active soak with repeated load/input pulses, Wi-Fi/EventSub loss and recovery, saved-token refresh, bounded retention, memory minima and zero resets/transport errors.
- [ ] Publish final beta measurements and open limitations for the exact released revision. The 30 fps goal must remain labeled as a target wherever it has not been measured.
- [x] Battery, charging, contact temperature, outdoor readability and mounting are explicitly **unqualified**. This disclosed beta scope does not require claiming those future wearable tests have passed.

The beta does not need to satisfy every finished-wearable target below, but it
must pass its software/safety/distribution gates and state the remaining limits.
Evidence is in [verification](verification.md); only evidence from the relevant
revision can update a checkbox.

A preliminary uncommitted candidate has completed two warm-scroll runs at
**30.16 completed panel frames/s**, with UI-frame p95 **30.451 / 30.608 ms**.
This was paused, cached synthetic chat with authenticated TLS connected. It is
not a claim for live arrivals or physical input latency. The exact-release
performance gate remains open until that committed image repeats the workload.
Live Delete and Timeout have Twitch acknowledgements; Ban/Undo cleanup, final
physical checks and the one-hour active soak remain pending.

## Finished-wearable targets still open

- Sustained scrolling at least 30 fps, frame-time p95 at most 33 ms, and measured finger-to-feedback p95 under TLS/burst load.
- Longer field use and varied hotspot conditions beyond the beta's instrumented one-hour active soak.
- All-corner/gesture qualification and a usable touch-lock strategy for mounted use.
- Battery connector/polarity, protection, charging, current draw, runtime, thermal behavior against skin, mount/strap ergonomics and strain relief.

Historical v0.3.2 measured about 15.34 fps; that is not a measurement of the beta's
new transport. Battery runtime and readiness for skin-contact wear are not
supported claims. These are future product qualifications, not passed beta tests.

## Source and binary distribution gates

- [x] Exact purchase URL, board evidence, Apache-2.0 original-code license, font/vendor exceptions and full notices are prepared.
- [x] Personal account/device defaults are removed from public source. Private development material, NVS, backups and chat captures remain outside Git.
- [x] Initial source-only checks and local builds/tests have passed. Repeat Gitleaks, public-tree checks and manual history/export review after the final changes and before publication.
- [x] [Dependency advisory review](dependency-review.md) covers the pinned ESP-IDF 5.3.6 / Mbed TLS 3.6.7, vendor feature applicability, DNS workaround and host/font-tool results. Preserve the build guard and recheck the final configuration.
- [x] The exact-input [firmware license audit](../LICENSES/firmware-5.3.6/README.md) and full source/notices bundle are implemented, including runtime exceptions and MPL CA source.
- [ ] Fresh network-on and display-only builds from the reviewed clean commit pass, with version/source metadata matching the release tag.
- [ ] Hosted CI passes on the intended default branch, with its actual result reviewed. Local linting is not a hosted run.
- [ ] The final binary audit returns `complete:true`, with both application and bootloader hashes matching the packaged images and no unreviewed/stale inputs.
- [ ] The release package passes independent manifest, offsets/partition, checksums, metadata, privacy and recovery checks; a downloaded copy is verified. Use the [install/update guide](installing.md).
- [ ] Final release notes state beta scope, actual measurements, known limits, authorization changes and first-install/update/recovery requirements.

Never upload this machine's `.local/`, user/factory flash dumps, NVS, tokens,
credential-bearing configuration, complete status recordings or real chat.
Build distributable firmware from the reviewed source commit with no personal
configuration. Source archives contain only committed source; firmware packages
also carry checksums, offsets, dependency versions, NOTICE and applicable full
license/source files. A license report is valid only for its exact image hashes.

## Publication sequence

1. Review and commit the complete source candidate and history locally. Run host checks, advisory/configuration review, Gitleaks and public-tree checks. Keep development on separate branches and use `main` as the release default.
2. Push only reviewed history to the existing private repository for hosted CI. Review and fix failures while the repository is still private.
3. Verify least-privilege Actions settings and applicable branch protection. Enable private vulnerability reporting, secret/push protection and dependency alerts where available; do not claim a setting is active before checking it.
4. Finish the beta hardware and distribution gates, build/package the clean reviewed commit and verify the exact artifacts. CI does not substitute for device behavior or licensing evidence.
5. Make the reviewed repository public after green CI and release validation. Create the matching version tag and mark the GitHub release as a pre-release with the hardware-beta limits. Attach only reviewed packages/checksums, then verify the downloaded package and hardware link from the public repository.
