#!/usr/bin/env python3
"""Unit tests for the external Intel 8088 conformance adapter."""

from __future__ import annotations

import copy
from pathlib import Path
import unittest

from tools import run_8088_conformance as conformance


class Intel8088ConformanceTests(unittest.TestCase):
    def test_accepts_only_the_pinned_hardware_corpus_metadata(self) -> None:
        metadata = dict(conformance.EXPECTED_CORPUS)
        metadata["opcodes"] = {}
        conformance.validate_metadata(metadata, Path("metadata.json"))

        changed = copy.deepcopy(metadata)
        changed["cpu_detail"] = "unidentified part"
        with self.assertRaisesRegex(ValueError, "cpu_detail"):
            conformance.validate_metadata(changed, Path("metadata.json"))

    def test_metadata_group_selects_the_modrm_operation(self) -> None:
        metadata = {
            "opcodes": {
                "82": {"reg": {"0": {"status": "alias"}}}
            }
        }
        self.assertEqual(
            conformance.metadata_entry(metadata, "82.0"),
            {"status": "alias"},
        )

    def test_compare_masks_only_undefined_flag_bits(self) -> None:
        test = {
            "initial": {
                "regs": {
                    "ax": 1, "cx": 2, "dx": 3, "bx": 4,
                    "sp": 0x100, "bp": 5, "si": 6, "di": 7,
                    "es": 8, "cs": 9, "ss": 10, "ds": 11,
                    "ip": 12, "flags": 0xF002,
                },
                "ram": [],
                "queue": [],
            },
            "final": {"regs": {"ip": 13, "flags": 0xF012}, "ram": []},
        }
        request, expected_ram = conformance.encode_case(test)
        self.assertTrue(request.startswith("H "))
        response = (
            "H 0 1 0001 0002 0003 0004 0100 0005 0006 0007 "
            "0008 0009 000a 000b 000d f002 0 0 000d 0"
        )
        errors, queue_matches, read_matches = conformance.compare_case(
            test, response, expected_ram, 0xFFEF
        )
        self.assertEqual(errors, [])
        self.assertTrue(queue_matches)
        self.assertTrue(read_matches)

        changed = copy.deepcopy(test)
        changed["final"]["queue"] = [0x90]
        errors, queue_matches, read_matches = conformance.compare_case(
            changed, response, expected_ram, 0xFFEF,
            require_raw_final_queue=True,
        )
        self.assertFalse(queue_matches)
        self.assertTrue(read_matches)
        self.assertTrue(any(error.startswith("queue=") for error in errors))

    def test_queue_reads_use_hardware_f_and_s_without_electrical_timing(self) -> None:
        test = {
            "initial": {"regs": {
                "ax": 1, "cx": 2, "dx": 3, "bx": 4,
                "sp": 0x100, "bp": 5, "si": 6, "di": 7,
                "es": 8, "cs": 9, "ss": 10, "ds": 11,
                "ip": 12, "flags": 0xF002,
            }, "ram": [], "queue": []},
            "final": {"regs": {"ip": 14}, "ram": [], "queue": []},
            "cycles": [
                [0, 0, "--", "---", "---", 0, 0, "PASV", "Ti", "F", 0x90],
                [0, 0, "--", "---", "---", 0, 0, "PASV", "Ti", "-", 0],
                [0, 0, "--", "---", "---", 0, 0, "PASV", "Ti", "S", 0x34],
            ],
        }
        self.assertEqual(conformance.hardware_queue_reads(test),
                         [("F", 0x90), ("S", 0x34)])
        response = (
            "H 0 1 0001 0002 0003 0004 0100 0005 0006 0007 "
            "0008 0009 000a 000b 000e f002 0 0 000e 2 F 90 S 34"
        )
        errors, _, reads_match = conformance.compare_case(
            test, response, [], 0xFFFF, require_queue_reads=True
        )
        self.assertEqual(errors, [])
        self.assertTrue(reads_match)

        wrong = response.replace("S 34", "S 35")
        errors, _, reads_match = conformance.compare_case(
            test, wrong, [], 0xFFFF, require_queue_reads=True
        )
        self.assertFalse(reads_match)
        self.assertTrue(any(error.startswith("queue_reads=") for error in errors))


if __name__ == "__main__":
    unittest.main()
