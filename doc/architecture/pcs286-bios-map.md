# PCS286 BIOS 1.42 execution map

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

This is a bounded map of the clean local-only 128 KiB Olivetti PCS286 BIOS
1.42 used by the portable-engine probe. It is not a complete disassembly or a
vendor symbol table. Addresses name observed entry points in the current image;
they are evidence locators, not a public ABI and must never select emulator
behaviour.

Canonical input: `bios-142-combined`, SHA-256
`AFBD051666869F3F58F23E52F9DD468FB9AD9F629D2DBCCFDA5324E83A897621`.
The firmware remains outside Git. The map records no ROM bytes and does not
authorize redistribution.

## Coverage states

| State | Meaning |
|---|---|
| Observed | Executed by a retained trace or directly visible in a framebuffer |
| Inspected | Bounded read-only disassembly establishes control and I/O flow |
| Inferred | Meaning follows from nearby calls/data but lacks an isolated trace |
| Pending | Known entry or subsystem without enough evidence for a semantic name |

## Current routine map

| BIOS 1.42 address | State | Current interpretation and evidence |
|---|---|---|
| `F000:FFF0` | Observed | 80286 reset vector; first fetch is physical `FFFFF0h` through the real reset cache and Headland ROM mapping |
| `F000:05C0` | Observed/inspected | Integrated VGA initialization and diagnostics. Programs `46E8h`, `0102h`, VGA indexed registers and later invokes the status, VRAM and register checks below. This code is in the motherboard BIOS; no separate C000 option ROM is used. |
| `F000:088F`–`08F3` | Observed/inspected | VGA input-status/PIT timing diagnostic. The original portable status kept bit0 high while the sequencer blanked the screen, causing a 65536-iteration timeout and visible error bit1. Raster display-enable now continues while blanked; two PIT samples produce the accepted interval `8500h`. |
| `F000:08F4`–`098C` | Inspected | VGA register/status exercise around attribute/indexed registers; contributes error bit2. Its attribute diagnostic-output path is now modeled and unit-tested, but this routine was not reached in the retained trace. |
| `F000:098D`–`09B5` | Inspected | Planar VRAM pattern test at `A0000h`; contributes error bit4. The portable run reaches a later visible error, but this subtest has not been independently attributed pass/fail. |
| `F000:0A1F`–`0AA4` | Observed/inspected | Monitor-sense test. It writes DAC entry zero and expects Input Status 0 bit4 set for RGB `04/04/04`, then clear when each primary is raised to `10h`. The functional comparator now follows those observed transitions; its physical voltage threshold remains unqualified. |
| `F000:0BF7` | Inferred | PIT counter sampling helper used by the VGA timing diagnostic. Calling convention and exact counter arithmetic still need a focused trace. |
| `F000:5791` | Observed/inspected | Calls integrated VGA diagnostics during POST. |
| `F000:57A6` | Observed/inspected | Formats/reports the returned VGA diagnostic bit mask. Retained frames progressed from `VGA Error : 1` to `VGA Error : 10`, then to no VGA error after correcting raster blanking and monitor sense. |
| `F000:44A9`–`44D2` | Observed/inspected | Writes zero to port80h, installs the protected-return state and requests CPU reset through KBC command FEh. RAM, CMOS, peripherals and the port80h checkpoint must survive this CPU-only reset. |
| `F000:44D5`–`44F9` | Observed/inspected | Resumes through the warm-reset vector and reads port80h. A nonzero value prints unlabelled `Error : 2`; retained zero emits progress code4Ah and continues. A board-owned read/write latch removes the former false error without forcing the read result. |
| `F000:477B`–`47C3` | Observed/inspected | Writes eight rotating patterns across ports80h–8Eh, reads each back, then clears the bank before testing the two DMA controllers. Seven addresses are channel page registers; the remaining addresses are board-owned latches. The composed model now reaches visible `DMA Controllers Pass`. |
| `F000:4952`–`4963` | Observed/inspected | Reads KBC internal RAM cell2Dh and writes it back through6Dh after clearing bit7. This uses the generic 8042 RAM command ranges, not an Olivetti-only shortcut. |
| `F000:49D4`–`49EE` | Observed/inspected | Sends keyboard commandEDh and its LED option before draining both ACKs. Host IBF and full OBF must remain independent; retained ACKs are delivered by the real keyboard pair. Visible result is `Keyboard Pass`. |
| `F000:4A01`–`4C02` | Observed/inspected | RTC/CMOS diagnostics, including waiting for UIP high/low and periodic/update checks. The retained run visibly reaches `Clock/Calendar Pass` and `CMOS RAM Pass`. |
| `F000:4C29` onward | Observed/partly inspected | Protected-mode CPU diagnostic. The retained run visibly reports `CPU Protected Mode Pass`. |
| `F000:514B`–`5162` | Observed/inspected | Coprocessor detection executes `FNINIT`, clears a temporary word, then executes `FNSTCW [0006]` and checks for control-word high byte03h. The explicit absent-80287 contract consumes both ESC instructions without a PEREQ transfer, leaves memory unchanged and follows the firmware's absence branch. |
| `F000:3D73`–`3DA9` | Observed/inspected | ATA readiness probe polls status port `1F7h` in five finite 65536-read batches. With no storage component mapped, FFh keeps BSY set; the retained longer run completes the firmware timeout and displays `Fixed Disks: Pass` for the absent-drive configuration. |
| `F000:3DAA`–`3DD3` | Observed/inspected | Diskette-controller diagnostic writes DOR `00h`, then `0Ch`, and waits for MSR controller-busy bit4 to clear at `3F4h`. An absent FFh port exhausts the bounded loop and records an error; the composed WD37C65 returns the documented idle `80h`. |
| `F000:5620`–`567A`, `8CF8`–`8F17` | Observed/inspected | Auxiliary-device probe enables the second KBC channel with A8h, unmasks IRQ12, sends mouse reset FFh through D4h and expects FAh/AAh/ID. Its bounded no-response branch disables the channel with A7h. The portable board currently attaches no mouse and must not synthesize these bytes. |
| `F000:567C`–`569B` | Observed/inspected | Command A4h queries whether a controller password is installed. The BIOS sends A6h only after response FAh. The functional KBC has no password store and returns F1h, selecting the real no-password branch. |
| `F000:51F1`–`51F6`, `55DD`–`55EA` | Observed/inspected | After fixed-disk detection the BIOS writes `47h` through controller command `60h`. Bit1 is reserved by the generic IBM AT description but is a retained command-byte RAM bit, not a failed host transfer. |
| `F000:82C2` | Inspected | Bounded KBC output-buffer drain helper. |
| `F000:82DC` | Observed/inspected | Waits for KBC IBF clear and writes a controller command to port64h. |
| `F000:82E2` | Observed/inspected | Waits for KBC IBF clear and writes host data to port60h. |
| `F000:82E8` | Observed/inspected | Waits for OBF and reads port60h. Real traces cover reset ACK/BAT and scheduled F1 bytes. |
| `F000:82EE` | Observed/inspected | Reads the KBC command byte using command20h. |
| `F000:D192` | Observed/inspected | Keyboard initialization/diagnostic sequence: programs CCB, disables the interface, sends resetFFh and consumes actual ACK/BAT through the KBC helpers. |
| `F000:C1CE`–`C201` | Observed/inspected | Prints a diagnostic name/error value and the continuation prompt through BIOS video output. Exact internal string-table ownership is not yet named. |
| `F000:C202`–`C23A` | Observed/inspected | Diagnostic reporting/parallel-code path preceding the F1 continuation flow. |
| `F000:C25A`–`C28D` | Observed/inspected | Enables keyboard, masks IRQ1, polls OBF and accepts set-1 byte3Bh as F1 before restoring controller/PIC state. |
| `F000:F220`–`F271` | Observed/inspected | KBC vendor-command wrapper and status polling. Real traces cover CFh and80h/84h through the explicit PCS286 functional profile. |
| `F000:F1CC` onward | Inspected/partly observed | Copies protected-mode tables, loads IDTR/GDTR, issues vendor KBC command CFh and enters PE with LMSW/far jump. The portable CPU work validates the architectural mechanisms; the complete BIOS path is not yet catalogued routine by routine. |
| `F000:4D64` vicinity | Pending | Historical Circle trace associated this area with a protected segment-limit fault. It has not yet been reproduced and bounded in the portable probe. |

