# SPDX-License-Identifier: GPL-2.0-or-later
"""CLI composition regressions with authored bytes, never preserved firmware."""
import json
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.environ.get("BM_PCS286_BOOT_PROBE"), "build the local boot probe")
class BootProbeTests(unittest.TestCase):
    def run_probe(self, program, *options, size=131072):
        image = bytearray(size)
        if size == 131072:
            image[0x10000:0x10000 + len(program)] = program
            image[0x1FFF0:0x1FFF5] = bytes.fromhex("ea000000f0")
        with tempfile.TemporaryDirectory(prefix="blumach-authored-probe-") as directory:
            bios = Path(directory) / "authored.bin"
            bios.write_bytes(image)
            result = subprocess.run(
                [os.environ["BM_PCS286_BOOT_PROBE"], "--bios", str(bios),
                 "--steps", "20", "--peripheral-ns", "1000", *options],
                capture_output=True, text=True, timeout=15)
            self.assertEqual(bios.read_bytes(), image, "input must remain immutable")
        events = [json.loads(line) for line in result.stdout.splitlines()]
        return result, events

    def test_reset_post_and_idle_budget(self):
        result, events = self.run_probe(bytes.fromhex("b012e680faf4"))
        self.assertEqual(result.returncode, 2, result.stderr)
        summary = events[-1]
        self.assertEqual(summary["reason"], "budget")
        self.assertEqual(summary["first_fetch"], 0xFFFFF0)
        self.assertEqual(summary["last_post"], 0x12)
        self.assertEqual(summary["post_count"], 1)
        self.assertEqual(summary["halted"], 1)
        self.assertEqual(summary["steps"], 20)
        self.assertFalse(summary["boot_claim"])
        self.assertEqual([e["pc"] for e in events if e["event"] == "cpu"][0], 0xFFFFF0)

    def test_checkpoint_port_retains_last_write(self):
        result, events = self.run_probe(bytes.fromhex("b05ae680e480e680faf4"))
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual([e["value"] for e in events if e["event"] == "post"],
                         [0x5A, 0x5A])
        self.assertEqual(events[-1]["last_post"], 0x5A)
        self.assertEqual(events[-1]["post_count"], 2)

    def test_unmapped_io_is_not_retried(self):
        result, events = self.run_probe(bytes.fromhex("ba3412ece680f4"))
        self.assertEqual(result.returncode, 1)
        summary = events[-1]
        self.assertEqual(summary["reason"], "step_error")
        self.assertEqual(summary["failed_address"], 0x1234)
        self.assertEqual(summary["failed_status"], -5)
        self.assertEqual(summary["io_calls"], 1)
        self.assertEqual(summary["post_count"], 0)
        self.assertEqual(summary["ip"], 3)
        result, events = self.run_probe(bytes.fromhex("ba3412ece680faf4"), "--io-holes", "ff")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(events[-1]["last_post"], 255)

    def test_installed_device_error_not_changed_to_ff(self):
        # RTC register A: unsupported divider encoding. Same status through
        # either unclaimed-I/O policy, no later POST or repeated device write.
        for holes in ("reject", "ff"):
            result, events = self.run_probe(bytes.fromhex("b00ae670b010e671e680f4"),
                                            "--io-holes", holes, "--rtc-divider", "strict")
            self.assertEqual(result.returncode, 1)
            self.assertEqual(events[-1]["status"], -8)
            self.assertEqual(events[-1]["failed_address"], 0x71)
            self.assertEqual(events[-1]["io_calls"], 2)
            self.assertEqual(events[-1]["post_count"], 0)

    def test_loop_budget_and_bounded_trace(self):
        result, events = self.run_probe(bytes.fromhex("ebfe"), "--steps", "100")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(events[-1]["steps"], 100)
        self.assertEqual(events[-1]["boundaries"], 100)
        self.assertEqual(len([e for e in events if e["event"] == "cpu"]), 64)
        self.assertEqual(events[-1]["ip"], 0)

    def test_bad_input_and_options(self):
        for size in (0, 131071, 131073):
            result, events = self.run_probe(b"", size=size)
            self.assertEqual(result.returncode, 3)
            self.assertFalse(events)

    def test_external_cmos_is_exact_and_read_only(self):
        with tempfile.TemporaryDirectory(prefix="blumach-authored-cmos-") as directory:
            cmos = Path(directory) / "cmos.bin"
            configured = bytearray(128)
            configured[4] = configured[7] = configured[8] = 1
            configured[6], configured[9] = 3, 0x80
            configured[10], configured[11] = 0x60, 0x82
            image = bytes(configured)
            cmos.write_bytes(image)
            result, events = self.run_probe(b"\xf4", "--cmos-file", str(cmos))
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertEqual(events[0]["cmos"], "external-128-byte-image")
            self.assertEqual(cmos.read_bytes(), image)
            result, events = self.run_probe(b"\xf4", "--cmos-file", str(cmos),
                                            "--cmos", "initialized")
            self.assertEqual(result.returncode, 3)
            self.assertFalse(events)
            cmos.write_bytes(image[:-1])
            result, events = self.run_probe(b"\xf4", "--cmos-file", str(cmos))
            self.assertEqual(result.returncode, 3)
            self.assertFalse(events)
        for options in (("--steps", "-1"), ("--steps", "0"),
                        ("--ram-mib", "5"), ("--peripheral-ns", "oops"),
                        ("--io-holes", "guess"), ("--rtc-divider", "guess"),
                        ("--video", "guess"), ("--cmos", "guess")):
            result, events = self.run_probe(b"", *options)
            self.assertEqual(result.returncode, 3)
            self.assertFalse(events)

    def test_existing_parallel_port(self):
        result, events = self.run_probe(bytes.fromhex("ba7803b05aeeece680faf4"))
        self.assertEqual(result.returncode, 2)
        self.assertEqual(events[-1]["last_post"], 0x5A)
        self.assertEqual([e["value"] for e in events if e["event"] == "lpt_data"], [0x5A])
        self.assertEqual(events[-1]["peripheral_ns"], 20000)

    def test_wd37c65_reset_releases_to_idle_msr(self):
        # Assert/release the standard AT DOR reset and report MSR. A missing
        # endpoint under FF-hole policy would report FFh instead of idle 80h.
        program = bytes.fromhex("baf20332c0eeb00ceebaf403ece680faf4")
        for holes in ("reject", "ff"):
            result, events = self.run_probe(program, "--io-holes", holes)
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertEqual(events[0]["fdc"], "wd37c65-functional-no-drive")
            self.assertTrue(events[0]["dma_service"])
            self.assertEqual([e["value"] for e in events if e["event"] == "post"],
                             [0x80])
            self.assertEqual(events[-1]["failed_status"], 0)
            self.assertEqual(events[-1]["dma_units"], 0)

    def test_read_only_1440k_floppy_attachment(self):
        image = bytearray(131072)
        image[0x10000:0x10006] = bytes.fromhex("b05ae680faf4")
        image[0x1FFF0:0x1FFF5] = bytes.fromhex("ea000000f0")
        media = bytes(range(256)) * 5760
        with tempfile.TemporaryDirectory(prefix="blumach-authored-floppy-") as directory:
            bios = Path(directory) / "authored.bin"
            floppy = Path(directory) / "authored.img"
            bios.write_bytes(image); floppy.write_bytes(media)
            result = subprocess.run(
                [os.environ["BM_PCS286_BOOT_PROBE"], "--bios", str(bios),
                 "--floppy", str(floppy), "--steps", "20"],
                capture_output=True, text=True, timeout=15)
            self.assertEqual(floppy.read_bytes(), media)
        self.assertEqual(result.returncode, 2, result.stderr)
        events = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual(events[0]["fdc"], "wd37c65-functional-drive0-read-only")
        self.assertTrue(events[0]["storage"])
        self.assertEqual(events[-1]["dma_units"], 0)

    def test_floppy_attachment_rejects_wrong_size(self):
        image = bytearray(131072)
        image[0x1FFF0:0x1FFF5] = bytes.fromhex("ea000000f0")
        with tempfile.TemporaryDirectory(prefix="blumach-authored-floppy-size-") as directory:
            bios = Path(directory) / "authored.bin"
            floppy = Path(directory) / "short.img"
            bios.write_bytes(image); floppy.write_bytes(bytes(512))
            result = subprocess.run(
                [os.environ["BM_PCS286_BOOT_PROBE"], "--bios", str(bios),
                 "--floppy", str(floppy), "--steps", "20"],
                capture_output=True, text=True, timeout=15)
        self.assertEqual(result.returncode, 3)
        self.assertIn("exactly 1474560 bytes", result.stderr)

    def test_classic_rtc_divider_is_retained(self):
        result, events = self.run_probe(bytes.fromhex("b00ae670b000e671e471e680faf4"))
        self.assertEqual(result.returncode, 2)
        self.assertEqual(events[-1]["last_post"], 0)
        self.assertEqual(events[0]["rtc_divider"], "classic-stop")
        # Daylight saving remains unsupported, even with the functional divider policy.
        result, events = self.run_probe(bytes.fromhex("b00be670b003e671e680f4"), "--io-holes", "ff")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(events[-1]["failed_status"], -8)
        self.assertEqual(events[-1]["post_count"], 0)

    def test_empty_keyboard_output_read(self):
        result, events = self.run_probe(bytes.fromhex("e460e680faf4"))
        self.assertEqual(result.returncode, 2)
        self.assertEqual(events[-1]["last_post"], 0)
        self.assertEqual(events[-1]["failed_status"], 0)

    def test_olivetti_cf_has_no_reply(self):
        # CF, poll IBF until consumed, report status then the retained empty latch.
        result, events = self.run_probe(bytes.fromhex("b0cfe664e464a80275fae680e460e680faf4"),
                                        "--steps", "100")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(events[0]["kbc_commands"], "olivetti-pcs286-classic-p2-cf")
        self.assertEqual([e["value"] for e in events if e["event"] == "post"], [8, 0])
        self.assertEqual(events[-1]["failed_status"], 0)
        # The PCS286S-specific 8B command is still outside this profile.
        result, events = self.run_probe(bytes.fromhex("b08be664e680faf4"), "--io-holes", "ff")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(events[-1]["failed_address"], 0x64)
        self.assertEqual(events[-1]["failed_status"], -8)
        self.assertEqual(events[-1]["post_count"], 0)

    def test_olivetti_p2_roundtrip_does_not_reset_cpu(self):
        # 84/write all-zero latch; 80/read response after polling, then HALT.
        # Bit0 clear must not generate CPU reset; bit7 clear is not serial drive.
        result, events = self.run_probe(bytes.fromhex(
            "b084e664e464a80275fab000e660e464a80275fa"
            "b080e664e464a80174fae460e680faf4"), "--steps", "100")
        self.assertEqual(result.returncode, 2)
        self.assertEqual(events[-1]["post_count"], 1)
        self.assertEqual(events[-1]["last_post"], 0)
        self.assertEqual(events[-1]["halted"], 1)
        self.assertEqual(events[-1]["failed_status"], 0)

    def test_absent_auxiliary_device_has_no_fabricated_reply(self):
        # Enable the second KBC channel, direct reset FFh through D4, observe
        # empty OBF, then disable it. The board has no mouse endpoint attached.
        program = bytes.fromhex(
            "b0a8e664e464a80275fa"
            "b0d4e664e464a80275fa"
            "b0ffe660e464a80275fae464e680"
            "b0a7e664e464a80275fae464e680faf4")
        result, events = self.run_probe(program, "--steps", "100")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual([e["value"] for e in events if e["event"] == "post"],
                         [0x00, 0x08])
        self.assertEqual(events[-1]["failed_status"], 0)
        self.assertEqual(events[-1]["halted"], 1)

    def test_full_kbc_command_byte_is_accepted(self):
        # BIOS-observed 47h includes reserved bit1. Component coverage proves
        # readback; this composition case proves it is not a host failure.
        program = bytes.fromhex(
            "b060e664e464a80275fab047e660e464a80275fa"
            "b047e680faf4")
        result, events = self.run_probe(program, "--steps", "100")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual([e["value"] for e in events if e["event"] == "post"], [0x47])
        self.assertEqual(events[-1]["failed_status"], 0)
        self.assertEqual(events[-1]["halted"], 1)

    def test_invalid_rtc_calendar_is_reported_without_repair(self):
        # Depleted zero calendar, B=02 releases SET, A=20 runs oscillator;
        # HALT lets the first calendar update expose the invalid date.
        result, events = self.run_probe(bytes.fromhex("b00be670b002e671b00ae670b020e671faf4"),
                                        "--steps", "100", "--peripheral-ns", "10000000")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(events[-1]["reason"], "peripheral_error")
        self.assertEqual(events[-1]["status"], -8)
        rtc = next(e for e in events if e["event"] == "rtc_snapshot")
        self.assertEqual(rtc["phase"], 16449)
        self.assertEqual(rtc["updating"], 0)
        self.assertEqual(rtc["registers"][:10], [0] * 10)
        self.assertEqual(rtc["registers"][10:12], [0x20, 2])


    def test_initialized_calendar_runs_without_repair(self):
        # Release SET into 12h mode, enable oscillator and wait through updates.
        program = bytes.fromhex("b00be670b000e671b00ae670b020e671faf4")
        result, events = self.run_probe(program, "--cmos", "initialized",
                                       "--steps", "200", "--peripheral-ns", "10000000")
        self.assertEqual(result.returncode, 2, result.stderr)
        rtc = next(e for e in events if e["event"] == "rtc_snapshot")
        self.assertGreater(rtc["registers"][0], 0)
        self.assertEqual(rtc["registers"][4:10], [1, 0, 3, 1, 1, 0x80])
        self.assertEqual(events[-1]["peripheral_ns"], 2000000000)
        # Guest writes invalid month: initialized policy cannot repair it.
        result, events = self.run_probe(bytes.fromhex("b008e670b000e671") + program,
                                       "--cmos", "initialized", "--io-holes", "ff",
                                       "--steps", "200", "--peripheral-ns", "10000000")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(events[-1]["reason"], "peripheral_error")

    def test_scheduled_keys_use_keyboard_translation_and_release(self):
        # Enable set1 translation; drain power-on BAT, then read F1 make/break.
        program = bytes.fromhex(
            "b060e664e464a80275fab040e660"
            "e464a80174fae460e680"
            "e464a80174fae460e680"
            "e464a80174fae460e680faf4")
        result, events = self.run_probe(program, "--steps", "500",
                                       "--key", "100:F1:down", "--key", "200:F1:up")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual([e["value"] for e in events if e["event"] == "post"], [0xaa, 0x3b, 0xbb])
        keys = [e for e in events if e["event"] == "key"]
        self.assertEqual([(e["step"], e["pressed"], e["status"]) for e in keys], [(100, 1, 0), (200, 0, 0)])
        reads = [e["value"] for e in events if e["event"] == "kbc_io" and e["port"] == 0x60 and e["op"] == 0]
        self.assertEqual(reads, [0xaa, 0x3b, 0xbb])
        self.assertEqual(events[-1]["halted"], 1)

    def test_frame_export_uses_pixels_without_advancing_guest(self):
        def out(port, value):
            return b"\xba" + port.to_bytes(2, "little") + bytes([0xb0, value, 0xee])
        def indexed(port, index, value):
            return out(port, index) + out(port + 1, value)
        # Authored 8x1 planar mode: first pixel red, remaining pixels black.
        program = (indexed(0x3c4, 1, 1) + indexed(0x3c4, 2, 1) +
                   indexed(0x3d4, 1, 0) + indexed(0x3d4, 0x12, 0) +
                   indexed(0x3d4, 0x17, 0x80) +
                   indexed(0x3ce, 6, 5) +
                   bytes.fromhex("bada03ec") + out(0x3c0, 1) + out(0x3c0, 1) +
                   out(0x3c0, 0x12) + out(0x3c0, 0x0f) + out(0x3c0, 0x20) +
                   out(0x3c8, 1) + out(0x3c9, 63) + out(0x3c9, 0) + out(0x3c9, 0) +
                   bytes.fromhex("b800a08ed8c606000080faf4"))
        baseline, base_events = self.run_probe(program, "--video", "pvga1a", "--steps", "300")
        self.assertEqual(baseline.returncode, 2)
        with tempfile.TemporaryDirectory() as directory:
            first, final = Path(directory) / "before.ppm", Path(directory) / "final.ppm"
            result, events = self.run_probe(program, "--video", "pvga1a", "--steps", "300",
                                           "--capture", f"200:{first}", "--frame", str(final))
            self.assertEqual(result.returncode, 2, result.stderr)
            self.assertEqual(events[-1], base_events[-1])
            expected = b"P6\n8 1\n255\n" + b"\xff\x00\x00" + b"\x00" * 21
            self.assertEqual(first.read_bytes(), expected)
            self.assertEqual(final.read_bytes(), expected)
            self.assertTrue(any(e["event"] == "video_status_io" for e in events))
            # Exclusive export cannot truncate any existing file, including BIOS.
            result, events = self.run_probe(program, "--video", "pvga1a", "--frame", str(final))
            self.assertEqual(result.returncode, 3)
            self.assertEqual(final.read_bytes(), expected)
            result, events = self.run_probe(program, "--video", "pvga1a",
                                           "--capture", f"2:{final}")
            self.assertEqual(result.returncode, 3)
            self.assertEqual(events[-1]["reason"], "capture_error")
            self.assertEqual(final.read_bytes(), expected)

    def test_action_options_are_validated_before_execution(self):
        for options in (("--frame", "x.ppm"), ("--capture", "1:x.ppm"),
                        ("--key", "0:F1:down"), ("--key", "20:F1:down"),
                        ("--key", "2:F11:down"), ("--key", "2:F1:tap"),
                        ("--key", "2:UNKNOWN:down"), ("--key", "oops:F1:up"),
                        ("--video", "pvga1a", "--capture", "2:"),
                        tuple(["--key", "2:A:down"] * 33)):
            result, events = self.run_probe(b"\xf4", *options)
            self.assertEqual(result.returncode, 3, options)
            self.assertFalse(events, options)

    def test_keyboard_reset_after_disable_uses_real_ack_and_bat(self):
        # AD, wait for IBF clear, FF without AE; receive actual keyboard ACK/BAT.
        result, events = self.run_probe(bytes.fromhex(
            "b0ade664e464a80275fab0ffe660"
            "e464a80174fae460e680e464a80174fae460e680faf4"), "--steps", "200")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual([e["value"] for e in events if e["event"] == "post"], [0xfa, 0xaa])
        self.assertEqual(events[-1]["halted"], 1)
        self.assertEqual(events[-1]["failed_status"], 0)

    def test_video_memory_and_indexed_io(self):
        # Planar reset mapping: write/read VRAM, then indexed CRTC readback.
        result, events = self.run_probe(bytes.fromhex(
            "b800a08ed8c60600005aa00000e680"
            "bad403b00eeebad503b033eeece680faf4"),
            "--video", "pvga1a", "--steps", "40")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(events[0]["video"], "pvga1a-clocked")
        self.assertEqual([e["value"] for e in events if e["event"] == "post"], [0x5a, 0x33])
        self.assertEqual(events[-1]["failed_status"], 0)

    def test_video_status_advances_and_external_clock_stops(self):
        # Reset CRTC has retrace at line zero. Wait for it to end then restart.
        result, events = self.run_probe(bytes.fromhex(
            "bada03eca80875fbeca80874fbe680faf4"),
            "--video", "pvga1a", "--steps", "10000")
        self.assertEqual(result.returncode, 2, result.stderr)
        self.assertEqual(events[-1]["last_post"], 9)
        self.assertEqual(events[-1]["halted"], 1)
        for holes in ("reject", "ff"):
            result, events = self.run_probe(bytes.fromhex("bac203b009eee680faf4"),
                                           "--video", "pvga1a", "--io-holes", holes)
            self.assertEqual(result.returncode, 1)
            self.assertEqual(events[-1]["reason"], "video_clock_error")
            self.assertEqual(events[-1]["post_count"], 0)


if __name__ == "__main__":
    unittest.main()
