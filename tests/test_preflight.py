"""Build gates must reject plausible-looking but incomplete specifications."""
import copy
import json
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools"))
from preflight import inspect_sheets


class PreflightTests(unittest.TestCase):
    def setUp(self):
        self.fixture = {
            "hooks": {"name": "hooks", "columns": ["id", "rva", "runtime_verified"],
                "rows": [{"id": "spawn", "rva": "0x1234", "runtime_verified": True}],
                "checks": "all", "references": []},
            "systems": {"name": "systems", "columns": ["id", "hook", "verification"],
                "rows": [{"id": "actor", "hook": "spawn", "verification": "passed"}],
                "checks": "all", "references": [{"column": "hook", "sheet": "hooks", "key": "id"}]}
        }

    def inspect(self, fixture=None):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            for name, sheet in (fixture or self.fixture).items():
                (root / f"{name}.json").write_text(json.dumps(sheet), encoding="utf-8")
            return inspect_sheets(root)

    def test_completed_fixture_passes(self):
        self.assertTrue(self.inspect()["clean"])

    def test_missing_native_address_blocks_build(self):
        self.fixture["hooks"]["rows"][0]["rva"] = None
        self.assertFalse(self.inspect()["clean"])

    def test_claimed_checks_cannot_override_failed_runtime(self):
        self.fixture["hooks"]["rows"][0]["runtime_verified"] = False
        self.assertFalse(self.inspect()["clean"])

    def test_pending_behavior_cannot_be_shipped(self):
        self.fixture["systems"]["rows"][0]["verification"] = "pending"
        self.assertFalse(self.inspect()["clean"])

    def test_dangling_cross_sheet_reference_blocks_build(self):
        self.fixture["systems"]["rows"][0]["hook"] = "invented_spawn"
        self.assertFalse(self.inspect()["clean"])

    def test_unchecked_cell_blocks_build(self):
        self.fixture["hooks"]["checks"] = {"spawn": ["id", "rva"]}
        self.assertFalse(self.inspect()["clean"])

    def test_duplicate_row_identity_blocks_build(self):
        self.fixture["hooks"]["rows"].append(copy.deepcopy(self.fixture["hooks"]["rows"][0]))
        self.assertFalse(self.inspect()["clean"])

    def test_empty_directory_does_not_pass(self):
        with tempfile.TemporaryDirectory() as folder:
            self.assertFalse(inspect_sheets(Path(folder))["clean"])


if __name__ == "__main__":
    unittest.main()
