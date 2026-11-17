# Hardware-beta release validation

Forearm Chat releases remain **hardware betas**, with native chat and guarded moderation. The 0.6.0-beta.1 source adds animated Twitch/7TV media and compact colored sender headers to text sizing, startup graphics and profile/session recovery. Exact-image qualification outcomes belong to the matching release notes and summary. No release qualifies a finished forearm wearable. Match the [purchase reference](https://www.aliexpress.us/item/3256807380017698.html) and [board variant](hardware-reference.md) before using an image.

This document defines the validation method, not a live completion dashboard. A release's notes and attached `qualification-summary.json` identify the tested source commit, image hashes, workloads, results and limitations. The package manifest, license audit and qualification summary must refer to the same released images. [Verification](verification.md) is a dated historical record; passing another revision's checks does not qualify a new binary.

## Freeze and evidence identity

Commit the complete candidate before its final clean build and device qualification. Record the source commit, SDK/configuration and application/bootloader hashes. Tag that tested commit. Attach final measurements and notes to the release without changing the source identity of the qualified binary. If later documentation is committed separately, make its link to the tested release explicit; do not rebuild the new commit and present it as the already tested image.

Keep raw status, real chat, credentials and per-device identifiers private. Export only reviewed, allowlisted measurements. Label historical API tests with their actual source/image; explain which changed paths have host or device regression coverage instead of claiming the old requests were repeated on the new image.

## Hardware-beta validation

Record the result and evidence source for each item:

- Native portrait display, physical touch sampling, pause, scrollback and explicit Go live, including settings/Back targets and drag-versus-tap separation.
- Public Device Code login, saved authorization, profile persistence and live EventSub directly over Wi-Fi. On cold boot and after an app-only update, verify the saved channel/read-only profile appears before authentication and remains correct offline or during reconnect. Check normal saved-session reuse, expired-access refresh, radio recovery and explicit reconnect reasons without exposing credentials. Cover legacy-token compatibility and journal pending/corrupt/missing-authoritative states with fake storage/transport tests; never reuse an ambiguous refresh token to manufacture a pass.
- Full-length Unicode text, complete stored/tapped sender-name/login fallback and explicitly ellipsized single-line headers and positioned inline emotes. Verify missing or invalid positions and occurrences beyond the eight-image bound retain safe text fallback. Recognized pending assets reserve inline space rather than flashing their trigger words; a late catalog preserves words already displayed.
- All nine text/UI sizes, live slider preview, saved-size restart, readable wrapping, wide inline images and paused-anchor reflow. Check physical Settings, message → Back and single-tap Go live at useful size extremes. Startup/Preview startup must consume its dismissal contact without selecting the underlay or changing account/profile state.
- Media churn beyond 64 distinct assets within the 512 KiB pixel budget: unpinned entries retire, visible leases remain valid, decoder references detach before reuse, and lease-error counters stay zero. Check unavailable-asset retry without another chat arrival, transient catalog retention, room isolation, and paced retroactive history enrichment of unseen rows including events delivered after a catalog scan completes. Already displayed text keeps its original interpretation. Host races must retain the reserved slot when readiness is lost before acquisition, and attach graphics only under a valid lease. Capture the actual running-device churn/recovery result separately from host coverage.
- Read-only denial in UI, portable policy and worker, including a previously broad saved grant. A watched read-only channel must receive **no mutation test requests**.
- Exact-target moderation and separate confirmation/Undo, protected and shared-source targets, stale session, pending, denied, rate-limited and unknown outcomes. Use fake callbacks for failure paths. Any real action needs an explicitly authorized disposable target; record API acknowledgement separately from its observed effect and complete cleanup. Never replay an ambiguous mutation automatically.
- Synthetic 20 messages/s for ten minutes and 100 messages/s for five seconds, with scrolling/input pulses, bounded retention, memory headroom and explicit generated/drop accounting. Synthetic fixtures and injected input are labeled and kept separate from actual chat and physical contacts.
- Two sustained warm-scroll runs at least 30 completed panel frames/s, with UI-frame p95 at most 33 ms on the selected release configuration. State the frame count, message workload, cache state and TLS state. This gate does not establish 30 fps during live 500-byte arrivals or physical dragging.
- Dense short-message scrolling must also meet 30 completed panel frames/s and UI-frame p95 at most 33 ms in both sustained programmatic and held-pointer drags. Report short separate gestures with their release/idle intervals separately; they are not the sustained-motion denominator.
- A genuine one-hour active soak with repeated bounded load/input pulses, Wi-Fi/EventSub recovery, saved-token refresh and sampled heap/DMA/PSRAM minima. Require zero resets and additional display/touch transport errors; investigate a failure and repeat on the corrected image. A shorter recovery stress run does not replace the hour.
- Final physical interaction evidence and sampled input-to-completed-frame measurements, with method and workload. Sampled controller-to-frame time is not finger-to-photon latency. Identify any physical evidence reused from unchanged interaction code rather than claiming it was measured again on another binary.

The beta discloses battery, charging, skin-contact temperature, runtime, outdoor readability and mounting as **unqualified**. Use USB power for initial testing. These future wearable qualifications cannot be inferred from a software soak.

## Historical snapshot — November 10, 2026

Source `54c1169c` completed five acknowledged live API actions with cleanup, three green jobs in its hosted-CI workflow and a byte-identical four-image rebuild. It also passed its two warm-scroll measurements, but its active soak **failed at about 50 minutes with one touch-transport error**. It was rejected for release qualification. Later I2C changes had their own failed intermediate stress runs; a subsequent Core1 sampler variant passed 30 radio-recovery cycles. That stress result is not a final-image one-hour pass. The [historical verification record](verification.md) preserves exact numbers and scope. Release attachments provide the outcome for the source actually shipped.

## Source and binary distribution validation

- Review the exact board reference, original-code license, font/vendor exceptions, full notices and contribution/security guidance.
- Remove personal defaults, credentials, private chat and per-device identity from source and history. Run Gitleaks, public-tree checks and manual export/history review after the final changes.
- Build network-on and display-only configurations from the reviewed clean commit. Run portable core, host tools, font, panel, actual-LVGL and license tests. Review the actual hosted-CI result; local linting is not a hosted run.
- Review [dependency advisory applicability](dependency-review.md) for the pinned SDK/configuration and host/font tools. Preserve the fatal DNS configuration guard; a changed dependency or feature needs a new review.
- Run the [exact-input firmware license audit](../LICENSES/firmware-5.3.6/README.md) after the final build. Require `complete:true`, reviewed inputs and matching application/bootloader hashes. Include applicable full license/source files, runtime exceptions and MPL certificate source.
- Independently check package manifest, flash offsets/partitions, checksums, source/version metadata, privacy and recovery instructions. Follow the [install/update guide](installing.md). Download the release copy and verify it again.
- State actual workload-specific measurements, evidence identity, authorization/profile behavior and known limitations in the release notes. Keep broader product targets distinct from passed beta gates.

Never upload `.local/`, user/factory flash dumps, NVS, tokens, credential-bearing configuration, complete status recordings or real chat. Build distributable firmware without personal configuration. A license report is valid only for its exact image hashes.

## Publication procedure

1. Review and commit the complete source candidate and history. Run host checks, advisory/configuration review, Gitleaks and public-tree checks. Keep development on separate branches and use `main` as the release default.
2. Push only reviewed history for hosted CI and review the results. Before initial public visibility, resolve failures while the repository is private.
3. Verify least-privilege Actions settings and applicable branch protection. Enable private vulnerability reporting, secret/push protection and dependency alerts where available; do not claim a setting is active before checking it.
4. Complete the exact-source hardware and distribution validation, then package the matching images. CI does not substitute for device behavior or licensing evidence.
5. Publish the reviewed repository and matching version tag. Mark the release as a pre-release with hardware-beta limits. Attach reviewed packages/checksums, qualification summary and notes, then verify the public download and hardware link.

## Further wearable targets

Measure physical drag and finger-to-feedback under busy live arrivals, longer field/hotspot use, both-arm ergonomics, touch locking, outdoor readability, battery connector/polarity and protection, charging, current draw, runtime, temperature against skin, mount integrity and strain relief. A bounded USB-powered beta test does not establish these results.
