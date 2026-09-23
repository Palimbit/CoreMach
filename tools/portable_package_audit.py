#!/usr/bin/env python3
"""Validate an installed BluMach portable-product package without ROMs."""

from __future__ import annotations

import argparse
import os
from pathlib import Path
import subprocess
import sys


MIB = 1024 * 1024
FORBIDDEN_SUFFIXES = {
    ".bin",
    ".dsk",
    ".hdi",
    ".ima",
    ".img",
    ".imd",
    ".rom",
    ".td0",
    ".vhd",
}


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", required=True, type=Path)
    parser.add_argument(
        "--platform", required=True, choices=("linux", "macos", "windows")
    )
    parser.add_argument("--version", required=True)
    parser.add_argument("--max-size-mib", type=float, default=100.0)
    return parser.parse_args()


def package_paths(root: Path, platform: str) -> tuple[Path, Path, list[Path]]:
    if platform == "windows":
        binaries = root / "bin"
        headless = binaries / "BluMach-headless.exe"
        desktop = binaries / "BluMach-portable.exe"
        required = [
            headless,
            desktop,
            binaries / "Qt6Core.dll",
            binaries / "Qt6Gui.dll",
            binaries / "Qt6Widgets.dll",
            binaries / "libgcc_s_seh-1.dll",
            binaries / "libstdc++-6.dll",
            binaries / "libwinpthread-1.dll",
            root / "share" / "qt6" / "plugins" / "platforms" / "qwindows.dll",
        ]
    elif platform == "macos":
        contents = root / "BluMach-portable.app" / "Contents"
        headless = root / "bin" / "BluMach-headless"
        desktop = contents / "MacOS" / "BluMach-portable"
        required = [
            headless,
            desktop,
            contents / "Info.plist",
            contents / "Frameworks" / "QtCore.framework",
            contents / "PlugIns" / "platforms" / "libqcocoa.dylib",
        ]
    else:
        headless = root / "bin" / "BluMach-headless"
        desktop = root / "bin" / "BluMach-portable"
        required = [headless, desktop]
    required.extend(
        [root / "COPYING", root / "FORK-NOTICE.md", root / "AUTHORS",
         root / "README-Portable.md"]
    )
    return headless, desktop, required


def clean_environment(platform: str) -> dict[str, str]:
    environment = os.environ.copy()
    if platform == "windows":
        windows_root = Path(os.environ.get("SystemRoot", r"C:\Windows"))
        environment["PATH"] = os.pathsep.join(
            (str(windows_root / "System32"), str(windows_root))
        )
    elif platform == "linux":
        environment["QT_QPA_PLATFORM"] = "offscreen"
    return environment


def run_smoke(executable: Path, arguments: list[str], environment: dict[str, str]) -> str:
    result = subprocess.run(
        [str(executable), *arguments],
        cwd=executable.parent,
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
            f"Package smoke test failed for {executable.name} "
            f"(exit {result.returncode}){suffix}"
        )
    return (result.stdout + result.stderr).strip()


def main() -> int:
    args = parse_args()
    root = args.root.resolve()
    if not root.is_dir():
        raise SystemExit(f"Package root does not exist: {root}")

    headless, desktop, required = package_paths(root, args.platform)
    missing = [path.relative_to(root) for path in required if not path.exists()]
    if missing:
        raise SystemExit(
            "Package is missing required paths: "
            + ", ".join(str(path) for path in missing)
        )

    packaged_files = [path for path in root.rglob("*") if path.is_file()]
    forbidden = [
        path.relative_to(root)
        for path in packaged_files
        if path.suffix.lower() in FORBIDDEN_SUFFIXES
        or "roms" in {part.lower() for part in path.relative_to(root).parts}
    ]
    if forbidden:
        raise SystemExit(
            "Package contains forbidden firmware or media: "
            + ", ".join(str(path) for path in forbidden)
        )

    package_size = sum(path.stat().st_size for path in packaged_files)
    size_mib = package_size / MIB
    if size_mib > args.max_size_mib:
        raise SystemExit(
            f"Package size {size_mib:.1f} MiB exceeds the "
            f"{args.max_size_mib:.1f} MiB regression budget"
        )

    environment = clean_environment(args.platform)
    output = run_smoke(headless, ["--version"], environment)
    expected = f"BluMach portable engine {args.version}"
    if output != expected:
        raise SystemExit(f"Unexpected version output: {output!r}; expected {expected!r}")

    machines = run_smoke(headless, ["--list-machines"], environment).splitlines()
    for machine_id in ("olivetti-pcs86", "olivetti-m15"):
        if machine_id not in machines:
            raise SystemExit(f"Packaged registry does not contain {machine_id}")
        description = run_smoke(
            headless, ["--describe", machine_id], environment
        )
        if "engine_mode=clocked" not in description:
            raise SystemExit(
                f"Unexpected description for {machine_id}: {description}"
            )

    # --version exits before the event loop and proves the deployed Qt runtime
    # and platform plugin can load without opening a persistent window.
    run_smoke(desktop, ["--version"], environment)

    print(
        f"Portable package passed: {len(packaged_files)} files, "
        f"{size_mib:.1f} MiB, version {args.version}"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
