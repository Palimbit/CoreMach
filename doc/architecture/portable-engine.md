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

This is not yet a BIOS-capable CPU. The explicit-state interpreter implements
only the reset-vector path and a small, documented instruction subset needed to
prove segmented fetch, a far jump, register/segment setup, a RAM write and
halt. Any other opcode returns `BM_STATUS_UNSUPPORTED`; it is never silently
treated as a no-op. One scheduler tick currently represents one completed
instruction, so cycle and bus timing remain deliberately outside this cut.

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

### PCS86-2A BIOS execution and interrupt status

The original BIOS is now a local-only diagnostic input to a manual probe; it
is never part of CTest or a build artifact. With the two recorded revision 1.09
EPROM hashes verified outside the executable, the portable engine executes
1,377,877 instructions and 163 successful I/O transactions before reporting
an unmapped write to VGA graphics-controller index port `3CEh` from
`F000:6B95`. This covers the
reset jump, flag/register self-test, the firmware's complete 64 KiB checksum
loop, its first conventional-memory alias check, a 64 KiB upper-memory
clear-and-scan pass, the following segment-overridden memory-alias check and
programming self-tests for the 8237 and its external page latches, the
MM58167 interrupt-status/control access, the following long conventional-
memory test, the complete empty option-ROM scan and the observed PCS 86 video-
selection sequence at `46E8h` and `102h`. The option-ROM region explicitly
models an unpopulated bus returning ones: the PCS 86 firmware already contains
its Paradise initialization and no separate ROM is invented at `C0000h`.

The interpreter additions are still a tested subset: arithmetic and logical
flags, conditional and relative branches, register ModR/M forms, 8086 memory
effective-address decoding, immediate arithmetic including sign-extended CMP,
byte and word immediate
memory moves, byte comparison, TEST, AND
and NOT, memory forms of general and segment moves, near CALL/RET, register and
ES/DS and FLAGS stack operations, SHR by CL, segment-overridden loads, all four segment
overrides and `LODSW`/`REP STOSW`/`REPE SCASW`. Its inspection contract exposes
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
not yet assign undocumented selection side effects to them. The strict bus now
stops at the first PVGA1A register transaction, `OUT 3CEh,0Fh`; VGA registers,
VRAM, rendering and a framebuffer contract remain absent.

The PCS 86 now owns a portable 8237 programming core with explicit address,
count, command, mode, request, mask, status and master-clear state. A separate
XT page-register component maps the firmware-observed `87h`, `83h`, `81h` and
`82h` channel order, retains reserved ports as independent latches and exposes
the resulting 20-bit DMA address. PCS 86 writes are constrained to the
documented four-bit page value; an 8237 master clear cannot erase these
external latches. Neither component claims arbitration, bus ownership or byte
transfers.

The first MM58167 cut maps only the firmware-used interrupt front. Reading
`B0h` returns and clears the pending status; writing `B1h` clears that status
and stores the interrupt-control byte. This behaviour is a selective port of
the inherited `src/device/isartc.c`, retaining Fred N. van Kempen's notice.
Registers `B2h-B7h` and `E0h-EFh`, clock progression, alarms, IRQ generation
and persistence remain deliberately unmapped until they can be implemented and
tested as real RTC behaviour.

Maskable interrupts now have an explicit handshake. The PIC publishes its
pending output, the machine routes that signal through the engine CPU contract,
and the V30 asks the PIC for a vector before pushing FLAGS/CS/IP and reading the
real-mode vector table. Neither component owns the other. DMA transfers, the
remaining RTC and complete V30 coverage remain subsequent cuts; POST has not
completed and no video output exists yet.