## Subsystem coverage

| Area | What is mapped | What remains |
|---|---|---|
| Reset and early POST | Reset vector, ROM mapping, early I/O trace, protected CPU-reset return and retained port80h checkpoint | Remaining POST-code meanings and physical ownership of the port80h latch |
| Integrated video | Initialization, status/PIT diagnostic, monitor sense, register and VRAM tests, error reporting | Qualify physical raster/monitor-sense timing and independently trace the remaining subtests |
| Keyboard/KBC | Low-level helpers, reset/ACK/BAT, CCB writes, vendor commands, F1 continuation | Exact Mitsubishi mask-ROM behaviour; remaining vendor commands and physical timing |
| RTC/PIT/PIC | Executed I/O and portable component behaviour are recorded elsewhere | BIOS routine names and full call graph remain incomplete |
| Protected mode | Entry mechanisms and many CPU semantics are validated | Complete BIOS-owned task/diagnostic flow and the historical `F000:4D64` site |
| DMA/FDC/ATA | DMA tests pass; ATA absent-controller timeout at3D73 is mapped; historical Circle pilot reaches FDC/DOS | Portable ATA/FDC device service and later BIOS routines are pending |
| INT services | INT10 output is observed through integrated video code | Full INT10/13/15/16 tables and functions have not been enumerated |

## How to extend the map

Add an address only with an exact BIOS hash and classify the claim as observed,
inspected, inferred or pending. Prefer the smallest bounded disassembly around an
observed stop, I/O event or framebuffer change. Record ports, inputs, outputs and
failure branches without copying firmware bytes. Do not add emulator conditions
on BIOS addresses, strings or checksums. A named routine is evidence for fixing a
component contract; it is never permission to fake that routine's result.

The map is intentionally incomplete. “All BIOS functions documented” would
require a stable function inventory, cross-reference graph and interrupt-service
matrix for this exact image. The present goal is narrower: map every function
that becomes relevant to an observable portable-machine boundary, then expand
systematically as POST, Setup and media boot progress.
