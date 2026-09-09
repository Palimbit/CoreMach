# Toshiba T5100 (experimental)

For the evidence trail, failed approaches and engineering compromises behind
this model, see the
[Toshiba T5100 implementation narrative](toshiba-t5100-implementation.md).

The Toshiba T5100 is a 1988 portable built around a 16 MHz Intel 80386DX,
2 MiB of standard memory, an optional 2 MiB memory card, an internal IDE hard
disk and a gas-plasma display driven by Toshiba's proprietary AGS/CELT logic.

## Current implementation

This initial BluMach model is intentionally limited to behavior supported by
the Toshiba maintenance manual and observation of system BIOS V2.30:

- the 64 KiB interleaved system BIOS at `F0000-FFFFF`;
- a distinct secondary keyboard-controller endpoint at `8060/8064`, including
  the POST command `BB` and hardware-status command `B4`;
- coherent Toshiba system-control latches at `8080-808F`, with unknown
  electrical side effects left unimplemented;
- the 64 KiB LIM frame at `D0000-DFFFF`, four 16 KiB slots and the eight page
  registers used by the BIOS; and
- the Toshiba `TC8565`-compatible floppy path already used by the T1x00
  family, including Toshiba's `3F7` data-rate/disk-change semantics;
- a T5100-specific integrated IDE route at `1F0-1F7` and `3F6`, leaving
  shared port `3F7` exclusively to the floppy gate logic; and
- an experimental internal PEGA2-compatible video path: EGA register and
  256 KiB VRAM behavior, the documented 2 KiB SRAM aperture, Toshiba's
  two-read extended-register unlock, and a four-level orange plasma palette;
  and
- generic AT RTC and external-keyboard cores where the documented interfaces
  permit reuse.

The documented `B4` result is `8C` for the standard 16 MHz machine with one
1.44 MB internal floppy and no external floppy.

## Important limitations

The original 32 KiB AGS video ROM has not been preserved. BluMach neither
invents it nor borrows the T3200 ROM. For this explicitly experimental v1, a
locally supplied IBM EGA option ROM is adapted **in memory only**: the system
BIOS's `AGS` signature and its two far-entry pointers at `3FF0` and `3FF4` are
provided as conservative returns, while the standard option entry supplies
EGA INT 10h services. The source ROM is not modified on disk and is not
presented as Toshiba firmware.

This is a compatibility implementation, not a faithful recovery of AGS or
CELT. The current guest timing is standard 640x350 EGA rather than the panel's
exact 640x400 scan conversion. Exact line expansion, gray-scale transfer,
extended-register side effects, internal/external display switching, optional
memory-card page decode, remaining Toshiba gate-array behavior, exact floppy
timing and controller firmware remain unavailable or provisional.

BIOS V2.30 now boots both the read-only TESTCE3 floppy and Toshiba MS-DOS 3.30
from hard disk on the internal compatibility display. In Setup choose
**Internal EGA compatible / External MDA or None** and **High resolution**.
The factory defaults select that video path but also hard-disk type 6; for the
tested CP-3044-style image, restore hard-disk type 7 before recording with
F10/Y and power-cycling.

The tested profile uses 2 MiB RAM, the integrated controller and an IDE
image with 980 cylinders, 5 heads and 17 sectors (BIOS hard-disk type 7).
The BIOS setup displays the highest cylinder number, 979. This validates the
geometry and host interface, not a cycle-accurate Conner CP-3044 model.

Toshiba DOS 3.30 FDISK and FORMAT /S successfully prepared an initially blank
disk, followed by file write/read and a cold boot from C: with no floppy
inserted. The active primary partition starts at sector 17 and contains
65518 sectors, respecting DOS 3.30's approximately 32 MiB partition limit.

The machine remains experimental: the internal compatibility display, full
EMS behavior and reset/resume coverage are incomplete. The v1 selector is
therefore restricted to the tested 2 MiB population; the documented optional
2 MiB card is not offered until its mapping can be modeled honestly. Enable **Log
experimental firmware progress** only when collecting diagnostic logs.

## Memory and diagnostic coverage

For the tested 2 MiB configuration, use BIOS setup **Extended memory: 1 MB**
and **Expanded memory: 0 MB + 384 KB**, with fast ROM disabled. Save using
F10/Y. This matches the current linear-memory and 24-page LIM subset more
closely than the factory-style 1 MB expanded-memory allocation. It is an
experimental profile, not a claim that all memory modes are implemented.

TEST3 from Toshiba R3CE0 (version 6.00) identifies the T5100 and reports
640 KiB conventional plus 1024 KiB extended memory with this setup. Its
automatic sequence includes operator-verified display patterns; a complete
manufacturer-diagnostic pass has **not** been established. XCHAD reports
`not available on this machine` in the earlier external-CGA profile. It has not
yet been repeated against the internal compatibility path, and the model must
not bypass its machine checks.

Two project-authored DOS probes are available as source in `tests/`:

- `t5100_ems_probe.asm`: saves, writes, compares and restores two words per
  bank-0 page through 0208/D000. Pages 0-23 pass; the exploratory 24-87 range
  is absent. This does not validate all four slots or an EMS driver.
- `t5100_xmem_probe.asm`: queries INT 15h/88h and uses INT 15h/87h to save,
  write, compare and restore 16 distinct words at 64 KiB boundaries from
  100000h to 1F0000h. The 1 MiB extended profile passes this sampled test;
  this is not exhaustive RAM, parity or timing validation.

Assemble with NASM's `-f bin` option into a local test directory outside the
Git worktree. Run only on disposable DOS profiles with no resident EMS/XMS
manager or other extended-memory user. Keep original media read-only and
scan any generated boot medium before execution. The probes print counts in
hexadecimal; they report observations rather than serving as CI exit-code
assertions.

## Firmware

Atlas TESTCE3 4.30 reaches the service subtest menus. MEMORY TEST 04
(Protected mode), loop enabled and stop-on-error enabled, reports three
passes and zero errors with the 1 MiB extended allocation in interpreter
mode. The disposable looping run was stopped from the host. This does not
validate EMS, dynamic recompilation or the complete diagnostic suite.

System BIOS V2.30 uses two 32 KiB ROM images, EVEN `042F` and ODD `043F`.
The v1 compatibility path additionally requires IBM EGA BIOS
`ibm_6277356_ega_card_u44_27128.bin` (16 KiB, SHA-256
`bf1583dd387d6e078ab3f5039bcb3f7020a66a63d5f0e57039883c5081dbbf9c`).
Firmware is not distributed with BluMach. The expected files are shown in the
machine's BIOS configuration and must be supplied locally by the user.
