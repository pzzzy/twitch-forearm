"""Safety properties of release-license gating; no SDK or hardware required."""
import hashlib
import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
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
        self.assertEqual(report["application_sha256"], hashlib.sha256(b"application image").hexdigest())
        self.assertEqual(report["bootloader_sha256"], hashlib.sha256(b"bootloader image").hexdigest())
        self.assertNotIn(str(self.root), json.dumps(report))

    def test_changed_header_cannot_inherit_approval(self):
        self.inputs.paths["sdk/reviewed.h"].write_text("Modified upstream code")
        report = audit.audit(self.inputs, self.catalog)
        self.assertFalse(report["complete"])
        self.assertIn("reviewed input changed: sdk/reviewed.h", report["missing"])

    def test_unrecognized_member_source_fails_closed(self):
        new = self.root / "new-source.c"
        new.write_text("New source absent from the reviewed catalog")
        self.inputs.paths["sdk/new-source.c"] = new
        report = audit.audit(self.inputs, self.catalog)
        self.assertFalse(report["complete"])
        self.assertIn("unreviewed input: sdk/new-source.c", report["missing"])

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


if __name__ == "__main__":
    unittest.main()
