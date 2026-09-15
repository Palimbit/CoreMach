#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import tempfile
import unittest
from pathlib import Path

import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import provenance_audit  # noqa: E402


class ProvenanceAuditTests(unittest.TestCase):
    def make_tree(self, manifest):
        temporary = tempfile.TemporaryDirectory()
        root = Path(temporary.name)
        (root / "engine").mkdir()
        (root / "engine" / "unit.c").write_text(
            "/* SPDX-License-Identifier: GPL-2.0-or-later */\n", encoding="utf-8"
        )
        manifest_path = root / "components.json"
        manifest_path.write_text(json.dumps(manifest), encoding="utf-8")
        return temporary, root, manifest_path

    def base_manifest(self):
        return {
            "schema": "blumach-component-provenance-v1",
            "coverage_roots": ["engine"],
            "components": [
                {
                    "id": "engine",
                    "destination": ["engine/**"],
                    "method": "new",
                    "license": "GPL-2.0-or-later",
                    "copyright_holders": ["BluMach contributors"],
                }
            ],
            "external_dependencies": [],
        }

    def test_accepts_exact_coverage(self):
        temporary, root, path = self.make_tree(self.base_manifest())
        self.addCleanup(temporary.cleanup)
        self.assertEqual(provenance_audit.audit(root, path)["errors"], [])

    def test_rejects_multiple_owners(self):
        manifest = self.base_manifest()
        manifest["components"].append(
            {
                "id": "duplicate",
                "destination": ["engine/*.c"],
                "method": "new",
                "license": "GPL-2.0-or-later",
                "copyright_holders": [],
            }
        )
        temporary, root, path = self.make_tree(manifest)
        self.addCleanup(temporary.cleanup)
        errors = provenance_audit.audit(root, path)["errors"]
        self.assertTrue(any("exactly one provenance owner" in error for error in errors))

    def test_rejects_media_and_missing_spdx(self):
        temporary, root, path = self.make_tree(self.base_manifest())
        self.addCleanup(temporary.cleanup)
        (root / "engine" / "firmware.rom").write_bytes(b"not firmware")
        errors = provenance_audit.audit(root, path)["errors"]
        self.assertTrue(any("firmware or guest-media" in error for error in errors))
        self.assertTrue(any("missing SPDX" in error for error in errors))


if __name__ == "__main__":
    unittest.main()
