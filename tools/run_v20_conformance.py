#!/usr/bin/env python3
"""Run external SingleStepTests/V20 vectors against the portable V30 core.

The corpus is not downloaded or copied into BluMach. Pin it separately to the
revision documented by the caller and pass its v1_native directory explicitly.
Cycle and queue traces are intentionally ignored: V20 architectural results
are useful for the shared native ISA, while its 8-bit bus is not V30 timing
evidence.
"""

from __future__ import annotations

import argparse
import gzip
import json
from pathlib import Path
import subprocess
import sys


REGISTER_ORDER = (
    "ax", "cx", "dx", "bx", "sp", "bp", "si", "di",
    "es", "cs", "ss", "ds", "ip", "flags",
)


def metadata_entry(metadata: dict, opcode: str) -> dict:
    stem = opcode.upper()
    group = None
    if "." in stem:
        stem, group = stem.split(".", 1)
    entry = metadata["opcodes"][stem]
    if group is not None:
        entry = entry["reg"][group]
    return entry


def expected_registers(test: dict) -> dict[str, int]:
    result = dict(test["initial"]["regs"])
    result.update(test["final"]["regs"])
    return result


def encode_case(test: dict) -> tuple[str, list[int]]:
    registers = test["initial"]["regs"]
    initial_ram = test["initial"].get("ram", [])
    final_ram = test["final"].get("ram", [])
    fields = ["C"]
    fields.extend(f"{registers[name] & 0xffff:x}" for name in REGISTER_ORDER)
    fields.append(str(len(initial_ram)))
    for address, value in initial_ram:
        fields.extend((f"{address:x}", f"{value:x}"))
    fields.append(str(len(final_ram)))
    fields.extend(f"{address:x}" for address, _ in final_ram)
    return " ".join(fields) + "\n", [value for _, value in final_ram]


def compare_case(test: dict, response: str, expected_ram: list[int],
                 flags_mask: int) -> list[str]:
    fields = response.split()
    if len(fields) < 18 or fields[0] != "R":
        return [f"invalid runner response: {response!r}"]
    status = int(fields[1], 10)
    consumed = int(fields[2], 10)
    actual = {
        name: int(value, 16)
        for name, value in zip(REGISTER_ORDER, fields[3:17], strict=True)
    }
    query_count = int(fields[17], 10)
    actual_ram = [int(value, 16) for value in fields[18:]]
    errors: list[str] = []
    if status != 0:
        errors.append(f"status={status}")
    if consumed != 1:
        errors.append(f"consumed={consumed}")
    expected = expected_registers(test)
    for name in REGISTER_ORDER:
        mask = flags_mask if name == "flags" else 0xffff
        if (actual[name] & mask) != (expected[name] & mask):
            errors.append(
                f"{name}={actual[name]:04X}, expected={expected[name]:04X}, "
                f"mask={mask:04X}"
            )
    if query_count != len(expected_ram) or actual_ram != expected_ram:
        errors.append(f"ram={actual_ram}, expected={expected_ram}")
    return errors


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", type=Path, required=True,
                        help="Pinned SingleStepTests/v20 v1_native directory")
    parser.add_argument("--runner", type=Path, required=True,
                        help="portable-engine-808x-vector-runner executable")
    parser.add_argument("--opcode", action="append", required=True,
                        help="Corpus opcode stem such as D4, D5 or F6.0")
    parser.add_argument("--limit", type=int, default=0,
                        help="Maximum vectors per opcode; zero means all")
    parser.add_argument("--max-failures", type=int, default=20)
    args = parser.parse_args()

    metadata_path = args.suite / "metadata.json"
    if not metadata_path.is_file():
        parser.error(f"missing {metadata_path}")
    if not args.runner.is_file():
        parser.error(f"missing {args.runner}")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    process = subprocess.Popen(
        [str(args.runner)], stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True, bufsize=1,
    )
    assert process.stdin is not None and process.stdout is not None
    total = 0
    failures = 0
    try:
        for opcode in args.opcode:
            vector_path = args.suite / f"{opcode.upper()}.json.gz"
            if not vector_path.is_file():
                parser.error(f"missing {vector_path}")
            entry = metadata_entry(metadata, opcode)
            flags_mask = int(entry.get("flags-mask", 0xffff))
            with gzip.open(vector_path, "rt", encoding="utf-8") as stream:
                vectors = json.load(stream)
            if args.limit > 0:
                vectors = vectors[:args.limit]
            opcode_failures = 0
            for test in vectors:
                request, expected_ram = encode_case(test)
                process.stdin.write(request)
                process.stdin.flush()
                response = process.stdout.readline()
                if not response:
                    raise RuntimeError("vector runner terminated unexpectedly")
                errors = compare_case(test, response, expected_ram, flags_mask)
                total += 1
                if errors:
                    failures += 1
                    opcode_failures += 1
                    if failures <= args.max_failures:
                        print(
                            f"FAIL {opcode} #{test.get('idx', '?')} "
                            f"{test.get('name', '')}: {'; '.join(errors)}",
                            file=sys.stderr,
                        )
            print(
                f"{opcode.upper()}: vectors={len(vectors)} "
                f"failures={opcode_failures} flags_mask={flags_mask:04X}"
            )
    finally:
        if process.poll() is None:
            process.stdin.write("Q\n")
            process.stdin.flush()
        process.stdin.close()
        process.stdout.close()
        return_code = process.wait()
    print(f"SUMMARY vectors={total} failures={failures}")
    if return_code != 0:
        print(f"runner exit={return_code}", file=sys.stderr)
        return 2
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
