from __future__ import annotations

import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import rc_manifest  # noqa: E402


class RcManifestTests(unittest.TestCase):
    def test_rejects_firmware_and_media_paths(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = [root / "roms" / "machine.bin", root / "disk.img"]
            for path in files:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"restricted")
            with self.assertRaisesRegex(rc_manifest.ManifestError, "firmware or guest media"):
                rc_manifest.reject_restricted_assets(root, files)

    def test_accepts_normal_runtime_files(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            files = [root / "BluMach.exe", root / "platforms" / "qwindows.dll"]
            for path in files:
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_bytes(b"runtime")
            rc_manifest.reject_restricted_assets(root, files)

    def test_sha256_is_deterministic(self) -> None:
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "file"
            path.write_bytes(b"BluMach")
            self.assertEqual(
                rc_manifest.sha256(path),
                "82666f6b9585157591399c5b923ffcbc1478184220cb4cab1d82be568601384b",
            )


if __name__ == "__main__":
    unittest.main()
