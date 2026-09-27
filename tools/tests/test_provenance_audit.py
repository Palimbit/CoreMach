#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later

import json
import tempfile
import unittest
from unittest import mock
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

    def test_filtered_origin_uses_local_commit_without_rewriting_manifest(self):
        manifest = self.base_manifest()
        original, filtered = "a" * 40, "b" * 40
        component = manifest["components"][0]
        component.update(method="selective-port", origin_commit=original,
                         origin_repository="https://github.com/BluMach/BluMach",
                         origin_paths=["src/original.c"])
        temporary, root, path = self.make_tree(manifest)
        self.addCleanup(temporary.cleanup)
        (root / "provenance").mkdir()
        (root / "provenance/history-commit-map.txt").write_text(
            f"old new\n{original} {filtered}\n", encoding="ascii")
        with mock.patch.object(provenance_audit, "git_object_exists", return_value=True) as check:
            self.assertEqual(provenance_audit.audit(root, path)["errors"], [])
            self.assertEqual(check.call_args_list, [
                mock.call(root, filtered + "^{commit}"),
                mock.call(root, filtered + ":src/original.c")])
        self.assertEqual(json.loads(path.read_text())["components"][0]["origin_commit"], original)

    def test_rejects_ambiguous_history_mapping(self):
        temporary, root, path = self.make_tree(self.base_manifest())
        self.addCleanup(temporary.cleanup)
        (root / "provenance").mkdir()
        original = "a" * 40
        (root / "provenance/history-commit-map.txt").write_text(
            f"old new\n{original} {'b' * 40}\n{original} {'c' * 40}\n", encoding="ascii")
        with self.assertRaisesRegex(provenance_audit.AuditError, "duplicate original"):
            provenance_audit.audit(root, path)

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
