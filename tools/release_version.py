#!/usr/bin/env python3
"""Check or update BluMach release metadata without network access."""

from __future__ import annotations

import argparse
import re
import sys
from datetime import date, datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
VERSION_PATTERN = r"[0-9]+\.[0-9]+(?:\.[0-9]+)?"


class VersionError(RuntimeError):
    """Raised when release metadata is missing, ambiguous, or inconsistent."""


def metadata_patterns() -> dict[Path, re.Pattern[str]]:
    return {
        ROOT / "CMakeLists.txt": re.compile(
            rf"(?ms)(project\(BluMach\s+VERSION\s+)({VERSION_PATTERN})"
        ),
        ROOT / "vcpkg.json": re.compile(
            rf'(?m)("version-string"\s*:\s*")({VERSION_PATTERN})(")'
        ),
        ROOT / "src/unix/assets/BluMach.spec": re.compile(
            rf"(?m)^(Version:\s*)({VERSION_PATTERN})[ \t]*$"
        ),
        ROOT / "src/unix/assets/io.github.BluMach.BluMach.metainfo.xml": re.compile(
            rf'(<release\s+version=")({VERSION_PATTERN})("\s+date="[0-9]{{4}}-[0-9]{{2}}-[0-9]{{2}}"\s*/>)'
        ),
        ROOT / "debian/changelog": re.compile(
            rf"(?m)^(blumach \()({VERSION_PATTERN})(\)\s+)"
        ),
    }


def read_versions() -> dict[Path, str]:
    versions: dict[Path, str] = {}
    for path, pattern in metadata_patterns().items():
        text = path.read_text(encoding="utf-8")
        matches = list(pattern.finditer(text))
        if len(matches) != 1:
            relative = path.relative_to(ROOT)
            raise VersionError(f"{relative}: expected one release version, found {len(matches)}")
        versions[path] = matches[0].group(2)
    return versions


def check_versions() -> str:
    versions = read_versions()
    unique = set(versions.values())
    if len(unique) != 1:
        details = ", ".join(
            f"{path.relative_to(ROOT).as_posix()}={version}"
            for path, version in versions.items()
        )
        raise VersionError(f"release versions are not synchronized: {details}")
    return unique.pop()


def replace_once(path: Path, pattern: re.Pattern[str], replacement: str) -> None:
    text = path.read_text(encoding="utf-8")
    updated, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        relative = path.relative_to(ROOT)
        raise VersionError(f"{relative}: expected one matching field, found {count}")
    path.write_text(updated, encoding="utf-8", newline="")


def set_version(version: str, release_date: date) -> None:
    if re.fullmatch(VERSION_PATTERN, version) is None:
        raise VersionError("version must contain two or three numeric components (for example 7.0 or 7.0.1)")

    # Refuse to overwrite inconsistent metadata: divergence should be reviewed,
    # not silently normalized by a release command.
    check_versions()

    for path, pattern in metadata_patterns().items():
        replace_once(path, pattern, rf"\g<1>{version}\g<3>" if pattern.groups >= 3 else rf"\g<1>{version}")

    metainfo = ROOT / "src/unix/assets/io.github.BluMach.BluMach.metainfo.xml"
    replace_once(
        metainfo,
        re.compile(r'(<release\s+version="[^"]+"\s+date=")[0-9]{4}-[0-9]{2}-[0-9]{2}("\s*/>)'),
        rf"\g<1>{release_date.isoformat()}\g<2>",
    )

    debian = ROOT / "debian/changelog"
    debian_stamp = datetime(
        release_date.year, release_date.month, release_date.day, tzinfo=timezone.utc
    ).strftime("%a, %d %b %Y %H:%M:%S +0000")
    replace_once(
        debian,
        re.compile(r"(?m)^( -- BluMach project maintainers <blumach@users\.noreply\.github\.com>\s+).+$"),
        rf"\g<1>{debian_stamp}",
    )

    spec = ROOT / "src/unix/assets/BluMach.spec"
    rpm_stamp = release_date.strftime("%a %b %d %Y")
    replace_once(
        spec,
        re.compile(rf"(?m)^\* .+ BluMach project maintainers <blumach@users\.noreply\.github\.com> {VERSION_PATTERN}-1$"),
        f"* {rpm_stamp} BluMach project maintainers <blumach@users.noreply.github.com> {version}-1",
    )

    observed = check_versions()
    if observed != version:
        raise VersionError(f"updated metadata reports {observed}, expected {version}")


def parse_date(value: str) -> date:
    try:
        return date.fromisoformat(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("date must use YYYY-MM-DD") from exc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check", help="verify that every release metadata file has the same version")
    set_parser = subparsers.add_parser("set", help="set a numeric release version and explicit release date")
    set_parser.add_argument("version")
    set_parser.add_argument("--date", required=True, type=parse_date, dest="release_date")
    args = parser.parse_args(argv)

    try:
        if args.command == "check":
            version = check_versions()
            print(f"BluMach release metadata is synchronized at {version}")
        else:
            set_version(args.version, args.release_date)
            print(f"BluMach release metadata updated to {args.version} ({args.release_date})")
    except (OSError, VersionError) as exc:
        print(f"release_version: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
