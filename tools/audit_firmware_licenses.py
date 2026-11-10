#!/usr/bin/env python3
"""Fail-closed license inventory for the exact linked firmware and bootloader.

This checks reviewed source/header and prebuilt-runtime hashes, not merely a
component-name allowlist. It performs no network access and never reads NVS,
flash backups, credentials, or arbitrary .local files. Run after the final build.
"""
from __future__ import annotations

import argparse
from collections import defaultdict
import hashlib
import json
from pathlib import Path
import re
import subprocess

PROJECT = Path(__file__).resolve().parents[1]
CATALOG = PROJECT / "LICENSES/firmware-5.3.6/coverage.json"


def sha256(path: Path) -> str:
    return hashlib.sha256(path.read_bytes()).hexdigest()


class AuditError(Exception):
    pass


class Inputs:
    def __init__(self, build: Path, sdk: Path | None = None):
        self.build = build.resolve()
        description = json.loads((self.build / "project_description.json").read_text())
        self.description = description
        self.sdk = (sdk or Path(description["idf_path"])).resolve()
        compiler = Path(description["c_compiler"]).resolve()
        self.toolchain = compiler.parent.parent
        self.paths: dict[str, Path] = {}
        self.archives: dict[str, dict] = {}
        self.objects: list[dict] = []
        self.missing: list[str] = []

    def normalized(self, path: Path) -> str:
        path = path.resolve()
        for prefix, root in (("sdk", self.sdk), ("toolchain", self.toolchain),
                             ("build", self.build), ("project", PROJECT)):
            if path.is_relative_to(root):
                if prefix == "project" and path.relative_to(root).parts[0] == ".local":
                    raise AuditError("input is outside the explicitly selected SDK/build roots")
                return prefix + "/" + path.relative_to(root).as_posix()
        raise AuditError("input outside the approved project/SDK/toolchain roots")

    def add(self, path: Path) -> str:
        path = path.resolve()
        name = self.normalized(path)
        if not path.is_file():
            self.missing.append("input missing: " + name)
        else:
            self.paths[name] = path
        return name

    def digest(self, name: str) -> str:
        data = self.paths[name].read_bytes()
        if name.startswith("build/"):
            # Generated assembly has .incbin absolute build paths. Normalize
            # only approved roots so identical clean builds remain portable.
            for root, marker in ((self.sdk, "<sdk>"), (self.toolchain, "<toolchain>"),
                                 (self.build, "<build>"), (PROJECT, "<project>")):
                data = data.replace(str(root).encode(), marker.encode())
        return hashlib.sha256(data).hexdigest()

    def collect_build(self, build: Path, image: str, map_name: str):
        mapping = defaultdict(list)
        output_mapping = {}
        commands = json.loads((build / "compile_commands.json").read_text())
        for command in commands:
            output = Path(command["output"]).resolve()
            relative = output.relative_to(build).as_posix()
            source = Path(command["file"]).resolve()
            mapping[output.name].append((relative, source, output))
            output_mapping[relative] = (source, output)

        deps_process = subprocess.run(["ninja", "-t", "deps"], cwd=build,
                                      capture_output=True, text=True, check=True)
        dependencies = {}
        key = None
        for line in deps_process.stdout.splitlines():
            if line and not line.startswith(" "):
                key = line.split(": #deps", 1)[0]
                dependencies[key] = []
                if "(STALE)" in line:
                    # Only used objects are checked below; unused stale build
                    # products are not evidence for the final firmware.
                    dependencies[key].append(None)
            elif key and line.startswith("    "):
                dependency = Path(line.strip())
                dependencies[key].append((dependency if dependency.is_absolute() else build / dependency).resolve())

        def collect_object(relative, source, output):
            self.add(source)
            if relative not in dependencies:
                self.missing.append("dependency evidence missing: " + self.normalized(output))
                return
            for dependency in dependencies[relative]:
                if dependency is None:
                    self.missing.append("stale dependency evidence: " + self.normalized(output))
                    continue
                self.add(dependency)
                if dependency.is_file() and output.is_file() and dependency.stat().st_mtime_ns > output.stat().st_mtime_ns:
                    self.missing.append("unbuilt source/header: " + self.normalized(dependency))

        map_file = build / map_name
        text = map_file.read_text()
        # This conservative set includes linked members later discarded by GC.
        # Including their notices is safe; omitting a loaded member is not.
        header = text.split("Discarded input sections", 1)[0]
        members = sorted(set(re.findall(r"^([^\s]+\.a)\(([^)]+)\)", header, re.M)))
        if not members:
            raise AuditError("link map has no archive-member evidence")
        for archive_name, member in members:
            archive_path = Path(archive_name)
            if not archive_path.is_absolute():
                archive_path = build / archive_path
            archive_path = archive_path.resolve()
            archive_key = self.normalized(archive_path)
            record = self.archives.setdefault(archive_key, {"path": archive_key, "sha256": sha256(archive_path),
                                                         "images": [], "members": []})
            if image not in record["images"]:
                record["images"].append(image)
            sources = []
            if archive_path.is_relative_to(build):
                parent = archive_path.parent.relative_to(build).as_posix() + "/"
                matches = [entry for entry in mapping[member] if entry[0].startswith(parent)]
                if not matches:
                    self.missing.append("unresolved archive member: " + archive_key + "(" + member + ")")
                for relative, source, output in matches:
                    collect_object(relative, source, output)
                    sources.append(self.normalized(source))
            else:
                # Prebuilt SDK and runtime archives must match a reviewed hash.
                self.add(archive_path)
            entry = {"name": member, "sources": sorted(set(sources))}
            if entry not in record["members"]:
                record["members"].append(entry)

        # Startup/runtime objects and the generated ELF source are not archives.
        standalone = sorted(set(re.findall(r"^LOAD ([^\n]+\.(?:o|obj))$", text, re.M)))
        for raw in standalone:
            path = Path(raw)
            if not path.is_absolute():
                path = build / path
            path = path.resolve()
            name = self.normalized(path)
            if path.is_relative_to(build):
                relative = path.relative_to(build).as_posix()
                if relative not in output_mapping:
                    self.missing.append("unresolved standalone object: " + name)
                    continue
                source, output = output_mapping[relative]
                collect_object(relative, source, output)
                self.objects.append({"path": name, "image": image, "source": self.normalized(source)})
            else:
                self.add(path)
                self.objects.append({"path": name, "image": image})

    def collect(self):
        version = self.description.get("git_revision", "").lstrip("v")
        if version != "5.3.6":
            self.missing.append("SDK version has no reviewed inventory: " + version)
        self.collect_build(self.build, "application", self.description["project_name"] + ".map")
        self.collect_build(self.build / "bootloader", "bootloader", "bootloader.map")
        # Embedded CA data has no C copyright header, so explicitly inventory
        # both the generated payload and every public source input.
        config = (self.build / "config/sdkconfig.h").read_text()
        if re.search(r"^#define CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE\s+1$", config, re.M):
            self.missing.append("custom certificate source requires separate license review")
        if "#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE 1" in config:
            ca = self.sdk / "components/mbedtls/esp_crt_bundle"
            for filename in ("cacrt_all.pem", "cacrt_local.pem", "gen_crt_bundle.py"):
                self.add(ca / filename)
            if "#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEFAULT_CMN 1" in config:
                self.add(ca / "cmn_crt_authorities.csv")
            if "#define CONFIG_MBEDTLS_CERTIFICATE_BUNDLE_DEPRECATED_LIST 1" in config:
                self.add(ca / "cacrt_deprecated.pem")
        return self


