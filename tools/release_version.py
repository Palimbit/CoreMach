#!/usr/bin/env python3
"""Check or update BluMach release metadata without network access."""

from __future__ import annotations

import argparse
import re
import sys
from dataclasses import dataclass
from datetime import date, datetime, timezone
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASE_VERSION_PATTERN = r"[0-9]+\.[0-9]+\.[0-9]+"
PRERELEASE_PATTERN = r"[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*"
FULL_VERSION_PATTERN = rf"{BASE_VERSION_PATTERN}(?:-{PRERELEASE_PATTERN})?"


class VersionError(RuntimeError):
    """Raised when release metadata is missing, ambiguous, or inconsistent."""


@dataclass(frozen=True)
class ReleaseVersion:
    base: str
    prerelease: str = ""

    @property
    def full(self) -> str:
        return f"{self.base}-{self.prerelease}" if self.prerelease else self.base

    @property
    def debian(self) -> str:
        # Debian sorts '~' before the final release.
        suffix = self.prerelease.replace(".", "")
        return f"{self.base}~{suffix}-1" if suffix else f"{self.base}-1"

    @property
    def rpm_release(self) -> str:
        suffix = self.prerelease.replace(".", "")
        return f"0.{suffix}" if suffix else "1"


def parse_version(value: str) -> ReleaseVersion:
    match = re.fullmatch(
        rf"(?P<base>{BASE_VERSION_PATTERN})(?:-(?P<prerelease>{PRERELEASE_PATTERN}))?",
        value,
    )
    if match is None:
        raise VersionError(
            "version must be SemVer with three numeric components and an optional "
            "prerelease (for example 0.1.0 or 0.1.0-rc.1)"
        )
    return ReleaseVersion(match.group("base"), match.group("prerelease") or "")


def read_one(path: Path, pattern: re.Pattern[str], field: str) -> str:
    text = path.read_text(encoding="utf-8")
    matches = list(pattern.finditer(text))
    if len(matches) != 1:
        relative = path.relative_to(ROOT)
        raise VersionError(f"{relative}: expected one {field}, found {len(matches)}")
    return matches[0].group(1)


def observed_versions() -> dict[str, str]:
    cmake = ROOT / "CMakeLists.txt"
    spec = ROOT / "src/unix/assets/BluMach.spec"
    return {
        "cmake_base": read_one(
            cmake,
            re.compile(rf"(?ms)project\(BluMach\s+VERSION\s+({BASE_VERSION_PATTERN})"),
            "project version",
        ),
        "cmake_prerelease": read_one(
            cmake,
            re.compile(rf'(?m)^set\(BLUMACH_VERSION_PRERELEASE "({PRERELEASE_PATTERN}|)"\)$'),
            "prerelease identifier",
        ),
        "vcpkg": read_one(
            ROOT / "vcpkg.json",
            re.compile(rf'(?m)"version-string"\s*:\s*"({FULL_VERSION_PATTERN})"'),
            "version-string",
        ),
        "rpm_version": read_one(
            spec, re.compile(rf"(?m)^Version:\s*({BASE_VERSION_PATTERN})\s*$"), "Version"
        ),
        "rpm_release": read_one(
            spec,
            re.compile(r"(?m)^Release:\s*([^%\s]+)%\{\?dist\}\s*$"),
            "Release",
        ),
        "rpm_upstream": read_one(
            spec,
            re.compile(rf"(?m)^%global upstream_version\s+({FULL_VERSION_PATTERN})\s*$"),
            "upstream_version",
        ),
        "metainfo": read_one(
            ROOT / "src/unix/assets/io.github.BluMach.BluMach.metainfo.xml",
            re.compile(rf'<release\s+version="({FULL_VERSION_PATTERN})"\s+date="[0-9]{{4}}-[0-9]{{2}}-[0-9]{{2}}"\s*/>'),
            "release version",
        ),
        "debian": read_one(
            ROOT / "debian/changelog",
            re.compile(r"(?m)^blumach \(([^)]+)\)\s+"),
            "package version",
        ),
    }


