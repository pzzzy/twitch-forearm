"""Safety properties of release-license gating; no SDK or hardware required."""
import hashlib
import importlib.util
import io
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from contextlib import redirect_stdout
from unittest.mock import patch

MODULE_PATH = Path(__file__).resolve().parents[1] / "audit_firmware_licenses.py"
SPEC = importlib.util.spec_from_file_location("firmware_license_audit", MODULE_PATH)
audit = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(audit)


class LicenseAuditTest(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name).resolve()
        self.build = self.root / "build"
        (self.build / "bootloader").mkdir(parents=True)
        (self.build / "app.bin").write_bytes(b"application image")
        (self.build / "bootloader/bootloader.bin").write_bytes(b"bootloader image")
        (self.root / "LICENSES").mkdir()
        (self.root / "LICENSES/example.txt").write_text("Reviewed full license")
        source = self.root / "reviewed.h"
        source.write_text("Reviewed source/header")
        self.inputs = audit.Inputs.__new__(audit.Inputs)
        self.inputs.build = self.build
        self.inputs.sdk = self.root / "sdk"
        self.inputs.toolchain = self.root / "toolchain"
        self.inputs.description = {"app_bin": "app.bin", "git_revision": "v5.3.6"}
        self.inputs.paths = {"sdk/reviewed.h": source}
        self.inputs.missing = []
        self.inputs.archives = {}
        self.inputs.objects = []
        self.catalog = {"review_status": "approved", "inputs": {"sdk/reviewed.h": {
            "sha256": audit.sha256(source), "license_files": ["LICENSES/example.txt"]}},
            "license_files": [{"path": "LICENSES/example.txt", "sha256": audit.sha256(self.root / "LICENSES/example.txt")}]}
        (self.root / "coverage.json").write_text(json.dumps(self.catalog))
        p1 = patch.object(audit, "PROJECT", self.root)
        p2 = patch.object(audit, "CATALOG", self.root / "coverage.json")
        p1.start(); p2.start()
        self.addCleanup(p1.stop); self.addCleanup(p2.stop)

    def test_complete_binds_both_exact_images(self):
        report = audit.audit(self.inputs, self.catalog)
        self.assertTrue(report["complete"])
        self.assertEqual(report["missing"], [])
        self.assertEqual(report["input_differences"], [])
        self.assertEqual(report["application_sha256"], hashlib.sha256(b"application image").hexdigest())
        self.assertEqual(report["bootloader_sha256"], hashlib.sha256(b"bootloader image").hexdigest())
        self.assertNotIn(str(self.root), json.dumps(report))

    def test_changed_header_cannot_inherit_approval(self):
        self.inputs.paths["sdk/reviewed.h"].write_text("Modified upstream code")
        report = audit.audit(self.inputs, self.catalog)
        self.assertFalse(report["complete"])
        self.assertIn("reviewed input changed: sdk/reviewed.h", report["missing"])
        self.assertEqual(report["input_differences"], [{
            "path": "sdk/reviewed.h",
            "expected_sha256": self.catalog["inputs"]["sdk/reviewed.h"]["sha256"],
            "observed_sha256": audit.sha256(self.inputs.paths["sdk/reviewed.h"])}])
        self.assertNotIn(str(self.root), json.dumps(report))

    def test_unrecognized_member_source_fails_closed(self):
        new = self.root / "new-source.c"
        new.write_text("New source absent from the reviewed catalog")
        self.inputs.paths["sdk/new-source.c"] = new
        report = audit.audit(self.inputs, self.catalog)
        self.assertFalse(report["complete"])
        self.assertIn("unreviewed input: sdk/new-source.c", report["missing"])
        self.assertEqual(report["input_differences"], [{
            "path": "sdk/new-source.c", "expected_sha256": None,
            "observed_sha256": audit.sha256(new)}])

    def test_reviewed_platform_variant_is_exact_and_retains_original(self):
        record = self.catalog["inputs"]["sdk/reviewed.h"]
        original = self.inputs.paths["sdk/reviewed.h"].read_bytes()
        alternate = b"Reviewed platform configuration with identical license"
        alternate_hash = hashlib.sha256(alternate).hexdigest()
        record["reviewed_variants"] = [{"sha256": alternate_hash, "provenance": "official-linux-package"}]
        self.catalog["variant_provenance"] = {"official-linux-package": {"review_status": "approved"}}
        self.assertTrue(audit.audit(self.inputs, self.catalog)["complete"])
        self.inputs.paths["sdk/reviewed.h"].write_bytes(alternate)
        report = audit.audit(self.inputs, self.catalog)
        self.assertTrue(report["complete"])
        self.assertEqual(report["reviewed_variants_used"], [{
            "path": "sdk/reviewed.h", "sha256": alternate_hash, "provenance": "official-linux-package"}])
        self.inputs.paths["sdk/reviewed.h"].write_bytes(alternate + b" unreviewed edit")
        self.assertFalse(audit.audit(self.inputs, self.catalog)["complete"])
        self.inputs.paths["sdk/reviewed.h"].write_bytes(original)
        self.assertTrue(audit.audit(self.inputs, self.catalog)["complete"])

    def test_variant_does_not_approve_another_path_or_unreviewed_provenance(self):
        record = self.catalog["inputs"]["sdk/reviewed.h"]
        alternate = b"Exact reviewed alternate"
        self.inputs.paths["sdk/reviewed.h"].write_bytes(alternate)
        variant = {"sha256": hashlib.sha256(alternate).hexdigest(), "provenance": "official-linux-package"}
        record["reviewed_variants"] = [variant]
        self.assertFalse(audit.audit(self.inputs, self.catalog)["complete"])
        self.catalog["variant_provenance"] = {"official-linux-package": {"review_status": "candidate"}}
        self.assertFalse(audit.audit(self.inputs, self.catalog)["complete"])
        self.catalog["variant_provenance"]["official-linux-package"]["review_status"] = "approved"
        other = self.root / "other.h"
        other.write_bytes(alternate)
        self.inputs.paths["sdk/other.h"] = other
        self.catalog["inputs"]["sdk/other.h"] = {
            "sha256": hashlib.sha256(b"Original other file").hexdigest(),
            "license_files": record["license_files"]}
        report = audit.audit(self.inputs, self.catalog)
        self.assertFalse(report["complete"])
        self.assertEqual(report["input_differences"][0]["path"], "sdk/other.h")

    def test_missing_or_changed_license_fails_closed(self):
        (self.root / "LICENSES/example.txt").unlink()
        self.assertFalse(audit.audit(self.inputs, self.catalog)["complete"])

    def test_candidate_or_stale_build_never_approves(self):
        self.catalog["review_status"] = "candidate"
        self.assertFalse(audit.audit(self.inputs, self.catalog)["complete"])
        self.catalog["review_status"] = "approved"
        self.inputs.missing.append("unbuilt source/header: sdk/reviewed.h")
        self.assertFalse(audit.audit(self.inputs, self.catalog)["complete"])

    def test_private_local_paths_are_rejected_without_reading(self):
        self.inputs.sdk = self.root / ".local/sdk"
        self.inputs.build = self.root / ".local/build"
        with self.assertRaises(audit.AuditError):
            self.inputs.normalized(self.root / ".local/private/credentials.json")
        self.assertEqual(self.inputs.normalized(self.root / ".local/sdk/include/public.h"), "sdk/include/public.h")

    def test_compile_database_paths_use_entry_directory(self):
        directory = self.build / "work"
        directory.mkdir()
        source, output = audit.compile_paths({
            "directory": str(directory), "file": "../../sdk/source.c",
            "output": "../objects/source.c.obj"}, self.build)
        self.assertEqual(source, self.root / "sdk/source.c")
        self.assertEqual(output, self.build / "objects/source.c.obj")
        with self.assertRaises(audit.AuditError):
            audit.compile_paths({"directory": str(directory), "file": "source.c",
                                 "output": "../../outside.obj"}, self.build)

    def test_real_collection_accepts_absolute_relative_and_optional_output(self):
        """Exercise map/dependency resolution with CMake's portable DB variants."""
        sdk = self.root / "sdk"
        sdk.mkdir()
        source = sdk / "source.c"
        source.write_text("/* Reviewed source */\n")
        object_relative = "esp-idf/example/CMakeFiles/__idf_example.dir/source.c.obj"
        output = self.build / object_relative
        output.parent.mkdir(parents=True)
        output.write_bytes(b"compiled object")
        archive = self.build / "esp-idf/example/libexample.a"
        archive.write_bytes(b"built archive")
        (self.build / "app.map").write_text(
            "Archive member included to satisfy reference by file (symbol)\n\n"
            "esp-idf/example/libexample.a(source.c.obj)\n"
            "Discarded input sections\n")
        dependency_output = object_relative + ": #deps 1, deps mtime 1 (VALID)\n    " + str(source) + "\n\n"
        variants = [
            {"output": str(output)},
            {"output": object_relative},
            {"arguments": ["cc", "-c", str(source), "-o", object_relative]},
            {"command": "cc -c ../sdk/source.c -o '" + object_relative + "'"},
            {"arguments": ["cc", "-c", str(source), "-o" + object_relative]},
        ]
        for variant in variants:
            with self.subTest(variant=variant):
                command = {"directory": str(self.build), "file": "../sdk/source.c", **variant}
                (self.build / "compile_commands.json").write_text(json.dumps([command]))
                self.inputs.paths = {}
                self.inputs.archives = {}
                self.inputs.missing = []
                with patch.object(audit.subprocess, "run", return_value=subprocess.CompletedProcess(
                        ["ninja", "-t", "deps"], 0, dependency_output, "")):
                    self.inputs.collect_build(self.build, "application", "app.map")
                self.assertEqual(self.inputs.missing, [])
                self.assertEqual(set(self.inputs.paths), {"sdk/source.c"})
                member = self.inputs.archives["build/esp-idf/example/libexample.a"]["members"][0]
                self.assertEqual(member, {"name": "source.c.obj", "sources": ["sdk/source.c"]})

    def test_missing_or_ambiguous_compile_output_fails_closed(self):
        base = {"directory": str(self.build), "file": "source.c"}
        for arguments in (["cc", "-c", "source.c"], ["cc", "-o"],
                          ["cc", "-o", "one.obj", "-o", "two.obj"]):
            with self.subTest(arguments=arguments), self.assertRaises(audit.AuditError):
                audit.compile_paths({**base, "arguments": arguments}, self.build)

    def test_resolution_failure_reports_stage_without_exception_paths(self):
        (self.build / "project_description.json").write_text(json.dumps({
            "idf_path": str(self.inputs.sdk),
            "c_compiler": str(self.inputs.toolchain / "bin/cc")}))
        report_path = self.build / "license-audit.json"

        def fail_collect(inputs):
            inputs.stage = "application compile database"
            raise ValueError("unpublished personal path: " + str(self.root))

        with patch.object(audit.Inputs, "collect", fail_collect), \
                patch.object(sys, "argv", ["audit", "--build-dir", str(self.build),
                                            "--output", str(report_path)]), redirect_stdout(io.StringIO()):
            self.assertEqual(audit.main(), 1)
        report = json.loads(report_path.read_text())
        self.assertFalse(report["complete"])
        self.assertEqual(report["diagnostic"], {
            "stage": "application compile database", "error_type": "ValueError"})
        self.assertNotIn(str(self.root), report_path.read_text())


if __name__ == "__main__":
    unittest.main()
