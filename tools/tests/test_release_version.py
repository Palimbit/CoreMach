from __future__ import annotations

import sys
import tempfile
import unittest
from datetime import date
from pathlib import Path
from unittest import mock

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import release_version  # noqa: E402


class ReleaseVersionTests(unittest.TestCase):
    def make_tree(self, root: Path, versions: dict[str, str] | None = None) -> None:
        values = versions or {}
        fixtures = {
            "CMakeLists.txt": f"project(BluMach\n    VERSION {values.get('cmake', '7.0')}\n    LANGUAGES C CXX)\n",
            "vcpkg.json": f'{{"version-string": "{values.get("vcpkg", "7.0")}"}}\n',
            "src/unix/assets/BluMach.spec": (
                f"Version:\t{values.get('spec', '7.0')}\n"
                "* Sat Jun 20 2026 BluMach project maintainers "
                "<blumach@users.noreply.github.com> 7.0-1\n"
            ),
            "src/unix/assets/io.github.BluMach.BluMach.metainfo.xml": (
                f'<release version="{values.get("metainfo", "7.0")}" date="2026-06-20"/>\n'
            ),
            "debian/changelog": (
                f"blumach ({values.get('debian', '7.0')}) UNRELEASED; urgency=medium\n\n"
                " -- BluMach project maintainers <blumach@users.noreply.github.com>  "
                "Sat, 20 Jun 2026 00:00:00 +0000\n"
            ),
        }
        for relative, contents in fixtures.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")

    def test_check_accepts_synchronized_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root)
            with mock.patch.object(release_version, "ROOT", root):
                self.assertEqual(release_version.check_versions(), "7.0")

    def test_check_rejects_divergent_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, {"vcpkg": "7.1"})
            with mock.patch.object(release_version, "ROOT", root):
                with self.assertRaisesRegex(release_version.VersionError, "not synchronized"):
                    release_version.check_versions()

    def test_set_updates_versions_and_dates_without_network(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root)
            with mock.patch.object(release_version, "ROOT", root):
                release_version.set_version("7.1.2", date(2026, 9, 14))
                self.assertEqual(release_version.check_versions(), "7.1.2")

            metainfo = (root / "src/unix/assets/io.github.BluMach.BluMach.metainfo.xml").read_text()
            changelog = (root / "debian/changelog").read_text()
            spec = (root / "src/unix/assets/BluMach.spec").read_text()
            self.assertIn('version="7.1.2" date="2026-09-14"', metainfo)
            self.assertIn("Mon, 14 Sep 2026 00:00:00 +0000", changelog)
            self.assertIn("* Mon Sep 14 2026", spec)
            self.assertIn("7.1.2-1", spec)
            self.assertRegex(spec, r"(?m)^Version:\s*7\.1\.2$")

    def test_set_rejects_prerelease_suffix(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root)
            with mock.patch.object(release_version, "ROOT", root):
                with self.assertRaisesRegex(release_version.VersionError, "numeric components"):
                    release_version.set_version("7.1-rc1", date(2026, 9, 14))


if __name__ == "__main__":
    unittest.main()
