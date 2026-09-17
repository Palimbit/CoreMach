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
- Storage components consume caller-owned block media; host paths and file
  handles stay outside the engine.
- Frontends submit stable physical-key events; guest machines own scan-code and
  controller-protocol translation.
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

The component also exposes a versioned architectural snapshot and a
single-boundary step operation. These contracts contain registers, segments,
IP, FLAGS and halt state, but deliberately exclude bus pins, trace bookkeeping
and host callbacks. They allow external conformance tools to establish an
arbitrary documented state without adding test-only globals or host services to
the interpreter.

`tools/run_v20_conformance.py` can stream an explicitly supplied
SingleStepTests/V20 corpus through the manual vector runner. The corpus remains
outside BluMach and its cycle and queue traces are ignored: native V20 results
are evidence for the architectural ISA shared with V30, not for the V30's
16-bit bus timing. The initial D4h/D5h campaign passes all 20,000 hardware
vectors and records two NEC distinctions: `AAM 00h` produces `AH=FFh` without
interrupt zero, while `AAD` always uses decimal base ten regardless of its
encoded second byte.

The next native-ISA campaign raises the verified total to 250,000 hardware
vectors. It covers the previously absent byte and accumulator forms of ADC and
SBB, DAA/DAS/AAA/AAS, CWD, INT3, INTO and all eight operations of the NEC 82h
byte-immediate alias. In particular, decimal and ASCII adjustment follow the
measured NEC flag and carry thresholds rather than assuming the behaviour of a
different x86 generation. Undefined flags are excluded with the corpus masks;
the interpreter still gives them a deterministic internal value.

The complete F6h/F7h campaign raises that total to 410,000 hardware vectors.
Both TEST encodings, NOT, NEG, unsigned and signed multiply, and unsigned and
signed divide now cover their byte and word forms. Divide errors enter vector
zero only after consuming the full instruction and leave the dividend intact;
the external adapter masks architecturally undefined saved-FLAGS bits without
weakening comparisons of the saved return address or other memory. The measured
NEC byte-IDIV boundary also raises divide error for a quotient of exactly -128.
Non-string REP prefixes are accepted as observed by the hardware corpus instead
of turning an otherwise implemented instruction into an unsupported opcode.

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

The earlier timekeeping cut produced a deterministic 720x400 frame (CRC32
`680D0FA8`) containing the PCS-86 Resident Diagnostics 1.09 screen. CPU, ROM,
DMA, interrupt-controller, Timer 0 and Clock/Calendar checks were visible as
passing, and 640 kB of base memory was reported. The current cut then adds a
caller-owned SPP register component at `378h-37Ah`, an NS16450 register core at
`3F8h-3FFh`, their port-65h gates and IRQ7/IRQ4 routes, and the PCS 86 dual
keyboard/mouse command queues. Runtime keyboard input uses stable physical-key
identifiers and the machine translates supported keys to IBM Set 1 bytes on
IRQ1; Qt, Win32 and host scan codes remain outside the engine.

With those components the preceding local BIOS probe executed 6,272,717
instructions and 3,251 successful I/O transactions, completed its parallel and
serial probes, and stopped strictly at its first access to the then-absent
floppy controller, `OUT 3F2h,AL` at `F000:352C`. The no-printer SPP status and
disconnected UART remain honest device states, not forced diagnostic-success
values. The floppy cut below advances beyond that recorded boundary; mouse
input, complete keyboard command coverage, cursor/blink, graphics modes and
scan timing remain outside the current implementation.

The PCS 86 now owns a portable 8237 core with explicit address,
count, command, mode, request, mask, status and master-clear state. A separate
XT page-register component maps the firmware-observed `87h`, `83h`, `81h` and
`82h` channel order, retains reserved ports as independent latches and exposes
the resulting 20-bit DMA address. PCS 86 writes are constrained to the
documented four-bit page value; an 8237 master clear cannot erase these
external latches. Devices can transfer bytes synchronously through an explicit
channel API; asynchronous arbitration, cycle stealing and bus-ownership timing
remain outside this cut.

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
owns the other. The added SPP, NS16450 and keyboard paths follow the same
ownership rule and communicate only through explicit callbacks and runtime
events. Mouse delivery and complete V30 coverage remain subsequent cuts.

### PCS86-4 floppy/bootstrap status

Storage remains a composition of small contracts. The engine defines a
caller-owned block medium with no host path or `FILE *`; a floppy drive owns
geometry and mechanical state; the uPD765-compatible component owns the AT
register front, command/result phases, active-low change indication and its
explicit links to IRQ6 and DMA2. The PCS 86 machine chooses the documented
jumper encoding from the configured drive geometry. No layer reaches into a
global drive table.

The public tests use newly authored in-memory sectors. They verify complete
512-byte DMA read and write paths, terminal count, including completion before
an EOT beyond the mounted track, result bytes, reset/sense
interrupts and write protection. The controller contract limits a sector
payload to 4096 bytes; read and write commands for an otherwise valid 8192-byte
generic drive are rejected before DMA or media callbacks. Original firmware
and media remain local-only manual inputs. With BIOS 1.09 and the preserved
720 KiB system diskette, all visible resident diagnostics report `Pass`, and
the BIOS detects one floppy and enters primary bootstrap. The boot sector and
system files execute far enough to display the Microsoft MS-DOS 3.30a banner.
The run then stops explicitly with `BM_STATUS_UNMAPPED` at `OUT 02F2h,AL` after
6,935,257 retired instructions and 14,031 I/O accesses. This is observed DOS
initialization, not a completed boot to a command prompt.

A CPU architecture cut adds tested, general 808x semantics for
sign extension, string comparison and repeat conditions, direct and indirect
far control flow, `POP r/m16`, byte/word exchange and negation, accumulator
`TEST`, `XLAT`, immediate carry arithmetic and unsigned byte division. The
CPU trace now distinguishes the first byte from the effective opcode after
prefixes, while inspection exposes up to the first eight consumed instruction
bytes and their full length. Each behavior is covered with synthetic memory and
machine-independent tests; opcode handling does not depend on firmware
addresses, image contents or host services.

The following CPU cut completes both F6h/F7h TEST encodings and their byte/word
NOT, NEG, MUL, IMUL, DIV and IDIV operations, including interrupt-zero error
entry. Its external V20 corpus evidence remains architectural only; it does not
claim V30 cycle, prefetch-queue or 16-bit-bus fidelity.

The FDC deliberately omits rotational and command latency, non-DMA transfer,
format-track, deleted-data distinction, flux/weak-sector formats and dynamic
media insertion. Those are explicit future fidelity work, while deterministic
raw-sector loading through the DOS banner is the validated boundary of this
cut.
