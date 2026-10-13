"""The build gate must reject sheets that look plausible but are incomplete."""
import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
from preflight import inspect_sheets  # noqa: E402


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.fixture = {
            "native_functions": {
                "name": "native_functions",
                "columns": ["id", "rva", "runtime_verified"],
                "rows": [{"id": "spawn", "rva": "0x1234", "runtime_verified": True}],
                "references": [],
            },
            "systems": {
                "name": "systems",
                "columns": ["id", "uses", "verification"],
                "rows": [{"id": "double", "uses": ["native_functions:spawn"], "verification": "passed"}],
                "references": [{"column": "uses", "qualified": True}],
                "release_columns": ["verification"],
            },
        }

    def inspect(self, stage="build"):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name, sheet in self.fixture.items():
                (root / f"{name}.json").write_text(json.dumps(sheet), encoding="utf-8")
            return inspect_sheets(root, stage)

    def test_completed_fixture_passes_both_stages(self):
        self.assertTrue(self.inspect("build")["clean"])
        self.assertTrue(self.inspect("release")["clean"])

    def test_missing_native_address_blocks_build(self):
        self.fixture["native_functions"]["rows"][0]["rva"] = None
        self.assertFalse(self.inspect()["clean"])

    def test_unverified_native_row_blocks_build(self):
        self.fixture["native_functions"]["rows"][0]["runtime_verified"] = False
        self.assertFalse(self.inspect()["clean"])

    def test_pending_system_builds_but_cannot_be_released(self):
        self.fixture["systems"]["rows"][0]["verification"] = "pending"
        self.assertTrue(self.inspect("build")["clean"])
        self.assertEqual(self.inspect("build")["pending_release_checks"], ["systems/double/verification"])
        self.assertFalse(self.inspect("release")["clean"])

    def test_dangling_reference_blocks_build(self):
        self.fixture["systems"]["rows"][0]["uses"] = ["native_functions:invented_spawn"]
        self.assertFalse(self.inspect()["clean"])

    def test_row_no_system_uses_blocks_build(self):
        self.fixture["native_functions"]["rows"].append({"id": "orphan", "rva": "0x5678", "runtime_verified": True})
        self.assertFalse(self.inspect()["clean"])

    def test_undeclared_column_blocks_build(self):
        self.fixture["native_functions"]["rows"][0]["note"] = "not a column"
        self.assertFalse(self.inspect()["clean"])

    def test_duplicate_row_identity_blocks_build(self):
        self.fixture["native_functions"]["rows"].append(copy.deepcopy(self.fixture["native_functions"]["rows"][0]))
        self.assertFalse(self.inspect()["clean"])

    def test_empty_directory_does_not_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            self.assertFalse(inspect_sheets(Path(folder))["clean"])

    def test_project_sheets_pass_the_build_gate(self):
        self.assertEqual(inspect_sheets(ROOT / "sheets", "build", ROOT)["findings"], [])


if __name__ == "__main__":
    unittest.main()