def audit(inputs: Inputs, catalog: dict) -> dict:
    missing = list(inputs.missing)
    if catalog.get("review_status") != "approved":
        missing.append("license coverage catalog has not been approved after review")
    approved = catalog["inputs"]
    license_files = set()
    for name in sorted(inputs.paths):
        record = approved.get(name)
        if record is None:
            missing.append("unreviewed input: " + name)
            continue
        if record["sha256"] != inputs.digest(name):
            missing.append("reviewed input changed: " + name)
        license_files.update(record["license_files"])
    for record in catalog["license_files"]:
        path = PROJECT / record["path"]
        if not path.is_file() or sha256(path) != record["sha256"]:
            missing.append("license evidence missing/changed: " + record["path"])
    license_files.update(record["path"] for record in catalog["license_files"])
    application = inputs.build / inputs.description["app_bin"]
    bootloader = inputs.build / "bootloader/bootloader.bin"
    for path in (application, bootloader):
        if not path.is_file():
            missing.append("built image missing: " + inputs.normalized(path))
    for archive in inputs.archives.values():
        for image in archive["images"]:
            binary = application if image == "application" else bootloader
            # A partially rebuilt tree is not evidence for the existing image.
            relative = archive["path"]
            roots = {"sdk": inputs.sdk, "toolchain": inputs.toolchain, "build": inputs.build, "project": PROJECT}
            prefix, _, suffix = relative.partition("/")
            archive_path = roots[prefix] / suffix
            if binary.is_file() and archive_path.stat().st_mtime_ns > binary.stat().st_mtime_ns:
                missing.append("image predates linked archive: " + relative)
    report = {
        "schema_version": 1,
        "complete": not missing,
        "application_sha256": sha256(application) if application.is_file() else "",
        "bootloader_sha256": sha256(bootloader) if bootloader.is_file() else "",
        "sdk_version": inputs.description.get("git_revision", "").lstrip("v"),
        "coverage_catalog_sha256": sha256(CATALOG),
        "archives": sorted(inputs.archives.values(), key=lambda record: record["path"]),
        "standalone_objects": inputs.objects,
        "reviewed_input_count": len(inputs.paths),
        "ca_sources": [{"path": name, "sha256": inputs.digest(name)} for name in sorted(inputs.paths)
                       if name.endswith(("/cacrt_all.pem", "/cacrt_local.pem", "/cacrt_deprecated.pem"))],
        "license_files": sorted(license_files),
        "missing": sorted(set(missing)),
    }
    return report


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", required=True, type=Path)
    parser.add_argument("--sdk-path", type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    try:
        inputs = Inputs(args.build_dir, args.sdk_path).collect()
        report = audit(inputs, json.loads(CATALOG.read_text()))
    except (AuditError, OSError, ValueError, KeyError, subprocess.CalledProcessError) as error:
        # Never publish machine-specific exception paths in the release report.
        report = {"schema_version": 1, "complete": False, "application_sha256": "", "bootloader_sha256": "",
                  "sdk_version": "", "archives": [], "license_files": [],
                  "missing": ["audit inputs could not be resolved; check final build, Ninja, SDK and reviewed catalog"]}
        print("Firmware license audit could not resolve its inputs:", type(error).__name__)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(report, indent=2) + "\n")
    print("Firmware license audit:", "complete" if report["complete"] else "INCOMPLETE",
          "(%d missing checks)" % len(report["missing"]))
    return 0 if report["complete"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
