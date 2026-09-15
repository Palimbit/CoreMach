# BluMach portable engine

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: foundation for `0.2.0-dev`.

The portable engine is developed beside the inherited product and does not call
into its global state, configuration, device registry or user interfaces. The
two implementations can be enabled independently with
`BLUMACH_BUILD_LEGACY` and `BLUMACH_BUILD_ENGINE`.

## Dependency direction

```text
optional frontends
        |
        v
opaque runtime sessions
        |
        v
engine scheduler and CPU contract <--- reusable bus components
        |
        v
capability-based host services <--- null, Windows, POSIX, Circle, MorphOS
```

The engine and runtime are C11. They do not depend on Qt, SDL, native windowing,
host threads, C++ exceptions or a particular byte order. A frontend may use
those facilities without exposing them to an emulated component.

## Foundation invariants

- Every mutable value belongs to an engine, session, machine or component.
- A session has an explicit lifecycle and supports safe partial cleanup.
- Time uses integer ticks and same-time events use insertion order.
- CPU implementations receive a bounded budget and report consumed time.
- Buses model memory, I/O, program and data spaces independently.
- Debug access is an attribute of a transaction, not a second hidden bus.
- Host allocation, time and logging arrive through explicit capabilities.
- The null platform makes tests and GUI-free targets first-class builds.
- Every production file is covered exactly once by the provenance manifest.

The synthetic test machine has four instructions, two registers, memory, I/O,
halt, interrupt, a timer, event capture and register introspection. It is test
equipment and must never be exposed in the historical catalogue.

## Olivetti PCS 86 boundary

The canonical research identifies the PCS 86 processor as an NEC V30 at
10 MHz. The first real CPU target will therefore be an 808x-family interpreter
with the required V30 behaviour, not a target named or constrained as a pure
Intel 8088. No inherited CPU implementation is moved until its exact source
paths, commit, notices and adaptation method can be recorded.

The PCS 86 vertical slice starts only after this foundation is green. Its first
stage supplies firmware as a caller-owned blob, establishes reset execution and
memory transactions, and records instruction checkpoints without adding ROMs
or machine media to Git.

### PCS86-1 bring-up status

The first real-machine cut now models the documented NEC V30 at 10 MHz, 640 KiB
of conventional RAM and two caller-supplied 32 KiB firmware halves interleaved
at `F0000h-FFFFFh`. The machine validates the known firmware hashes when a
frontend supplies them, but test firmware may omit a hash so repository tests
can use newly authored synthetic bytes.

The explicit-state interpreter remains an incomplete V30 implementation. Any
unknown opcode returns `BM_STATUS_UNSUPPORTED`; it is never silently treated as
a no-op. One scheduler tick currently represents one completed instruction, so
cycle and bus timing remain deliberately outside this stage even though the
implemented subset can execute a substantial original-BIOS path.

The test ROM jumps from physical `FFFF0h` to `F0100h`, writes a byte through
the memory bus and halts. No Olivetti firmware or guest media is compiled,
copied or executed by this test.

### PCS86-2 platform-contract status

The next cut adds explicit, independently testable instances of the single
8259A interrupt controller and the 8253 timer. The PCS 86 owns its board glue:
known registers at `60h-6Fh` and the jumper byte at `100h` are not hidden in a
generic PC global. PIT channel 0 raises the machine's PIC IRQ0 input, while a
bus observer can capture successful I/O transactions without coupling devices
to a debugger or frontend.

The V30 subset now performs byte-oriented `IN` and `OUT` operations, including
word forms as two consecutive 8-bit bus transfers, and supports CLI, STI and
CLD. A synthetic ROM uses those paths to configure the PIC and PIT and exercise
board registers. That cut validated composition and traceability only; DMA,
RTC, keyboard queues, interrupt entry and much of the instruction set were
still absent at that boundary.

### PCS86-2A earlier BIOS execution milestone

The original BIOS is a local-only diagnostic input to a manual probe; it is
never part of CTest or a build artifact. Before the later timekeeping and video
work, the two recorded revision 1.09 EPROM hashes verified that the engine could execute
6,341,893 instructions and 2,851 successful I/O transactions before stopping
strictly on the first unmapped RTC counter register, port `E8h`, read at
`F000:F4B0`. This covers the
reset jump, flag/register self-test, the firmware's complete 64 KiB checksum
loop, its first conventional-memory alias check, a 64 KiB upper-memory
clear-and-scan pass, the following segment-overridden memory-alias check and
programming self-tests for the 8237 and its external page latches, the
MM58167 interrupt-status/control access, the following long conventional-
memory test, the complete empty option-ROM scan, the observed PCS 86 video-
selection sequence at `46E8h` and `102h`, the first Paradise initialization and
memory-copy paths, and enough resident diagnostics to publish a real text-mode
frame. The option-ROM region explicitly
models an unpopulated bus returning ones: the PCS 86 firmware already contains
its Paradise initialization and no separate ROM is invented at `C0000h`.

