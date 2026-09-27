# Olivetti PCS 286 — board foundation, not yet a runnable machine

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

This directory supplies draft machine contracts, real RAM/firmware backing
storage and private AT memory/I/O adapters for the inherited components. It does not
implement the complete board. Calling the declared machine
factory currently fails to link; there is no
fake-success implementation or runtime/frontend registration.

A separate [local boot probe](../../doc/architecture/pcs286-boot-probe.md) runs
the real external BIOS1.42 from reset using the existing components. Optional
initialized calendar and clocked PVGA1A remain explicit. The motherboard BIOS
includes the video firmware; no separate generic VGA ROM is loaded. Diagnostic
framebuffer export and scheduled physical key events now use the existing
renderer and keyboard/controller pair.
The companion [BIOS execution map](../../doc/architecture/pcs286-bios-map.md)
records the routines reached or inspected so far and makes the missing coverage
explicit; the full BIOS function inventory is not yet complete.

BIOS1.42 now clears the observed VGA failures, protected reset checkpoint, DMA
page/controller tests, KBC RAM access and overlapped keyboard ACK/option flow
without a BIOS condition or forced result. The retained 8M-step framebuffer
shows 640KiB base +384KiB extended, then Parity, PIC, DMA, Keyboard,
Clock/Calendar, CPU Protected Mode and CMOS RAM all passing. Execution no
longer stops at the x87 probe: an explicitly absent 80287 consumes ESC
addressing without PEREQ transfers, so `FNINIT`/`FNSTCW` take the real firmware
absence branch. A retained longer run completes the finite absent-ATA timeout,
shows `Fixed Disks: Pass`, and reaches the next real boundary: command-byte
value `47h` at the 8042. The command-byte RAM now retains all eight bits while
only documented bits have external effects. There is no F1, guest exception,
HALT or shutdown; POST, Setup and DOS boot remain incomplete.

Next: continue from the corrected full 8042 command-byte latch and integrate the
appropriate portable floppy/storage service at the next observed boundary. A
runtime machine also needs a configuration-derived default CMOS with a valid
checksum; the probe's depleted and calendar-only profiles remain explicit
diagnostics. Public factory absent; strict CPU clock closed and changes remain
local.

Read [the architecture, evidence and work packets](../../doc/architecture/pcs286-portable-contracts.md)
before implementing anything here. The canonical record is `olivetti/pcs286`;
PCS286S is a different platform.

Contract ownership:

- `components/cpu/80286/`: reusable Intel 286 interpreter boundary.
- `components/chipsets/headland/`: memory routing/register boundary.
- `components/chipsets/olivetti-ioc02/`: reusable IOC02 boundary.
- `components/pc/include/blumach/components/at_*.h`, `pit8254.h`, `rtc_at.h`,
  `kbc8042.h`, `keyboard_at.h`, `wd37c65.h`: AT devices and clock links.
- `components/video/include/blumach/components/ims_g171.h`: DAC boundary.
- `include/blumach/systems/pcs286_board.h`: board pins and ports 61h–63h.
- `include/blumach/systems/pcs286_memory.h`, `src/pcs286_memory.c`: implemented
  storage ownership and resolved-offset accesses, without physical decoding.
- `include/blumach/systems/olivetti_pcs286.h`: typed machine configuration and
  future runtime factory signatures.

Existing memory, floppy drives/media, PVGA1A and UART/SPP APIs remain reuse
candidates; their present implementation does not imply full PCS286 fidelity.
Build `blumach_pcs286_contract_check` to compile each header independently.
This is structural validation, not component behaviour testing.

The [P0 test foundation and agent boundaries](../../doc/architecture/pcs286-p0.md)
now supply a running generic bus/memory/clock oracle and compiled initial
acceptance tests. The partial 286 and AT interconnect run real tests; the
AT PIC and DMA have bounded implementations and tests. The public Headland
gate still skips explicitly; its private migration and composition tests run.
No firmware was run in that P0 foundation stage; current probe results are above.

## Backing storage and synthetic composition

