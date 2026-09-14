#!/usr/bin/env python3
"""Reject obsolete 86Box release identities while preserving compatibility names."""

from __future__ import annotations

import subprocess
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]

# Keep these fragments split so this file checks itself as well.
FORBIDDEN_TEXT = (
    "github.com/BluMach/" + "86Box",
    "github.com/BluMachOrg/" + "86Box",
    "net.86box." + "86Box",
    "86Box." + "86Box",
    "https://ci." + "86box.net",
    "https://86box.net/" + "builds",
    "api.github.com/repos/86box/" + "86Box",
    "Usage: " + "86box",
    "weblate.github.com/" + "BluMach/BluMach",
    "d:" + "\\\\86boxnew",
    "The current catalogue " + "contains",
    # BluMach targets Qt 6 hosts; these compatibility declarations advertise
    # unsupported Windows versions in the release executable manifest.
    "1f676c76-80e1-4239-95bb-" + "83d0f6d0da78",
    "4a2f28e3-53b9-4441-ba9c-" + "d69d4a4a6e38",
    "35138b9a-5d96-4fbd-8e2d-" + "a2440225f93a",
    "e2011457-1546-43c5-a5fe-" + "008deee3d3f0",
)

FORBIDDEN_NAMES = {
    "86Box" + "-qt.rc",
    "86Box" + ".manifest",
    "86box" + ".pot",
    "net.86box." + "86Box.desktop",
    "net.86box." + "86Box.metainfo.xml",
    "dis" + "cord.c",
    "dis" + "cord.h",
    "dis" + "cord_game_sdk.h",
    "Jenkins" + "file",
    "README-UNIX-MODE-WITH-" + "OSD.txt",
}

FORBIDDEN_PACKAGING_TEXT = {
    "debian/copyright": (
        "<" + "years>",
        "<" + "put author's name and email here>",
        "<" + "likewise for another author>",
    ),
    "vcpkg.json": ('"sdl' + '2"',),
    "debian/control": ("libsdl" + "2-dev",),
    "src/unix/assets/BluMach.spec": ("SDL" + "2-devel",),
}

REMOVED_FEATURE_TEXT = (
    "win" + "box",
    "dis" + "cord",
    "irc" + "notify",
)

UNSUPPORTED_QT_TEXT = (
    "USE_" + "QT6",
    "Qt" + "5_ROOT",
    "Qt" + "5LinguistTools",
    "-Qt" + "5",
    "qt" + "5-",
)


def tracked_files() -> list[Path]:
    result = subprocess.run(
        ["git", "ls-files", "-z", "--cached", "--others", "--exclude-standard"],
        cwd=ROOT,
        check=True,
        capture_output=True,
    )
    return [ROOT / item.decode("utf-8") for item in result.stdout.split(b"\0") if item]


def main() -> int:
    errors: list[str] = []
    for path in tracked_files():
        if not path.is_file():
            continue
        relative = path.relative_to(ROOT).as_posix()
        if path.name in FORBIDDEN_NAMES:
            errors.append(f"{relative}: obsolete release-facing filename")

        try:
            contents = path.read_text(encoding="utf-8")
        except (UnicodeDecodeError, OSError):
            continue

        for line_number, line in enumerate(contents.splitlines(), 1):
            for value in FORBIDDEN_TEXT:
                if value in line:
                    errors.append(f"{relative}:{line_number}: obsolete identity {value!r}")
            folded_line = line.casefold()
            for value in REMOVED_FEATURE_TEXT:
                if value in folded_line:
                    errors.append(f"{relative}:{line_number}: removed integration {value!r}")
            for value in UNSUPPORTED_QT_TEXT:
                if value in line:
                    errors.append(f"{relative}:{line_number}: unsupported Qt 5 compatibility {value!r}")
            for value in FORBIDDEN_PACKAGING_TEXT.get(relative, ()):
                if value in line:
                    errors.append(f"{relative}:{line_number}: obsolete package dependency {value!r}")

    if errors:
        print("BluMach release identity check failed:", file=sys.stderr)
        print("\n".join(f"  {error}" for error in errors), file=sys.stderr)
        return 1

    print("BluMach release identity check passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