The interpreter additions are still a tested subset: arithmetic and logical
flags, conditional and relative branches, register ModR/M forms, 8086 memory
effective-address decoding, immediate arithmetic including sign-extended CMP,
byte and word immediate memory moves and loads, byte comparison, TEST, AND and
NOT, byte and word multiplication, unsigned word division with real-mode
interrupt-zero faults, NEC V30 bit operations, memory forms of general and
segment moves, direct and indirect near calls, software interrupts and IRET,
register plus all segment and FLAGS stack operations,
byte and word shifts, flag-control operations, segment-overridden loads, all four
segment overrides and byte/word MOVS, STOS, LODS and SCAS operations with
REP/REPE/REPNE. Its inspection contract exposes
all general and segment
registers. Unit tests use new synthetic bytes
reproducing the relevant instruction paths, not Olivetti firmware.

Port `70h` has no verified PCS 86 bit semantics in the evidence currently
available. BIOS context places its `40h` write in the upper-memory setup path,
so the machine records it as an opaque write-only board latch rather than
silently discarding it or borrowing the unrelated PC/AT CMOS convention.
Writes to the known EMS page-selector range `8400h-8403h` are also retained,
but the aperture and backing SIMMs remain deliberately absent.

BIOS writes to `46E8h` and `102h` are retained as write-only video-arbitration
latches. Their observed ordering and values are testable, but the engine does
not yet assign undocumented selection side effects to them. A new isolated
PVGA1A component owns the VGA and Paradise register files, DAC state and 256 KiB
of planar VRAM behind the generic buses. Its input-status phase is deliberately
deterministic rather than timed. The engine now defines a caller-owned XRGB8888
framebuffer value type, the runtime exposes optional geometry and rendering
operations, and the PCS 86 publishes PVGA1A output without exposing the device
to a frontend. The first rasterizer covers text modes only and reads the
programmed character, attribute, font, palette and CRTC state; it does not use a
built-in font or a synthetic diagnostic screen.

At the current stop point it produces a deterministic 720x400 frame (CRC32
`680D0FA8`) containing the PCS-86 Resident Diagnostics 1.09 screen. CPU, ROM,
DMA, interrupt-controller, Timer 0 and Clock/Calendar checks are visible as
passing, and 640 kB of base memory is reported. Execution continues beyond the
diagnostics until the first unimplemented parallel-port status read at `37Ah`.
This is bring-up evidence, not a claim that POST completes. Cursor, blink phase,
graphics modes and scan timing are still outside this cut.

The PCS 86 now owns a portable 8237 programming core with explicit address,
count, command, mode, request, mask, status and master-clear state. A separate
XT page-register component maps the firmware-observed `87h`, `83h`, `81h` and
`82h` channel order, retains reserved ports as independent latches and exposes
the resulting 20-bit DMA address. PCS 86 writes are constrained to the
documented four-bit page value; an 8237 master clear cannot erase these
external latches. Neither component claims arbitration, bus ownership or byte
transfers.

The MM58167 maps the PCS 86 control window at `B0h-B7h` and its counter/alarm
RAM at `E0h-EFh`. It advances a deterministic BCD millisecond calendar, matches
alarms, raises enabled interrupt sources and saves or restores its caller-owned
32-byte state. Counter and RAM reset commands, GO, standby and the PCS 86
checksum repair path are covered by focused tests. This is a selective port of
the inherited `src/device/isartc.c`, retaining Fred N. van Kempen's notice. A
yearless calendar and instruction-domain scheduling are explicit fidelity
limits; this is not a crystal- or battery-level simulation.

The 8253 is a selective port of the measured edge-state core, retaining Daniel
Balsom and Clara's attribution. It covers modes 0-5, binary and BCD counts,
gates, stable latches and output edges. Rational accumulators drive the PIT and
RTC from scheduler time without floating point. Until CPU cycle accounting is
part of the engine contract, the PCS 86 uses a measured functional instruction
rate for those domains and does not claim cycle accuracy.

Maskable interrupts now have an explicit handshake. The PIC publishes its
pending output, the machine routes that signal through the engine CPU contract,
and the V30 asks the PIC for a vector before pushing FLAGS/CS/IP and reading the
real-mode vector table. A withdrawn edge request no longer survives as its
original IRQ before the first interrupt acknowledgement. Neither component
owns the other. DMA transfers, parallel I/O, keyboard delivery, storage and
complete V30 coverage remain subsequent cuts; POST has not completed, although
its current diagnostic screen can be rendered and captured without Qt.
