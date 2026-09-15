#!/usr/bin/env python3
"""Validate an installed BluMach package without requiring ROM images."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


MIB = 1024 * 1024


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument(
        "--platform", required=True, choices=("linux", "macos", "windows")
    )
    parser.add_argument("--qt", required=True, choices=("on", "off"))
    parser.add_argument("--max-size-mib", required=True, type=float)
    parser.add_argument("--expected-version")
    return parser.parse_args()


def required_paths(root: Path, platform: str, qt: bool) -> tuple[Path, list[Path]]:
    if platform == "windows":
        executable = root / "BluMach.exe"
        required = [executable]
        if qt:
            required.extend(
                [root / "Qt6Core.dll", root / "platforms" / "qwindows.dll"]
            )
    elif platform == "macos":
        contents = root / "BluMach.app" / "Contents"
        executable = contents / "MacOS" / "BluMach"
        required = [executable, contents / "Info.plist"]
        if qt:
            required.extend(
                [
                    contents / "Frameworks" / "QtCore.framework",
                    contents / "PlugIns" / "platforms" / "libqcocoa.dylib",
                    contents / "Resources" / "qt.conf",
                ]
            )
    else:
        executable = root / "bin" / "BluMach"
        required = [executable]

    return executable, required


def package_size(root: Path) -> tuple[int, int]:
    files = [path for path in root.rglob("*") if path.is_file()]
    return sum(path.stat().st_size for path in files), len(files)


def append_summary(platform: str, qt: bool, size: int, limit: float) -> None:
    summary_path = os.environ.get("GITHUB_STEP_SUMMARY")
    if not summary_path:
        return

    with open(summary_path, "a", encoding="utf-8") as summary:
        summary.write("\n### Package audit\n\n")
        summary.write("| Platform | UI | Installed size | Limit |\n")
        summary.write("| --- | --- | ---: | ---: |\n")
        summary.write(
            f"| {platform} | {'Qt 6' if qt else 'SDL'} | "
            f"{size / MIB:.1f} MiB | {limit:.1f} MiB |\n"
        )


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    qt = args.qt == "on"

    if not root.is_dir():
        raise SystemExit(f"Package root does not exist: {root}")

    executable, required = required_paths(root, args.platform, qt)
    missing = [path.relative_to(root) for path in required if not path.exists()]
    if missing:
        formatted = ", ".join(str(path) for path in missing)
        raise SystemExit(f"Package is missing required paths: {formatted}")

    size, file_count = package_size(root)
    size_mib = size / MIB
    print(f"Package contains {file_count} files and uses {size_mib:.1f} MiB")
    append_summary(args.platform, qt, size, args.max_size_mib)
    if size_mib > args.max_size_mib:
        raise SystemExit(
            f"Package size {size_mib:.1f} MiB exceeds the "
            f"{args.max_size_mib:.1f} MiB regression budget"
        )

    environment = os.environ.copy()
    if args.platform == "windows":
        # MSYS2 places its complete toolchain in PATH. Hide it so the smoke test
        # proves that the downloaded package is self-contained on stock Windows.
        windows_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
        environment["PATH"] = os.pathsep.join(
            (str(windows_root / "System32"), str(windows_root))
        )

    result = subprocess.run(
        [str(executable), "--version"],
        cwd=root,
        env=environment,
        capture_output=True,
        text=True,
        timeout=20,
        check=False,
    )
    if result.returncode != 0:
        details = (result.stderr or result.stdout).strip()
        suffix = f": {details}" if details else ""
        raise SystemExit(
            f"Packaged executable failed its startup check "
            f"(exit {result.returncode}){suffix}"
        )

    output = (result.stdout + result.stderr).strip()
    if output and "BluMach" not in output:
        raise SystemExit(f"Unexpected --version output: {output}")
    if args.expected_version and output != f"BluMach {args.expected_version}":
        raise SystemExit(
            f"Unexpected --version output: {output!r}; expected "
            f"'BluMach {args.expected_version}'"
        )
    print(output or "Packaged executable started and exited successfully")
    return 0


if __name__ == "__main__":
    sys.exit(main())
