# PCS286 portable 80286: absent 80287 contract

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Selected functional model

The current PCS286 profile has no populated 80287. Intel's 1987 80286/80287
Programmer's Reference Manual section 3.12.1 defines ESC as the CPU/NPX
instruction and operand-transfer boundary. The 80286 first applies its MSW
extension controls: ESC faults through vector 7 when EM or TS is set; WAIT does
so only for MP+TS. Those truth tables remain unchanged.

With EM and TS clear, an absent extension leaves BUSY, ERROR and PEREQ inactive.
The CPU consumes the ESC opcode, ModR/M and displacement and calculates the
16-bit effective address. No PEREQ operand transfer follows, so a memory source
is not read and a memory destination is not written. Register forms likewise
have no architectural result. This is an explicit absent-device bus contract,
not floating-point execution and not a general promise that arbitrary x87 code
works.

The inherited classic 286 path was used as a behavioral comparison: its no-FPU
table fetches the effective address after the same CR0/MSW trap decision. No
classic source was copied. A future populated 80287 needs a separate interface
for BUSY/ERROR/PEREQ, operand direction and size, and extension state. Treating
stores as successful CPU writes or returning invented x87 values is forbidden.

## Coverage

`pcs286-component.cpu-extension` checks all eight ESC opcodes and all MSW
MP/EM/TS combinations, register and representative 16-bit memory addressing
forms, displacement consumption, RM/PE execution, invalid unused segment
caches, unchanged registers/FLAGS/memory, and host fetch failures before and
after every instruction byte. The exact BIOS sequence `FNINIT; FNSTCW [0006]`
leaves the sentinel word unchanged when no 80287 is attached.

The unchanged local-only BIOS 1.42 then advances beyond `F000:514B` without a
host stop or fabricated coprocessor result. A retained 9,000,000-attempt run
ends by budget while polling the unpopulated ATA status port `1F7h` at
`F000:3D82`; the finite firmware timeout has not yet expired. The framebuffer
still shows all prior diagnostics through CMOS RAM as Pass. No POST completion,
Setup, disk detection or boot is claimed.

A longer retained run confirms this is a finite firmware timeout: it displays
`Fixed Disks: Pass` for the absent-drive configuration and continues to an 8042
command-byte write. This extends the boot observation; it does not add ATA or
80287 hardware.

Timing remains UNKNOWN. LOCK/REP ESC combinations remain outside the documented
portable profile. Processor-extension segment overrun, a populated 80287,
numeric exceptions and physical pin timing remain pending.