`blumach_pcs286_memory` owns private RAM and a copied 128-KiB ROM image, accepting
combined firmware or two 64-KiB low/high lanes. Construction publishes nothing
until complete and releases partial allocations on failure. Byte-wise access
does not depend on host alignment or endianness. The storage capacity is not a
board-population validation. Zero-filled new RAM is an emulator policy only.

The board supplies an already resolved region and offset. This helper does not
decide A20, ROM mirrors, RAM remapping, write-enable signals, open bus, straps,
registers or elapsed time. It preserves the supplied wait count; this must not
be mistaken for established zero-wait hardware. ROM writes return READ_ONLY,
leaving electrical ignored-write policy to the future board adapter. DEBUG is
read-only. CPU-only reset does not reconstruct or clear memory.

`pcs286-component.board-memory` runs an authored reset trampoline at FFFFF0h,
jumps into test RAM, loads SS/SP, pushes/pops and exercises a test I/O latch
through the actual 286 and AT components. Its linear windows and I/O latch are
test-only fixtures, **not a provisional PCS286 chipset in production**. Timing
remains UNKNOWN. These earlier synthetic windows do not validate Headland.
The current migration/composition coverage is described below; qualified
PCS286 wiring, clocked execution and the runtime factory remain pending.

## Private Headland AT memory adapter

`blumach_pcs286_headland_at` composes the inherited GC103 route helper with
the existing backing store and AT ownership checks. Its caller-owned private
interface is `src/headland_at_memory.h`; it publishes no bus callback itself.
The caller explicitly selects the legacy profile, supplies A20 and chooses
strict/provisional timing, unpopulated-memory and protected-write policies.
The public Headland component and machine factory remain absent.

It splits transfers on routes and native byte/word boundaries, stages read
results, retains completed endpoint effects on host failure, and converts
summed extra service clocks to requester clocks with one upward rounding.
Strict timing rejects the inherited UNKNOWN routes before any endpoint call;
provisional values are caller inputs, not measured PCS286 timing. Installed
endpoint errors are propagated without converting them to successful FF reads.

`pcs286-component.headland-at-memory` exercises actual backing and AT bus,
CPU/DMA/ISA A20/ownership, partial failures and an authored CPU program that
programs EMS and accesses relocated RAM. Its external device and I/O timing
were initially test fixtures. The authored CPU program now uses the private
board I/O decoder below. See [coverage and remaining qualifications](../../doc/architecture/pcs286-headland.md).

## Private board I/O decoder

`src/board_io.h` composes existing Headland, IOC02, AT PIC and DMA engines with
explicit sparse ports, native widths, external resource conflicts and hole
policies. It shares exact wait conversion with memory. The AT bus checks
ownership before dispatch; partial device effects survive host errors while
the caller transaction is staged. Strict timing stays blocked, DEBUG stays
observational, and IOC02 bits do not drive undocumented physical signals.

`pcs286-component.board-io` tests all ports, split transfers, real device state,
resource boundaries and failures. This is a private functional composition,
not public Headland acceptance or a complete PCS286. PIT/RTC/KBC and remaining
board devices/signals, profile qualification, measured timing, runtime factory
and firmware validation remain separate work.

## Timer component available for composition

`blumach_pit8254` now reuses the existing PIT edge core with explicit 8254
read-back, NULL/count latches and odd-divisor behavior. Its regression fixture
connects the real AT I/O decoder and PIC IRQ0. See [timer evidence, tests and
scope](../../doc/architecture/pcs286-at-timer.md). `blumach_at_engine_adapters`
now supplies the PIT clock link: exact elapsed-pulse synchronization before I/O,
output deadlines, pure DEBUG, explicit gate and full-reset sequencing, and
retained host failures. The AT/PIC fixture also runs through that clock link.
Producing refresh/error signals, rendering audio, RTC, KBC and the complete
board coordinator remain pending; a clocked timer does not make the portable
machine bootable.

The private `blumach_pcs286_port61` now supplies documented AT signal transport
for GATE2, raw OUT2, digital speaker AND and check enables. REF DET and error
bits require real board endpoints; it does not equate PIT1 pulses with refresh
completion or manufacture clear error status. See [evidence and excluded classic
shortcuts](../../doc/architecture/pcs286-port61.md).

