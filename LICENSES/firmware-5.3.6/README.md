# Firmware license coverage

This bundle covers the reviewed ESP-IDF **5.3.6** source/header and prebuilt
inputs used by the native application and its bootloader, including linked
members that the linker later discards. `coverage.json` pins every reviewed
input hash and its license evidence. `linked-source-notices.txt` preserves the
actual file-level copyright and license comments, including historical license
alternatives. Canonical full texts accompany those notices. The surrounding
application uses Apache-2.0; font data remains OFL-1.1.

Mbed TLS uses its Apache-2.0 alternative, and wpa_supplicant uses BSD. GCC13.2
runtime code uses GPLv3 with the GCC Runtime Library Exception3.1; the native
target uses an eligible GCC compilation process. LLVM PSTL headers retain the
Apache/LLVM exception. Newlib and Xtensa/Tensilica/Cadence notices are retained
separately. These licenses do not relicense the application as GPL.

The converted Mozilla CA store uses MPL-2.0. Its exact unmodified source is
provided at `../esp-idf-5.3.6/ca-source/cacrt_all.pem`, together with Espressif's
`cacrt_local.pem` supplement (SDK Apache-2.0 default). Corresponding public source
URLs and hashes are in `provenance.json`. The SDK commit is
`79e3454c68248bd7d881721c9b2e94561378a8ce`. Conversion changes only representation
to subject/public-key records via the pinned SDK generator. The original PEM
sources remain available under their respective licenses; recipients can use
and modify the MPL-covered source under MPL-2.0. Custom CA inputs are not covered.

After the **final** build, run:

```sh
python3 tools/audit_firmware_licenses.py --build-dir PATH --output license-audit.json
```

The audit returns nonzero and `complete:false` for missing/changed/unreviewed
inputs or notices, stale source/header objects, a partial build, an unsupported
SDK, or custom certificates. It resolves source candidates from both compile
databases and obtains actual included headers through Ninja dependency data.
Prebuilt archives and standalone runtime objects require exact reviewed hashes.
Its report contains portable paths and exact application/bootloader hashes.
Distribute the report, root LICENSE/NOTICE, THIRD_PARTY_NOTICES.md, and this
complete LICENSES tree with firmware. Never use a report for another binary.

To prepare an update, use `tools/review_firmware_license_inputs.py --build-dir
PATH --output candidate.json`. This generates a **candidate**, not release
approval. Review new source origins, every license exception and alternative,
copyright/comment changes, prebuilt provenance, public CA sources, and full
texts. Only then promote the reviewed catalog with `review_status:"approved"`.
The final audit still must pass; catalog review does not approve a stale image.
The tools perform no network access or device/credential access.