def check_versions() -> str:
    observed = observed_versions()
    version = ReleaseVersion(observed["cmake_base"], observed["cmake_prerelease"])
    expected = {
        "cmake_base": version.base,
        "cmake_prerelease": version.prerelease,
        "vcpkg": version.full,
        "rpm_version": version.base,
        "rpm_release": version.rpm_release,
        "rpm_upstream": version.full,
        "metainfo": version.full,
        "debian": version.debian,
    }
    mismatches = [
        f"{field}={observed[field]} (expected {value})"
        for field, value in expected.items()
        if observed[field] != value
    ]
    if mismatches:
        raise VersionError("release versions are not synchronized: " + ", ".join(mismatches))
    return version.full


def replace_once(path: Path, pattern: re.Pattern[str], replacement: str) -> None:
    text = path.read_text(encoding="utf-8")
    updated, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        relative = path.relative_to(ROOT)
        raise VersionError(f"{relative}: expected one matching field, found {count}")
    path.write_text(updated, encoding="utf-8", newline="")


def set_version(value: str, release_date: date) -> None:
    version = parse_version(value)

    # Refuse to overwrite inconsistent metadata: divergence should be reviewed,
    # not silently normalized by a release command.
    check_versions()

    cmake = ROOT / "CMakeLists.txt"
    replace_once(
        cmake,
        re.compile(rf"(?ms)(project\(BluMach\s+VERSION\s+)({BASE_VERSION_PATTERN})"),
        rf"\g<1>{version.base}",
    )
    replace_once(
        cmake,
        re.compile(rf'(?m)^(set\(BLUMACH_VERSION_PRERELEASE ")({PRERELEASE_PATTERN}|)("\))$'),
        rf"\g<1>{version.prerelease}\g<3>",
    )

    replace_once(
        ROOT / "vcpkg.json",
        re.compile(rf'(?m)("version-string"\s*:\s*")({FULL_VERSION_PATTERN})(")'),
        rf"\g<1>{version.full}\g<3>",
    )

    spec = ROOT / "src/unix/assets/BluMach.spec"
    replace_once(
        spec,
        re.compile(rf"(?m)^(Version:\s*)({BASE_VERSION_PATTERN})\s*$"),
        rf"\g<1>{version.base}",
    )
    replace_once(
        spec,
        re.compile(r"(?m)^(Release:\s*)([^%\s]+)(%\{\?dist\})\s*$"),
        rf"\g<1>{version.rpm_release}\g<3>",
    )
    replace_once(
        spec,
        re.compile(rf"(?m)^(%global upstream_version\s+)({FULL_VERSION_PATTERN})\s*$"),
        rf"\g<1>{version.full}",
    )

    metainfo = ROOT / "src/unix/assets/io.github.BluMach.BluMach.metainfo.xml"
    replace_once(
        metainfo,
        re.compile(rf'(<release\s+version=")({FULL_VERSION_PATTERN})("\s+date=")[0-9]{{4}}-[0-9]{{2}}-[0-9]{{2}}("\s*/>)'),
        rf"\g<1>{version.full}\g<3>{release_date.isoformat()}\g<4>",
    )

    debian = ROOT / "debian/changelog"
    replace_once(
        debian,
        re.compile(r"(?m)^(blumach \()([^)]+)(\)\s+)"),
        rf"\g<1>{version.debian}\g<3>",
    )
    debian_stamp = datetime(
        release_date.year, release_date.month, release_date.day, tzinfo=timezone.utc
    ).strftime("%a, %d %b %Y %H:%M:%S +0000")
    replace_once(
        debian,
        re.compile(r"(?m)^( -- BluMach project maintainers <blumach@users\.noreply\.github\.com>\s+).+$"),
        rf"\g<1>{debian_stamp}",
    )

    rpm_stamp = release_date.strftime("%a %b %d %Y")
    replace_once(
        spec,
        re.compile(r"(?m)^\* .+ BluMach project maintainers <blumach@users\.noreply\.github\.com> [^\s]+$"),
        f"* {rpm_stamp} BluMach project maintainers <blumach@users.noreply.github.com> "
        f"{version.base}-{version.rpm_release}",
    )

    observed = check_versions()
    if observed != version.full:
        raise VersionError(f"updated metadata reports {observed}, expected {version.full}")


def parse_date(value: str) -> date:
    try:
        return date.fromisoformat(value)
    except ValueError as exc:
        raise argparse.ArgumentTypeError("date must use YYYY-MM-DD") from exc


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("check", help="verify all release metadata and packaging mappings")
    set_parser = subparsers.add_parser("set", help="set a SemVer release and explicit release date")
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