`blumach_pcs286_refresh` now supplies a functional pending-request/REF DET
producer using the existing AT arbiter. It waits through LOCK and existing
DMA/ISA ownership, completes a logical event after HOLD/HLDA and releases the
bus. It performs no RAM/MMIO reads or physical DRAM simulation. An authored
286 program reads this state through the actual PIT/clock/port61 composition.
See [scope, tests and scheduling obligations](../../doc/architecture/pcs286-refresh.md).
Physical refresh timing is not a prerequisite for the functional implementation.
`blumach_pcs286_checks` now provides functional RAM/I/O error capture, check
enables, global masking and NMI level routing. A real 286 integration reads the
cause through port61, clears it with OUT and returns from vector2 with IRET.
It consumes qualified guest parity samples/external error levels, never host
access failures. See [evidence and boundaries](../../doc/architecture/pcs286-checks-nmi.md).
The functional `blumach_at_rtc` now provides calendar, CMOS, alarms, periodic/
update IRQ flags, read-to-clear C and real port70 NMI masking. It reuses classic
code with Motorola corrections and explicit 64/128-byte profiles. The NMI CPU
test uses these real ports; IRQ8 is tested against the real cascaded PIC.
See [evidence, coverage and boundaries](../../doc/architecture/pcs286-at-rtc.md).
Its [engine clock attachment](../../doc/architecture/pcs286-at-rtc-clock.md) now
shares the PIT adapter, synchronizes elapsed cycles before I/O and rearms on
read-C/control changes. Warm RTC reset preserves oscillator phase; resetting the
whole engine epoch is a distinct lifecycle operation.

The bounded native `blumach_kbc8042` now reuses the classic translation table
and documented AT command/buffer behavior, with real PIC IRQ1 tests and fallible
A20/reset outputs. It preserves held reset and rejects unsupported electrical/
MCU/vendor diagnostics. See [coverage and limits](../../doc/architecture/pcs286-at-kbc.md).
The separate `blumach_at_keyboard` now supplies the documented enhanced AT
protocol, three scan sets, guest LEDs, typematic and independent response/scan
buffers. Real keyboard/controller/PIC integration verifies IRQ1, translation and
reset/BAT sequencing. See [keyboard evidence and limits](../../doc/architecture/pcs286-at-keyboard.md);
this is an explicit IBM enhanced profile, not certified Olivetti keyboard identity.
The [individual KBC/keyboard clock links](../../doc/architecture/pcs286-keyboard-clock.md)
now reuse the PIT/RTC adapter, synchronize owner inputs and preserve fractional
deadlines. The [coordinated pair](../../doc/architecture/pcs286-keyboard-pair.md)
now owns both devices and orders peer effects after both reach a common logical
clock boundary. It has real-engine/PIC/CPU-dispatch tests, without per-cycle
polling or invented ACKs. Unequal service rates reject explicitly; no physical
shared-oscillator identity is claimed. The private
[board control owner](../../doc/architecture/pcs286-board-control.md) now routes
pair A20 to Headland memory, IRQ1 to PIC and reset to a safe 286 boundary.
CPU-only reset retains peripheral state and resamples INTR/HOLD/NMI. Actual
CPU/bus/Headland/pair tests cover held/pulsed reset, LOCK/HLDA, IRQ/IRET and
retained host failures. Low-DATA-drive limits, full scheduling/error-source
composition, PCS286 qualification and audible output remain pending;
the public board is incomplete and the strict CPU-clock gate remains closed.

The private diagnostic now composes the functional
[WD37C65 controller](../../doc/architecture/pcs286-wd37c65.md) on its standard
AT port split, with IRQ6 and DREQ2 connected to the existing PIC and portable
AT DMA. The command/media behavior is derived from BluMach's existing portable
765 rather than reimplemented from an unrelated model. The CLI can attach a
verified external 1.44MiB image as a write-protected in-memory drive; the
register-level POST result and later media/DMA boot result remain distinct claims.

