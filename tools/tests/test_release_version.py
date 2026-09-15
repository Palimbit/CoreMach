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
    def make_tree(self, root: Path, version: str = "7.0.0", **overrides: str) -> None:
        parsed = release_version.parse_version(version)
        values = {
            "cmake_base": parsed.base,
            "cmake_prerelease": parsed.prerelease,
            "vcpkg": parsed.full,
            "rpm_version": parsed.base,
            "rpm_release": parsed.rpm_release,
            "rpm_upstream": parsed.full,
            "metainfo": parsed.full,
            "debian": parsed.debian,
        }
        values.update(overrides)
        fixtures = {
            "CMakeLists.txt": (
                f"project(BluMach\n    VERSION {values['cmake_base']}\n    LANGUAGES C CXX)\n"
                f'set(BLUMACH_VERSION_PRERELEASE "{values["cmake_prerelease"]}")\n'
            ),
            "vcpkg.json": f'{{"version-string": "{values["vcpkg"]}"}}\n',
            "src/unix/assets/BluMach.spec": (
                f"Version:\t{values['rpm_version']}\n"
                f"Release:\t{values['rpm_release']}%{{?dist}}\n"
                f"%global upstream_version {values['rpm_upstream']}\n"
                "Source0:\thttps://example.invalid/v%{upstream_version}.tar.gz\n"
                "%autosetup -p1 -n BluMach-%{upstream_version}\n"
                "* Sat Jun 20 2026 BluMach project maintainers "
                "<blumach@users.noreply.github.com> 7.0.0-1\n"
            ),
            "src/unix/assets/io.github.BluMach.BluMach.metainfo.xml": (
                f'<release version="{values["metainfo"]}" date="2026-06-20"/>\n'
            ),
            "debian/changelog": (
                f"blumach ({values['debian']}) UNRELEASED; urgency=medium\n\n"
                " -- BluMach project maintainers <blumach@users.noreply.github.com>  "
                "Sat, 20 Jun 2026 00:00:00 +0000\n"
            ),
        }
        for relative, contents in fixtures.items():
            path = root / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text(contents, encoding="utf-8")

    def test_check_accepts_stable_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root)
            with mock.patch.object(release_version, "ROOT", root):
                self.assertEqual(release_version.check_versions(), "7.0.0")

    def test_check_accepts_prerelease_package_mappings(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, "0.1.0-rc.1")
            with mock.patch.object(release_version, "ROOT", root):
                self.assertEqual(release_version.check_versions(), "0.1.0-rc.1")

    def test_check_rejects_divergent_metadata(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, vcpkg="7.1.0")
            with mock.patch.object(release_version, "ROOT", root):
                with self.assertRaisesRegex(release_version.VersionError, "not synchronized"):
                    release_version.check_versions()

    def test_set_updates_prerelease_versions_and_dates(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root)
            with mock.patch.object(release_version, "ROOT", root):
                release_version.set_version("0.1.0-rc.1", date(2026, 9, 15))
                self.assertEqual(release_version.check_versions(), "0.1.0-rc.1")

            cmake = (root / "CMakeLists.txt").read_text()
            metainfo = (root / "src/unix/assets/io.github.BluMach.BluMach.metainfo.xml").read_text()
            changelog = (root / "debian/changelog").read_text()
            spec = (root / "src/unix/assets/BluMach.spec").read_text()
            self.assertIn("VERSION 0.1.0", cmake)
            self.assertIn('BLUMACH_VERSION_PRERELEASE "rc.1"', cmake)
            self.assertIn('version="0.1.0-rc.1" date="2026-09-15"', metainfo)
            self.assertIn("0.1.0~rc1-1", changelog)
            self.assertIn("Tue, 15 Sep 2026 00:00:00 +0000", changelog)
            self.assertRegex(spec, r"(?m)^Version:\s*0\.1\.0$")
            self.assertRegex(spec, r"(?m)^Release:\s*0\.rc1%\{\?dist\}$")
            self.assertIn("%global upstream_version 0.1.0-rc.1", spec)
            self.assertIn("v%{upstream_version}.tar.gz", spec)

    def test_set_updates_back_to_stable(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root, "0.1.0-rc.1")
            with mock.patch.object(release_version, "ROOT", root):
                release_version.set_version("0.1.0", date(2026, 9, 15))
                self.assertEqual(release_version.check_versions(), "0.1.0")

    def test_set_rejects_invalid_prerelease(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            self.make_tree(root)
            with mock.patch.object(release_version, "ROOT", root):
                with self.assertRaisesRegex(release_version.VersionError, "SemVer"):
                    release_version.set_version("0.1-rc1", date(2026, 9, 15))


if __name__ == "__main__":
    unittest.main()
