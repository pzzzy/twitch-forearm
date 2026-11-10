# Dependency and advisory review

Reviewed **2026-11-10** for the **0.4.0-beta.1 hardware beta** on ESP32-S3.
This is an applicability assessment of known vendor advisories and the selected
configuration, not a claim that the software is free of vulnerabilities.
Repeat it before releases and after dependency, compiler, protocol or decoder
changes. A successful package scan does not cover native firmware dependencies.

## Reviewed versions and configuration

- ESP-IDF **5.3.6**, commit `79e3454c68248bd7d881721c9b2e94561378a8ce`,
  with its pinned Mbed TLS **3.6.7**; LVGL **8.4.0** and
  `espressif/esp_websocket_client` **1.8.0** are component-lock pinned.
- Target compiler: Espressif Xtensa GCC **13.2.0**, ordinary `-O2` optimization.
- Wi-Fi station/client operation; Bluetooth, SoftAP and the DHCP server are off.
  The app initializes no HTTP server, BLE provisioning/protocomm, ESP-TEE,
  OpenThread Border Router, ESP-NOW, SmartConfig or WPS service. Default SDK
  support macros alone do not mean a service is exposed; final link maps and
  application initialization were also reviewed.
- Verified HTTPS/WSS client connections use TLS 1.2 and the SDK CA bundle after
  time synchronization. TLS 1.3 and DTLS are not enabled in this build. USB
  administration and the host loopback setup service have separate trust bounds.
- PNG assets use LVGL's bundled LodePNG 20201017, a fixed Twitch HTTPS origin,
  exact validated IDs, no redirects, at most **32 KiB** compressed source and
  **128 × 128** dimensions checked before decode. Inflate output is capped at
  **133,120 bytes**; text/unknown chunks are not retained and text/ICC metadata
  limits are 4,096 bytes. Decoded-pixel cache budget is 512 KiB. These bounds reduce
  exposure; they are not a proof of decoder correctness.

These facts were checked against `firmware/sdkconfig.defaults`, the generated
beta `sdkconfig.h`, `main/CMakeLists.txt`, actual source initialization and link
maps. The final binary license/input audit must still pass for each package.

## ESP-IDF vendor advisories

