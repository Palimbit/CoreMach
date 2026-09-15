#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Validate provenance coverage for the portable BluMach architecture."""

from __future__ import annotations

import argparse
import fnmatch
import json
import subprocess
import sys
from pathlib import Path

ALLOWED_METHODS = {"new", "selective-port", "derived-rewrite", "third-party"}
MEDIA_SUFFIXES = {".rom", ".ima", ".img", ".iso", ".dsk", ".flp", ".vhd", ".vmdk", ".qcow", ".qcow2"}


class AuditError(RuntimeError):
    pass


def load_manifest(path: Path) -> dict:
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as exc:
        raise AuditError(f"cannot read {path}: {exc}") from exc
    if data.get("schema") != "blumach-component-provenance-v1":
        raise AuditError("unsupported or missing provenance schema")
    return data


def files_under(root: Path, relative_roots: list[str]) -> list[str]:
    result: list[str] = []
    for relative in relative_roots:
        candidate = root / relative
        if not candidate.exists():
            continue
        result.extend(
            path.relative_to(root).as_posix()
            for path in candidate.rglob("*")
            if path.is_file()
        )
    return sorted(result)


def git_object_exists(root: Path, object_name: str) -> bool:
    completed = subprocess.run(
        ["git", "cat-file", "-e", object_name],
        cwd=root,
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    return completed.returncode == 0


def tracked_files(root: Path) -> list[str]:
    completed = subprocess.run(
        ["git", "ls-files", "-z"],
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.DEVNULL,
        check=False,
    )
    if completed.returncode != 0:
        return []
    return [entry.decode("utf-8").replace("\\", "/") for entry in completed.stdout.split(b"\0") if entry]


def audit(root: Path, manifest_path: Path) -> dict:
    manifest = load_manifest(manifest_path)
    errors: list[str] = []
    components = manifest.get("components")
    coverage_roots = manifest.get("coverage_roots")
    if not isinstance(components, list) or not isinstance(coverage_roots, list):
        raise AuditError("components and coverage_roots must be arrays")

    ids: set[str] = set()
    patterns: list[tuple[str, str]] = []
    method_counts = {method: 0 for method in sorted(ALLOWED_METHODS)}
    for component in components:
        component_id = component.get("id")
        method = component.get("method")
        destinations = component.get("destination")
        if not isinstance(component_id, str) or not component_id:
            errors.append("component without a valid id")
            continue
        if component_id in ids:
            errors.append(f"duplicate component id: {component_id}")
        ids.add(component_id)
        if method not in ALLOWED_METHODS:
            errors.append(f"{component_id}: unknown method {method!r}")
        else:
            method_counts[method] += 1
        if not component.get("license"):
            errors.append(f"{component_id}: missing license")
        if not isinstance(component.get("copyright_holders"), list):
            errors.append(f"{component_id}: copyright_holders must be an array")
        if not isinstance(destinations, list) or not destinations:
            errors.append(f"{component_id}: destination must be a non-empty array")
        else:
            patterns.extend((component_id, pattern) for pattern in destinations)

        if method in {"selective-port", "derived-rewrite"}:
            commit = component.get("origin_commit")
            paths = component.get("origin_paths")
            if not isinstance(commit, str) or not git_object_exists(root, f"{commit}^{{commit}}"):
                errors.append(f"{component_id}: origin_commit is missing or unavailable")
            if not component.get("origin_repository"):
                errors.append(f"{component_id}: missing origin_repository")
            if not isinstance(paths, list) or not paths:
                errors.append(f"{component_id}: origin_paths must be a non-empty array")
            elif isinstance(commit, str):
                for path in paths:
                    if not git_object_exists(root, f"{commit}:{path}"):
                        errors.append(f"{component_id}: origin path does not exist: {path}")

    architecture_files = files_under(root, coverage_roots)
    matched_patterns: set[tuple[str, str]] = set()
    for relative in architecture_files:
        matching = [(component_id, pattern) for component_id, pattern in patterns
                    if fnmatch.fnmatchcase(relative, pattern)]
        owners = [component_id for component_id, _pattern in matching]
        matched_patterns.update(matching)
        if len(owners) != 1:
            errors.append(f"{relative}: expected exactly one provenance owner, found {owners}")
        if Path(relative).suffix.lower() in MEDIA_SUFFIXES:
            errors.append(f"{relative}: firmware or guest-media file is forbidden")
        try:
            prefix = (root / relative).read_text(encoding="utf-8", errors="ignore")[:4096]
        except OSError as exc:
            errors.append(f"{relative}: cannot inspect SPDX header: {exc}")
        else:
            if "SPDX-License-Identifier:" not in prefix:
                errors.append(f"{relative}: missing SPDX license identifier")

    for component_id, pattern in patterns:
        if (component_id, pattern) not in matched_patterns:
            errors.append(f"{component_id}: destination pattern matches no files: {pattern}")

    for relative in tracked_files(root):
        if Path(relative).suffix.lower() in MEDIA_SUFFIXES:
            errors.append(f"{relative}: tracked firmware or guest-media file is forbidden")

    dependencies = manifest.get("external_dependencies")
    if not isinstance(dependencies, list):
        errors.append("external_dependencies must be an array")
        dependencies = []
    for dependency in dependencies:
        if not dependency.get("id") or not dependency.get("version") or not dependency.get("license"):
            errors.append("each external dependency needs id, version and license")

    report = {
        "schema": manifest["schema"],
        "files": len(architecture_files),
        "components": len(components),
        "external_dependencies": len(dependencies),
        "methods": method_counts,
        "errors": errors,
    }
    return report


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--manifest", type=Path)
    parser.add_argument("--report", type=Path)
    args = parser.parse_args(argv)
    root = args.root.resolve()
    manifest_path = args.manifest.resolve() if args.manifest else root / "provenance" / "components.json"
    try:
        report = audit(root, manifest_path)
    except AuditError as exc:
        print(f"provenance audit failed: {exc}", file=sys.stderr)
        return 1
    rendered = json.dumps(report, indent=2, sort_keys=True)
    if args.report:
        args.report.write_text(rendered + "\n", encoding="utf-8")
    print(rendered)
    return 1 if report["errors"] else 0


if __name__ == "__main__":
    raise SystemExit(main())
