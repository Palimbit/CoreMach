#!/usr/bin/env python3
"""Create and verify a private RC package manifest without embedding firmware."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import subprocess
import sys


SCHEMA = "blumach-private-rc-manifest-v1"
FORBIDDEN_PARTS = {"bios", "firmware", "media", "rom", "roms"}
FORBIDDEN_SUFFIXES = {
    ".86f",
    ".bin",
    ".dsk",
    ".flp",
    ".ima",
    ".img",
    ".iso",
    ".rom",
    ".vfd",
    ".vhd",
}


class ManifestError(RuntimeError):
    """Raised when an RC package cannot be safely described."""


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def package_files(root: Path) -> list[Path]:
    return sorted(
        (path for path in root.rglob("*") if path.is_file()),
        key=lambda path: path.relative_to(root).as_posix(),
    )


def reject_restricted_assets(root: Path, files: list[Path]) -> None:
    rejected: list[str] = []
    for path in files:
        relative = path.relative_to(root)
        parts = {part.casefold() for part in relative.parts[:-1]}
        if parts & FORBIDDEN_PARTS or path.suffix.casefold() in FORBIDDEN_SUFFIXES:
            rejected.append(relative.as_posix())
    if rejected:
        raise ManifestError(
            "package contains possible firmware or guest media: " + ", ".join(rejected)
        )


def git_value(source: Path, *arguments: str) -> str:
    result = subprocess.run(
        ["git", *arguments],
        cwd=source,
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        raise ManifestError(result.stderr.strip() or "unable to read Git metadata")
    return result.stdout.strip()


def create_manifest(
    root: Path,
    source: Path,
    version: str,
    platform: str,
    configuration: str,
) -> dict[str, object]:
    root = root.resolve()
    source = source.resolve()
    if not root.is_dir():
        raise ManifestError(f"package root does not exist: {root}")

    files = package_files(root)
    if not files:
        raise ManifestError(f"package root is empty: {root}")
    reject_restricted_assets(root, files)

    dirty = git_value(source, "status", "--porcelain", "--untracked-files=no")
    if dirty:
        raise ManifestError("source checkout has tracked changes; commit the RC before packaging")

    entries = [
        {
            "path": path.relative_to(root).as_posix(),
            "size": path.stat().st_size,
            "sha256": sha256(path),
        }
        for path in files
    ]
    return {
        "schema": SCHEMA,
        "product": "BluMach",
        "version": version,
        "source": {
            "commit": git_value(source, "rev-parse", "HEAD"),
            "ref": git_value(source, "branch", "--show-current"),
        },
        "build": {"platform": platform, "configuration": configuration},
        "firmware_included": False,
        "guest_media_included": False,
        "files": entries,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument("--source", default=Path.cwd(), type=Path)
    parser.add_argument("--version", required=True)
    parser.add_argument("--platform", required=True)
    parser.add_argument("--configuration", required=True)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    try:
        manifest = create_manifest(
            args.root, args.source, args.version, args.platform, args.configuration
        )
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(
            json.dumps(manifest, indent=2, ensure_ascii=False) + "\n",
            encoding="utf-8",
            newline="\n",
        )
    except (OSError, ManifestError) as exc:
        print(f"rc_manifest: {exc}", file=sys.stderr)
        return 1

    print(f"Wrote {args.output} with {len(manifest['files'])} packaged files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
