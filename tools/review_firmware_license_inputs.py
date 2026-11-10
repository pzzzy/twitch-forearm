#!/usr/bin/env python3
"""Prepare a license-review candidate; never approve a release automatically.

Review the candidate, all changes to source notices, SPDX alternatives, and
prebuilt provenance before promoting it to LICENSES/firmware-5.3.6/coverage.json
with review_status="approved". The audit command rejects candidate catalogs.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import json
from pathlib import Path
import re

from audit_firmware_licenses import Inputs, PROJECT, sha256

BASE = "LICENSES/firmware-5.3.6/"
SDK = "LICENSES/esp-idf-5.3.6/"
NOTICES = BASE + "linked-source-notices.txt"
APACHE = "LICENSES/Apache-2.0.txt"
SPDX = {
    "Apache-2.0": [APACHE],
    "Apache-2.0 OR GPL-2.0-or-later": [APACHE, SDK + "MbedTLS-dual-license.txt"],
    "Apache-2.0 WITH LLVM-exception": [BASE + "LLVM-exception.txt"],
    "MIT": [SDK + "FreeRTOS-MIT.txt"],
    "BSD-3-Clause": [BASE + "TLSF-BSD-3-Clause.txt"],
    "BSD-2-Clause-FreeBSD": [BASE + "FreeBSD-endian.txt"],
    "BSD-2-Clause-FreeBSD AND Apache-2.0": [BASE + "FreeBSD-endian.txt", APACHE],
    "OFL-1.1": ["LICENSES/NotoEmoji-OFL-1.1.txt", "LICENSES/Montserrat-2024-OFL-1.1.txt", "LICENSES/NotoSans-OFL-1.1.txt"],
    "CC0-1.0": ["LICENSES/CC0-1.0.txt"],
}


def classify(name: str, path: Path, text: str):
    identifiers = sorted(set(value.strip() for value in re.findall(r"SPDX-License-Identifier:\s*([^\n\r*]+)", text)))
    if identifiers:
        files = set()
        for identifier in identifiers:
            if identifier not in SPDX:
                raise ValueError("unknown SPDX expression: " + identifier)
            files.update(SPDX[identifier])
        return " AND ".join(identifiers), files, "Explicit SPDX; select Apache alternative for Mbed TLS"
    if name.startswith("toolchain/"):
        if "/include/c++/" in name or "/lib/gcc/" in name or path.name in ("libstdc++.a", "libgcc.a"):
            return "GPL-3.0-or-later WITH GCC-exception-3.1", {BASE + "GCC-GPL-3.0.txt", BASE + "GCC-runtime-exception-3.1.txt"}, "Installed GCC13.2 runtime/header license; eligible GCC compilation"
        return "Newlib-collection", {BASE + "Newlib-toolchain-COPYING.txt", BASE + "Newlib-libgloss-COPYING.txt"}, "Installed Newlib runtime/header collection and file notices"
    if name.startswith("build/"):
        if name == "build/x509_crt_bundle.S":
            return "MPL-2.0 AND Apache-2.0", {BASE + "MPL-2.0.txt", APACHE, SDK + "ca-source/cacrt_all.pem", SDK + "ca-source/cacrt_local.pem"}, "Generated public Mozilla CA data and Espressif supplement; exact sources supplied"
        if name in ("build/config/sdkconfig.h", "build/project_elf_src_esp32s3.c", "build/bootloader/config/sdkconfig.h", "build/bootloader/project_elf_src_esp32s3.c"):
            return "Apache-2.0", {APACHE}, "ESP-IDF generated configuration/empty ELF translation unit"
        raise ValueError("unreviewed generated input")
    if name.startswith("project/firmware/managed_components/lvgl__lvgl/"):
        if path.name in ("lodepng.c", "lodepng.h"):
            return "Zlib", {"LICENSES/LodePNG-20201017.txt"}, "LodePNG bundled in pinned LVGL8.4.0"
        if path.name in ("qrcodegen.c", "qrcodegen.h"):
            return "MIT", {"LICENSES/Nayuki-QR-MIT.txt"}, "Nayuki generator bundled in pinned LVGL8.4.0"
        if path.name.startswith("lv_font_montserrat_") and path.suffix == ".c":
            return "OFL-1.1 AND MIT", {"LICENSES/Montserrat-7.200-OFL-1.1.txt", "LICENSES/FontAwesome-5.9.0-OFL-1.1.txt", "LICENSES/FontAwesome-5.9.0-license.txt", "LICENSES/LVGL-8.4.0-MIT.txt"}, "Pinned legacy Montserrat/FontAwesome bitmap inputs plus LVGL glue"
        return "MIT", {"LICENSES/LVGL-8.4.0-MIT.txt"}, "Pinned LVGL8.4.0 license and per-file attribution"
    if name.startswith("project/") and not name.startswith("project/firmware/managed_components/"):
        return "Apache-2.0", {APACHE}, "Original project code under root license; explicit font/panel SPDX takes precedence"
    if name.startswith("sdk/"):
        families = [
            ("/lwip/lwip/", "BSD-3-Clause", {SDK + "lwIP-BSD.txt"}),
            ("/wpa_supplicant/", "BSD-3-Clause", {SDK + "wpa_supplicant-BSD.txt", SDK + "wpa_supplicant-COPYING.txt"}),
            ("/json/cJSON/", "MIT", {SDK + "cJSON-MIT.txt"}),
            ("/http_parser/", "MIT", {SDK + "HTTP-parser-MIT.txt"}),
            ("/xtensa/", "MIT", {BASE + "Xtensa-HAL-MIT.txt", SDK + "SDK-COPYRIGHT.rst"}),
        ]
        if name.endswith("/cacrt_all.pem"):
            return "MPL-2.0", {BASE + "MPL-2.0.txt", SDK + "ca-source/cacrt_all.pem"}, "Mozilla CA PEM source; exact original source supplied"
        if name.endswith("/ubsan.c"):
            return "BSD-2-Clause", {BASE + "UBSAN-BSD-2-Clause.txt"}, "Linaro/HelenOS/Espressif source notice"
        for part, license_name, files in families:
            if part in name:
                return license_name, files, "Reviewed SDK library license; all per-file notices retained; BSD alternative selected for WPA"
        if "/mbedtls/mbedtls/" in name:
            raise ValueError("Mbed TLS input lacks an explicit reviewed license")
        return "Apache-2.0", {APACHE}, "ESP-IDF original-code default/legacy Apache notice or Apache Espressif prebuilt; verify per-file exceptions"
    raise ValueError("unreviewed origin")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    if args.output.resolve() == (PROJECT / BASE / "coverage.json").resolve():
        parser.error("write a candidate elsewhere; promotion requires a separate review")
    inputs = Inputs(args.build_dir).collect()
    blocks = defaultdict(list)
    records = {}
    unknown = []
    counts = Counter()
    for name, path in sorted(inputs.paths.items()):
        text = "" if path.suffix in (".a", ".o") else path.read_text(errors="replace")
        for block in re.findall(r"/\*.*?\*/|(?://[^\n]*\n)+|(?:^#[^\n]*\n)+", text, re.S | re.M):
            if re.search(r"copyright|SPDX-License|licensed? under|permission is hereby|redistribution and use|public domain|free software foundation", block, re.I):
                blocks[block.strip()].append(name)
        try:
            license_name, files, basis = classify(name, path, text)
        except ValueError as error:
            unknown.append({"path": name, "reason": str(error)})
            continue
        files.add(NOTICES)
        records[name] = {"sha256": inputs.digest(name), "license": license_name,
                         "license_files": sorted(files), "basis": basis}
        counts[license_name] += 1
    if unknown:
        print(json.dumps(unknown, indent=2))
        raise SystemExit("Unrecognized license input; no candidate written")
    notice_text = ["Linked source/header copyright and license notices for the reviewed ESP-IDF5.3.6 build.\n"
                   "Full canonical license texts accompany this file. Paths are portable roots.\n"
                   "Mbed TLS uses Apache-2.0; WPA uses BSD; GCC runtime uses its Runtime Library Exception3.1.\n"]
    for block, paths in sorted(blocks.items(), key=lambda item: sorted(item[1])[0]):
        notice_text.append("\n" + "=" * 78 + "\nFiles:\n" + "\n".join("  " + name for name in sorted(set(paths))) + "\n\n" + block + "\n")
    (PROJECT / NOTICES).write_text("".join(notice_text))
    files = sorted(set(path for entry in records.values() for path in entry["license_files"]))
    result = {"schema_version": 1, "review_status": "candidate", "sdk_version": "5.3.6",
              "toolchain": "Espressif xtensa-esp-elf esp-13.2.0_20250707 (GCC13.2.0)",
              "scope": "Exact source/header/prebuilt hashes; loaded members later discarded are conservatively included. New or changed input requires renewed review.",
              "license_files": [{"path": path, "sha256": sha256(PROJECT / path)} for path in files],
              "inputs": records}
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print("Candidate:", len(records), "inputs,", len(blocks), "notice blocks; license families", dict(counts))
    print("Build freshness failures:", len(set(inputs.missing)), "; candidate still requires review before promotion.")


if __name__ == "__main__":
    main()