The private [DMA coordinator](../../doc/architecture/pcs286-dma-coordinator.md)
now connects AT-DMA HRQ to the existing bus HOLD and real 286 HLDA boundary.
It grants DMA8/16, services one unit and releases grant/HOLD in order; DMA memory
accesses pass through the owned AT bus. Synthetic channel2 handoffs pass. The
retained no-drive result issues no DREQ. A first attached-media run stopped after
the absent-mouse timeout on the standard KBC A4h password query, before FDC
access, with zero DMA units. The coordinator is necessary infrastructure, not
yet a media-boot claim. The CLI can attach one external raw 1.44MiB image through an immutable
in-memory, write-protected drive. It accepts no other size and never gives the
controller a host path. The canonical clean/local-only MS-DOS image remains
outside Git. Writable overlay support and a real floppy boot remain pending.
A4h now returns F1h for the unmodeled/no-password state; password loading is not
fabricated.

The private [peripheral services owner](../../doc/architecture/pcs286-board-services.md)
now owns the PIT/RTC/pair clocks and port61/checks/refresh wiring. Actual CPU
IN/OUT, PIC interrupts, refresh ownership, NMI masking and explicit epoch-reset
recovery are tested together. Peripheral time and CPU boundaries remain separate;
no undocumented CPU duration is supplied to the engine. This completes the
bounded peripheral coordinator; full machine lifecycle, DMA service, remaining
devices and GC103/IOC02 qualification are still pending.

GC103 EMS memory-context selection now follows the manufacturer reference:
programming an inactive register context leaves live memory decode unchanged.
The classic upper-window artifact is explicitly corrected and regression-tested,
including real backing writes across context switches. See the
[Headland evidence and remaining qualification](../../doc/architecture/pcs286-headland.md).
This does not establish the exact PCS286 chip population or open public Headland
acceptance. Shadow source selection also follows the manufacturer's EMS pointer
table: F/FF windows read backing E, and E/FE windows read backing F. Independent
tests populate every byte through those EMS pointers, verify both read-only
aliases and retain the raw classic comparison with explicit divergences.
Actual straps, reset and write control through other aliases remain unqualified.
An opt-in standalone GC103 register profile now separates the software CR latch
from readback through explicit RAM1M/RAMSW1/RAMSW2/SPLSW inputs. The legacy
fixed-population mapper still refuses register-only substitution. A separate
configured initializer now integrates explicit uniform DRAM density and installed
banks with floating RAMSW/SPLSW software control. Live CR decode, bounded backing,
EMS and the explicit CONFIGURED_GC103 AT adapter are covered by 15,741,444 route
checks and actual storage/failure tests. Tied decode precedence, selected 512KiB
linear behavior and density-mismatch aliases remain unsupported; exact board
wiring/reset and alias write protection remain pending. Component capacity up to
8MiB does not change the PCS286 onboard 4MiB contract. See the Headland document
for documented rules versus retained inference and the precise exclusions.
Historical configured-geometry validation, GCC UCRT64 Debug/Release: 153 passes and one explicit Headland
skip (154 registered); Python tools: 50 passes. An invalid context argument in
the board-control strict-clock test was corrected and its refusal/reset checks
strengthened; CPU behavior is unchanged.

Development priority, 2026-09-25: use the existing corrected classic Headland
profile as the provisional functional baseline for a minimal executable board,
an early traced attempt with the preserved real BIOS, then DMA/device integration
for POST and media boot. The optional configured-geometry profile and unresolved physical
straps/reset details do not gate this work. Record each adopted approximation
and refine it when evidence or an observable failure calls for it; retain memory
bounds and host-error handling. See the
[functional integration decision](../../doc/architecture/pcs286-headland.md#functional-integration-priority-2026-09-25-user-decision).
This changes the work order only; no new boot, timing or public-component result.

RTC/video update,2026-09-26: the private BIOS probe now offers explicit valid
calendar initialization and optional existing PVGA1A composition. Clocked VGA
status is opt-in; PCS86 keeps its prior default/reset behavior and renderer.
Real BIOS1.42 proceeds to a keyboard FFh write after ADh; IDLE from that endpoint
still stops execution. No completed POST/visible screen/Setup/DOS or public
factory. See the [probe record](../../doc/architecture/pcs286-boot-probe.md) for
configuration, coverage, observed counts and the next keyboard/DMA/storage work.