All **26** entries currently published in the
[official ESP-IDF advisory list](https://github.com/espressif/esp-idf/security/advisories)
were reviewed against their affected versions and feature prerequisites.
The assessment column is this project's inference from those prerequisites
and its configuration. A disabled feature can become applicable after a change.

The relevant unresolved-version advisory is
[GHSA-ch63-jxm5-vmvf](https://github.com/espressif/esp-idf/security/advisories/GHSA-ch63-jxm5-vmvf):
5.3.6 contains the multi-address DNS bug. The vendor's stated workaround is one
cached address per hostname. `CONFIG_LWIP_DNS_MAX_HOST_IP=1` is explicitly pinned,
and the network build stops if it differs. Strong stack protection is also
pinned, but detects corruption rather than preventing the bug. Keep the guard
until a reviewed SDK contains the upstream fix; do not increase the DNS count.

| Advisory | Vendor issue | Assessment for this build |
| --- | --- | --- |
| [GHSA-49x8-xqjg-2pjg](https://github.com/espressif/esp-idf/security/advisories/GHSA-49x8-xqjg-2pjg) | Unvalidated Stack Pointer at the ESP-TEE Privilege-Switch Boundary | ESP-TEE is not used on this ESP32-S3 build; advisory lists newer ESP-TEE releases. |
| [GHSA-rjvf-fxv5-x87j](https://github.com/espressif/esp-idf/security/advisories/GHSA-rjvf-fxv5-x87j) | Stack-Based Buffer Overflow in BLE Mesh GATT Server Read Handling | Fixed in 5.3.6; Bluetooth/BLE Mesh GATT is not enabled or exposed. |
| [GHSA-c26h-gm53-qg83](https://github.com/espressif/esp-idf/security/advisories/GHSA-c26h-gm53-qg83) | Stack-Based Out-of-Bounds Write in Thread Border Router ND6 Option Parsing | Fixed in 5.3.6; no OpenThread Border Router is enabled or linked. |
| [GHSA-xcpr-5mqp-9qvv](https://github.com/espressif/esp-idf/security/advisories/GHSA-xcpr-5mqp-9qvv) | Heap Out-of-Bounds Read in Bluedroid A2DP Sink Media Packet Processing | No Bluedroid Classic/A2DP sink; ESP32-S3 client does not expose this protocol. |
| [GHSA-v335-fxwc-rc44](https://github.com/espressif/esp-idf/security/advisories/GHSA-v335-fxwc-rc44) | Bluetooth Classic L2CAP Heap Memory Disclosure Allowing Unauthorized Access to Sensitive Data | Fixed in 5.3.6; no Bluetooth Classic/L2CAP host service. |
| [GHSA-v6r2-f6p2-88cj](https://github.com/espressif/esp-idf/security/advisories/GHSA-v6r2-f6p2-88cj) | Stack-Based Out-of-Bounds Write in JPEG Decoder DQT Marker Parsing | Fixed in 5.3.6; app accepts bounded PNG emotes, not hardware-decoded JPEG input. |
| [GHSA-ch63-jxm5-vmvf](https://github.com/espressif/esp-idf/security/advisories/GHSA-ch63-jxm5-vmvf) | Stack-Based Buffer Overflow in lwIP DNS Cache Lookup with Multiple IP Addresses | Affected version: mitigated by pinned DNS_MAX_HOST_IP=1 and fatal CMake guard. Strong stack protection is additional detection, not the fix. |
| [GHSA-3pp8-42fh-3j3c](https://github.com/espressif/esp-idf/security/advisories/GHSA-3pp8-42fh-3j3c) | Heap Out-of-Bounds Read in Bluedroid AVRCP Target Parser | Fixed in 5.3.6; no Classic Bluetooth AVRCP target. |
| [GHSA-w82j-7q63-7pqm](https://github.com/espressif/esp-idf/security/advisories/GHSA-w82j-7q63-7pqm) | Out-of-Bounds Read in ESP-TEE Secure Service Wrappers | No ESP-TEE service/REE boundary in this build; advisory version/chip prerequisites are absent. |
| [GHSA-mmgp-73p4-92xp](https://github.com/espressif/esp-idf/security/advisories/GHSA-mmgp-73p4-92xp) | Out-of-Bounds Write in ESP-TEE Secure Service Wrappers | No ESP-TEE service/REE boundary in this build; advisory version/chip prerequisites are absent. |
| [GHSA-9r76-858f-v6jh](https://github.com/espressif/esp-idf/security/advisories/GHSA-9r76-858f-v6jh) | Heap buffer overflow in protocomm Security2 over Bluetooth | Fixed in 5.3.6; no NimBLE/protocomm Security2 provisioning service is initialized or linked. |
| [GHSA-3j8v-xgrq-5vg8](https://github.com/espressif/esp-idf/security/advisories/GHSA-3j8v-xgrq-5vg8) | Remote Null Pointer Dereference in WebSocket Server | Fixed in 5.3.6; native firmware is a WebSocket client and registers no HTTP/WebSocket server. |
| [GHSA-g764-gwc3-75m5](https://github.com/espressif/esp-idf/security/advisories/GHSA-g764-gwc3-75m5) | Out-of-bounds Read in lwIP DHCP Server Option Parser | Fixed in 5.3.6; SoftAP and lwIP DHCP server explicitly disabled. Station DHCP client is a different path. |
| [GHSA-h7r3-gmg9-xjmg](https://github.com/espressif/esp-idf/security/advisories/GHSA-h7r3-gmg9-xjmg) | Use-after-free Vulnerability in BLE Provisioning | Fixed in 5.3.5 or earlier; BLE provisioning is not exposed. |
| [GHSA-9j5x-rf36-54x9](https://github.com/espressif/esp-idf/security/advisories/GHSA-9j5x-rf36-54x9) | Out-of-Bounds Read Vulnerability in BLE Provisioning | Fixed in 5.3.5 or earlier; BLE provisioning is not exposed. |
| [GHSA-m2h2-683f-9mw7](https://github.com/espressif/esp-idf/security/advisories/GHSA-m2h2-683f-9mw7) | WPS Enrollee Fragment Integer Underflow Vulnerability | Fixed in 5.3.5; application does not invoke WPS enrolment. |
| [GHSA-hmjj-rjvv-w8pq](https://github.com/espressif/esp-idf/security/advisories/GHSA-hmjj-rjvv-w8pq) | Out-of-Bounds Write in ESP32 Bluetooth SDP Result Handling | Fixed in 5.3.5; no Bluetooth Classic SDP host. |
| [GHSA-43gh-7r4f-qp57](https://github.com/espressif/esp-idf/security/advisories/GHSA-43gh-7r4f-qp57) | Out-of-Bounds Write in ESP32 Bluetooth AVRCP Vendor Command Handling | Fixed in 5.3.5; no Bluetooth Classic AVRCP host. |
| [GHSA-vcw6-jc3p-4gj8](https://github.com/espressif/esp-idf/security/advisories/GHSA-vcw6-jc3p-4gj8) | ESP32-P4 JPEG Decoder Header Parsing Vulnerability | Fixed in 5.3.5; ESP32-P4 JPEG path is not used by this ESP32-S3 PNG client. |
| [GHSA-qhf9-vr2h-jh96](https://github.com/espressif/esp-idf/security/advisories/GHSA-qhf9-vr2h-jh96) | Out-of-Bounds Read in ESP32 Bluetooth AVRCP Command Handling | Fixed in 5.3.5; no Bluetooth Classic AVRCP host. |
| [GHSA-8mg7-9qpg-p92v](https://github.com/espressif/esp-idf/security/advisories/GHSA-8mg7-9qpg-p92v) | ESP32 Bluetooth Controller Invalid Access Address Vulnerability | Fixed in 5.3.5; advisory is ESP32-specific Bluetooth controller behavior, not an active ESP32-S3 feature here. |
| [GHSA-9w88-r2vm-qfc4](https://github.com/espressif/esp-idf/security/advisories/GHSA-9w88-r2vm-qfc4) | BluFi Example Memory Overflow Vulnerability | Vulnerable BluFi example is not included or run; setup uses private loopback host form plus USB. |
| [GHSA-hqhh-cp47-fv5g](https://github.com/espressif/esp-idf/security/advisories/GHSA-hqhh-cp47-fv5g) | ESP-NOW Integer Underflow Vulnerability Advisory | Fixed in 5.3.4; app does not initialize ESP-NOW. |
| [GHSA-wm57-466g-mhrr](https://github.com/espressif/esp-idf/security/advisories/GHSA-wm57-466g-mhrr) | AES/CBC Constant IV Vulnerability in ESPTouch v2 | Fixed in 5.3.2; app does not initialize ESPTouch/SmartConfig. |
| [GHSA-22x6-3756-pfp8](https://github.com/espressif/esp-idf/security/advisories/GHSA-22x6-3756-pfp8) | Bootloader TOCTOU Vulnerability in Anti-rollback Scheme | 5.3.6 is outside listed older affected versions; anti-rollback is not enabled and no eFuse security claim is made. |
| [GHSA-7f7f-jj2q-28wm](https://github.com/espressif/esp-idf/security/advisories/GHSA-7f7f-jj2q-28wm) | Espressif Bluetooth Mesh Stack Vulnerability | Older affected branches are outside the pin; BLE Mesh is not enabled or exposed. |

The [5.3.6 release](https://github.com/espressif/esp-idf/releases/tag/v5.3.6)
includes fixes for affected 5.3.5 components. The older 5.3.5 / Mbed TLS 3.6.5
qualification is historical and is not the beta's dependency baseline.

## Mbed TLS review

The [3.6.7 release and security links](https://github.com/Mbed-TLS/mbedtls/releases/tag/mbedtls-3.6.7)
include the July 2026 fixes for certificate parsing/validation, TLS 1.2 handshake
and error handling, TLS 1.3/DTLS issues, PKCS7, ECC, RSA and ChaCha20. The shipped
source's `mbedtls/build_info.h` identifies 3.6.7. The maintained 3.6 branch is
listed for security support through at least March 2027. The
[upstream advisory index](https://mbed-tls.readthedocs.io/en/latest/security-advisories/)
was used rather than treating an empty GitHub-advisory response as evidence.

Particularly relevant checks are
[certificate CA-bit parsing, fixed in 3.6.7](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2026-07-x509-ca-bit-forgery-invalid-basicconstraints/),
[certificate signature-policy enforcement, fixed in 3.6.7](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2026-07-signature-algorithm-restrictions-certificate-chain/),
and [zero-length ECC-key parsing, fixed in 3.6.7](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2026-07-zero-length-ecc-public-key-oob-read/).
The client still verifies certificate chains and hostnames; an update is not a
reason to disable validation. It imports no user-provided CA certificates.

Two qualifications remain explicit:

- [CVE-2025-66442](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2026-03-compiler-induced-constant-time-violations/)
  concerns compiler-induced timing leakage across library versions. Upstream
  identifies a Clang/LLVM select-optimize configuration; other compilers are not
  known to be affected. This firmware uses Xtensa GCC 13.2 at ordinary `-O2`,
  without that LLVM option. This is a configuration assessment, not a formal
  constant-time proof; reassess before changing compiler/optimization options.
- [CVE-2026-34877](https://mbed-tls.readthedocs.io/en/latest/security-advisories/mbedtls-security-advisory-2026-03-serialized-data/)
  requires confidentiality and integrity for serialized TLS sessions/contexts;
  newer documentation does not remove the requirement. The application does
  not persist or import serialized TLS state. OAuth token NVS is a different
  object and remains unencrypted. Reassess before adding persistent TLS caches
  or accepting external session/context blobs.

No additional framework upgrade is required by the applicable findings in this
review after 5.3.6, the DNS workaround and these configuration restrictions. This
conclusion expires when new advisories or relevant configuration changes appear.

## LVGL, WebSocket and image decoding

The [LVGL repository advisory list](https://github.com/lvgl/lvgl/security/advisories)
returned no published entries at review time. That is a narrow observation,
not assurance for all LVGL or bundled C code. LVGL 9 is not required by an
identified applicable advisory here; its renderer/API migration would need its
own hardware qualification. The app uses compiled fonts and bounded copied
chat; it does not load arbitrary external font files.

The [esp-protocols vendor list](https://github.com/espressif/esp-protocols/security/advisories)
had two entries:
[esp_modem string getters](https://github.com/espressif/esp-protocols/security/advisories/GHSA-2j6m-56jw-9mqg)
and [mDNS decompression](https://github.com/espressif/esp-protocols/security/advisories/GHSA-7fpr-vjw5-2c86).
Neither component is linked or initialized. Neither is an advisory for the
pinned WebSocket client. The ESP-IDF WebSocket-*server* advisory above also does
not describe this client transport.

[LodePNG's upstream source/change record](https://github.com/lvandeve/lodepng)
was reviewed alongside the actual bundled decoder and application limits above.
Untrusted image parsing remains a native-code attack surface even behind a
verified CDN. Keep the bounds, PNG signature/IHDR checks, metadata restrictions
and fixed origin. New formats, animated/third-party emotes or arbitrary URLs
require a separate review; do not infer permission from cache capacity alone.

## Host and font-tool audit results

The reviewed host requirements are `pyserial==3.5` and `Pillow==13.0.0` on
Python 3.11 or newer. `pip-audit 2.10.1` reported no known findings for those two
installed requirements. `npm audit` for the pinned font-tool lock reported
zero findings across 11 dependencies. These results are time- and database-bound;
they do not assess the SDK, C decoders, radio firmware, application logic or
unlisted global developer tools. Font generation is not part of an ordinary
firmware build.

Re-run dependency scans and this applicability review before publication.
Keep raw reports local if they contain host details; publish only sanitized
results. Continue monitoring the official advisory sources above. The hardware
beta also retains unencrypted device credentials, trusted physical USB access,
unqualified battery/mount/thermal behavior and pending long-duration testing;
those limits are documented in [SECURITY](../SECURITY.md) and the
[verification record](verification.md), not disproved by passing audits.
