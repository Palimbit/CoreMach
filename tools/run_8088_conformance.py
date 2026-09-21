#!/usr/bin/env python3
"""Run physical SingleStepTests/8088 V2 vectors against the portable core.

The hardware corpus remains outside BluMach.  Check out the exact revision
documented below and pass its ``v2`` directory explicitly. This first gate
checks architectural register and memory results with both empty and preloaded
prefetch queues. The raw final queue is measured for diagnostics, but the
corpus ends on the next instruction's first-byte queue event while the runner
returns at the current instruction boundary. Physical cycle traces therefore
remain deliberately unverified until the active bus-phase observations are
joined with an Intel EU/BIU schedule and a boundary-aligned trace adapter.
Logical F/S queue-read order can already be compared without inferring a
CPU-cycle position from those events.
Observed operand bus transfers can likewise be compared for ordering,
address and data, without claiming their placement in the CPU timeline.

Expected corpus:
  repository: https://github.com/SingleStepTests/8088
  commit:     aea84484abc79d09639d855b7b0ab32bc9e4dbeb
  metadata:   version 2.0.0, syntax version 2, AMD D8088 8441DMA
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
EXPECTED_CORPUS = {
    "url": "https://github.com/SingleStepTests/8088/",
    "version": "2.0.0",
    "syntax_version": 2,
    "cpu": "8088",
    "cpu_detail": "AMD D8088 8441DMA (C)1982",
    "generator": "arduino8088",
}


def metadata_entry(metadata: dict, opcode: str) -> dict:
    stem = opcode.upper()
    group = None
    if "." in stem:
        stem, group = stem.split(".", 1)
    entry = metadata["opcodes"][stem]
    if group is not None:
        entry = entry["reg"][group]
    return entry


def validate_metadata(metadata: dict, path: Path) -> None:
    mismatches = []
    for field, expected in EXPECTED_CORPUS.items():
        actual = metadata.get(field)
        if actual != expected:
            mismatches.append(f"{field}={actual!r}, expected {expected!r}")
    if mismatches:
        raise ValueError(f"unsupported corpus metadata in {path}: " +
                         "; ".join(mismatches))


def expected_registers(test: dict) -> dict[str, int]:
    result = dict(test["initial"]["regs"])
    result.update(test["final"]["regs"])
    return result


def encode_case(test: dict) -> tuple[str, list[list[int]]]:
    registers = test["initial"]["regs"]
    initial_ram = test["initial"].get("ram", [])
    initial_queue = test["initial"].get("queue", [])
    final_ram = test["final"].get("ram", [])
    fields = ["H"]
    fields.extend(f"{registers[name] & 0xffff:x}" for name in REGISTER_ORDER)
    fields.append(str(len(initial_ram)))
    for address, value in initial_ram:
        fields.extend((f"{address:x}", f"{value:x}"))
    fields.append(str(len(initial_queue)))
    fields.extend(f"{value:x}" for value in initial_queue)
    fields.append(str(len(final_ram)))
    fields.extend(f"{address:x}" for address, _ in final_ram)
    return " ".join(fields) + "\n", final_ram


def hardware_queue_reads(test: dict) -> list[tuple[str, int]]:
    """Extract only logical reads; QS status is sampled one cycle later."""
    return [
        (cycle[9], cycle[10]) for cycle in test.get("cycles", [])
        if cycle[9] in ("F", "S")
    ]


def hardware_operand_transfers(test: dict) -> list[tuple[str, int, int]]:
    """Decode observed transfers from T1 address and T3/last-Tw data.

    The corpus may stop after T3 of its final write, before T4. That byte was
    already transferred and must not be discarded merely because T4 is absent.
    """
    kinds = {"MEMR": "R", "MEMW": "W", "IOR": "I", "IOW": "O"}
    result: list[tuple[str, int, int]] = []
    pending: tuple[str, int, int | None] | None = None
    for cycle in test.get("cycles", []):
        phase = cycle[8]
        if phase == "T1":
            if pending is not None and pending[2] is not None:
                result.append((pending[0], pending[1], pending[2]))
            kind = kinds.get(cycle[7])
            address = cycle[1] & (0xffff if kind in ("I", "O") else 0xfffff)
            pending = (kind, address, None) if kind else None
        elif pending is not None:
            if phase == "T3" or phase == "Tw":
                pending = (pending[0], pending[1], cycle[6])
            elif phase == "T4":
                if pending[2] is not None:
                    result.append((pending[0], pending[1], pending[2]))
                pending = None
    if pending is not None and pending[2] is not None:
        result.append((pending[0], pending[1], pending[2]))
    return result


def compare_case(test: dict, response: str, expected_ram: list[list[int]],
                 flags_mask: int, require_raw_final_queue: bool = False,
                 require_queue_reads: bool = False,
                 require_operand_bus: bool = False
                 ) -> tuple[list[str], bool, bool, bool]:
    fields = response.split()
    if len(fields) < 20 or fields[0] != "H":
        return [f"invalid runner response: {response!r}"], False, False, False
    status = int(fields[1], 10)
    consumed = int(fields[2], 10)
    actual = {
        name: int(value, 16)
        for name, value in zip(REGISTER_ORDER, fields[3:17], strict=True)
    }
    query_count = int(fields[17], 10)
    ram_end = 18 + query_count
    if len(fields) < ram_end + 2:
        return [f"truncated runner response: {response!r}"], False, False, False
    actual_ram = [int(value, 16) for value in fields[18:ram_end]]
    queue_count = int(fields[ram_end], 10)
    queue_end = ram_end + 1 + queue_count
    if len(fields) < queue_end + 2:
        return [f"invalid queue response: {response!r}"], False, False, False
    actual_queue = [int(value, 16) for value in fields[ram_end + 1:queue_end]]
    actual_prefetch_pointer = int(fields[queue_end], 16)
    event_count = int(fields[queue_end + 1], 10)
    event_end = queue_end + 2 + 2 * event_count
    event_fields = fields[queue_end + 2:event_end]
    if len(fields) < event_end + 1:
        return [f"invalid queue event response: {response!r}"], False, False, False
    actual_events = [
        (event_fields[index], int(event_fields[index + 1], 16))
        for index in range(0, len(event_fields), 2)
    ]
    if any(kind not in ("F", "S", "E") for kind, _ in actual_events):
        return [f"unknown queue event in: {response!r}"], False, False, False
    transfer_count = int(fields[event_end], 10)
    transfer_fields = fields[event_end + 1:]
    if len(transfer_fields) != 3 * transfer_count:
        return [f"invalid bus transfer response: {response!r}"], False, False, False
    actual_transfers = [
        (transfer_fields[index], int(transfer_fields[index + 1], 16),
         int(transfer_fields[index + 2], 16))
        for index in range(0, len(transfer_fields), 3)
    ]
    if any(kind not in ("R", "W", "I", "O") for kind, _, _ in actual_transfers):
        return [f"unknown bus transfer in: {response!r}"], False, False, False
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
    expected_ram_values = [value for _, value in expected_ram]
    ram_matches = query_count == len(expected_ram)
    final = expected_registers(test)
    interrupt_stack = (
        final["sp"] == ((test["initial"]["regs"]["sp"] - 6) & 0xffff)
    )
    saved_flags_address = (
        ((final["ss"] << 4) + ((final["sp"] + 4) & 0xffff)) & 0xfffff
    )
    if ram_matches:
        for actual_value, (address, expected_value) in zip(
                actual_ram, expected_ram, strict=True):
            byte_mask = 0xff
            if interrupt_stack and address == saved_flags_address:
                byte_mask = flags_mask & 0xff
            elif interrupt_stack and address == ((saved_flags_address + 1) & 0xfffff):
                byte_mask = (flags_mask >> 8) & 0xff
            if (actual_value & byte_mask) != (expected_value & byte_mask):
                ram_matches = False
                break
    if not ram_matches:
        errors.append(f"ram={actual_ram}, expected={expected_ram_values}")
    expected_queue = test["final"].get("queue", [])
    queue_matches = actual_queue == expected_queue
    if require_raw_final_queue and not queue_matches:
        errors.append(
            f"queue={actual_queue}, expected={expected_queue}, "
            f"prefetch_pointer={actual_prefetch_pointer:04X}"
        )
    expected_reads = hardware_queue_reads(test)
    actual_reads = [event for event in actual_events if event[0] in ("F", "S")]
    reads_match = actual_reads == expected_reads
    if require_queue_reads and not reads_match:
        errors.append(f"queue_reads={actual_reads}, expected={expected_reads}")
    expected_transfers = hardware_operand_transfers(test)
    transfers_match = actual_transfers == expected_transfers
    if require_operand_bus and not transfers_match:
        errors.append(
            f"operand_bus={actual_transfers}, expected={expected_transfers}"
        )
    return errors, queue_matches, reads_match, transfers_match


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--suite", type=Path, required=True,
                        help="Pinned SingleStepTests/8088 v2 directory")
    parser.add_argument("--runner", type=Path, required=True,
                        help="portable-engine-808x-vector-runner executable")
    parser.add_argument("--opcode", action="append", required=True,
                        help="Corpus opcode stem such as 04, 82.0 or F6.0")
    parser.add_argument("--limit", type=int, default=0,
                        help="Maximum eligible vectors per opcode; zero means all")
    parser.add_argument("--max-failures", type=int, default=20)
    parser.add_argument(
        "--require-raw-final-queue", action="store_true",
        help=("Fail raw final-queue differences despite the known "
              "core/corpus instruction-boundary mismatch"),
    )
    parser.add_argument(
        "--require-queue-reads", action="store_true",
        help="Fail when logical F/S queue reads differ from hardware traces",
    )
    parser.add_argument(
        "--require-operand-bus", action="store_true",
        help="Fail when observed operand transfers differ from hardware traces",
    )
    args = parser.parse_args()

    metadata_path = args.suite / "metadata.json"
    if not metadata_path.is_file():
        parser.error(f"missing {metadata_path}")
    if not args.runner.is_file():
        parser.error(f"missing {args.runner}")
    metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
    try:
        validate_metadata(metadata, metadata_path)
    except ValueError as error:
        parser.error(str(error))

    process = subprocess.Popen(
        [str(args.runner), "--model", "intel-8088"],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE,
        text=True, bufsize=1,
    )
    assert process.stdin is not None and process.stdout is not None
    total = 0
    prefetched = 0
    queue_matches = 0
    queue_read_matches = 0
    operand_bus_matches = 0
    operand_bus_cases = 0
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
            prefetched += sum(
                bool(test["initial"].get("queue", [])) for test in vectors
            )
            opcode_failures = 0
            for test in vectors:
                request, expected_ram = encode_case(test)
                process.stdin.write(request)
                process.stdin.flush()
                response = process.stdout.readline()
                if not response:
                    raise RuntimeError("vector runner terminated unexpectedly")
                errors, queue_match, read_match, bus_match = compare_case(
                    test, response, expected_ram, flags_mask,
                    args.require_raw_final_queue,
                    args.require_queue_reads,
                    args.require_operand_bus,
                )
                total += 1
                queue_matches += int(queue_match)
                queue_read_matches += int(read_match)
                operand_bus_matches += int(bus_match)
                operand_bus_cases += bool(hardware_operand_transfers(test))
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
    print(
        f"SUMMARY vectors={total} failures={failures} "
        f"prefetched={prefetched} raw_queue_matches={queue_matches}/{total} "
        f"queue_read_matches={queue_read_matches}/{total} "
        f"operand_bus_matches={operand_bus_matches}/{total} "
        f"operand_bus_cases={operand_bus_cases} "
        "cycle_traces=not-yet-compared"
    )
    if return_code != 0:
        print(f"runner exit={return_code}", file=sys.stderr)
        return 2
    return 1 if failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
