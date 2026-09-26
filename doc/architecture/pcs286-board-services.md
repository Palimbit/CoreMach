# Functional board peripheral coordinator

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

The private `bm_pcs286_services_inspect_rtc` diagnostic copies native RTC state
and configured64/128-byte CMOS through existing pure inspection APIs, including
after a stopped peripheral clock. It exposes no mutable child, performs no
synchronization or read-C/D side effects, and stages outputs on invalid size.
The local BIOS probe uses it to distinguish calendar failures from the CPU's
last I/O access. Tests preserve stopped time/failure and caller data on refusal.

`blumach_pcs286_services` owns the existing PIT, RTC, keyboard/8042 pair, their
three clock links and a private peripheral engine, plus port61, checks and
refresh. It borrows the [board control owner](pcs286-board-control.md) and its
actual 286, AT bus, PIC and Headland memory. There is no second device model or
scheduler implementation, and no CPU is registered with this engine.

## Evidence and limits

This is authored composition of the already documented components. The AT
connections come from their existing IBM5170/8254/MC146818A contracts; the
GC102/GC103 applicability limits in [Headland](pcs286-headland.md) remain.
No new physical wiring, oscillator identity or measured CPU timing is claimed.
Original component authorship/licenses are unchanged. No firmware, external
media or restricted document is copied into Git or executed.

The owner is a private diagnostic integration boundary, not the public machine
factory. GC103/IOC02 qualification, DMA transfer scheduling/external cards,
storage/video/audio integration and complete machine lifecycle remain separate.
Headland mem2mem stays disabled and the CPU strict-clock gate remains closed.

## Construction and wiring

The caller supplies the PIT rational clock, RTC capacity/initial bytes and
keyboard/controller timing/straps explicitly. No host calendar, BIOS-specific
delays or guessed Olivetti keyboard profile is selected. Internal callbacks and
contexts in supplied RTC/pair configurations must be NULL; the owner installs
these connections and rejects conflicting callbacks. Port bases are fixed to
the selected private AT profile.

| Source | Existing destination |
|---|---|
| PIT OUT0 | PIC IRQ0 |
| PIT OUT1 | Refresh edge capture and AT ownership request |
| PIT OUT2 | port61 raw status and digital speaker gate |
| RTC IRQ | PIC IRQ8 |
| RTC address bit7 | Checks global NMI mask |
| Checks NMI | Board control NMI endpoint |
| Keyboard pair | Board control IRQ1, A20 and reset |
| port61 status | Checks bits6/7 OR completed-refresh bit4 |
| port61 writes | Existing PIT GATE2, speaker data and check-enable paths |

Map exact byte resources40h–43h,60h–61h,64h,70h–71h to `services_io` through the
existing board decoder. No62h/63h/92h aliases are added. Normal I/O synchronizes
all clocks before access and checks them again afterwards for retained scheduling
errors; ordinary rejected registers remain nonsticky. Caller results are staged
on failure. DEBUG reads are observational and lazy; DEBUG writes reject.

Construction requires an idle, healthy borrowed control/bus, allocates all
owned objects and publishes initial PIT IRQ0, RTC and pair outputs only after
construction succeeds. If publication fails, its accepted receiver effects
remain, but all engine/link/device allocations are released. Recipients and host
services outlive destruction. Destruction releases engine, links, then devices;
it does not invent reset transitions. Disconnect/reset recipients if needed.
One owner must exclusively coordinate the borrowed CPU/bus and these devices.

## Time and functional boundaries

`advance(ns)` moves peripheral time only. `step()` synchronizes peripherals and
performs at most one real CPU/control boundary. It reports separately whether
a CPU boundary/reset and/or refresh completed, returning IDLE when neither did.
No CPU instruction duration is inferred from bus waits or a test loop. CPU time
remains UNKNOWN; a NOP can complete with unchanged peripheral time, and advancing
peripherals does not change CPU IP.

PIT output callbacks capture and request refresh immediately, not at the end
of a long advance. They never execute the CPU or force HLDA. Existing DMA/ISA
ownership and LOCK defer service. Before/after the CPU boundary, the coordinator
services the request through the existing arbiter. A HOLD boundary can therefore
complete refresh without a fetch; REF DET changes only on completion. Pulses
while waiting coalesce as already specified by the refresh component. Held
reset leaves the CPU without a new HLDA; peripherals can still advance. No
physical reset bus-tristate timing or refresh duration is inferred.

The three existing timed sources are registered PIT, RTC, pair. Engine tie order
therefore follows that registration order; explicit sync uses the same order.
This is a deterministic composition policy, not physical chip priority. The
pair retains its own previously qualified KBC/keyboard coincidence policy.
Inputs arrive at the current boundary after synchronization. Qualified parity
and external IO-check signals use dedicated APIs; host failures never generate
either source.

Callbacks can inspect state/DEBUG. Mutating reentry, recursive I/O, advance,
step, reset and destroy from the digital speaker observer are refused. CPU I/O
is admitted only while the owner is executing that CPU boundary. Caller-supplied
CPU observers must still respect the existing no-mutation callback contract.

## Lifecycle and errors

CPU-only reset preserves all peripheral time/state and uses the existing control
resampling of INTR/HOLD/NMI. It can recover a CPU stop but cannot repair a failed
device/link. The service state reports peripheral failures; CPU failures remain
in the borrowed control state. An error returned by a CPU boundary does not
trigger another boundary, reset or instruction replay.

`reset_epoch(recover=0)` first synchronizes healthy elapsed time. It cancels its
own refresh request, resets CPU/bus/PIC, restarts the private engine epoch and
resets all three links plus checks/refresh/port61, then resamples CPU signals.
It retains RAM/Headland maps, DMA programming, RTC calendar/CMOS and integer
divider phase, keyboard lifetime counters and external IO-check level. A20
returns to the configured KBC output-port reset value. Fractional clock phase
restarts with the engine epoch: an emulator lifecycle policy, not a warm physical
board reset. Speaker transitions during this explicit reset order are observable.

An outstanding DMA/ISA request rejects epoch reset before effects: the caller
must quiesce its external owner. The operation does not reset DMA/cards and is
not advertised as full-machine reset. An owned refresh request or CPU LOCK can
be safely cancelled/released as part of this explicit operation.

Clock/endpoint failures are retained; inspection remains available. A rejected
overflowing duration has no accepted effects and returns INVALID_ARGUMENT
without poisoning the owner. Overflow after accepting a register change, such
as scheduling its next edge, remains a sticky failure with that change retained.
Void PIT output failures can only be observed after the synchronous engine/link
call returns; already completed effects in that call are not rolled back.

`reset_epoch(recover=1)` explicitly abandons unfinished failed intervals at the
retained component prefixes, then resets the complete owned clock group. It
does not replay elapsed work or sanitize retained CMOS values. If resetting
fails, its partial effects remain and the owner stays stopped. There is no
automatic recovery or host-error-to-guest-exception conversion.

## Validation

`pcs286-component.board-services` shares the existing authored CPU/AT/Headland
fixture with `board-control`; their common setup was extracted into a test-only
header, with original tests retained. The new fixture uses RAM2MiB, explicit
1MHz PIT, 32768Hz RTC with128-byte extension and the existing synthetic1.5MHz
keyboard service profile. Those rates/delays are test inputs, not PCS286 timing
recommendations. Checks include:

- real CPU IN/OUT programs configuring PIT1 and reading61h; refresh while HLT,
  no time fabricated for CPU instructions, no CPU execution during advance;
- IRQ0 and IRQ8 through actual cascaded PIC/INTA/286/IRET, parity masking via
  RTC70h and clear via61h, raw OUT2 versus digital speaker and callback reentry;
- keyboard/BAT/host key translation, KBC reset/held-reset peripheral advancement,
  epoch reset with pending refresh, LOCK REP cancellation, retained RAM/CMOS/
  DMA pages/Headland CR0 and external IO-check, and external-owner reset refusal;
- 32 deterministic partition profiles, four service boundaries each, comparing
  outputs/REF DET/requests and speaker transitions for whole versus fragmented time;
- all13 allocation failures and initial A20 publication failure with complete
  cleanup; invalid configuration/arguments, nonsticky register rejection/DEBUG;
- actual invalid-calendar RTC failure at its fractional deadline, no guest NMI
  or silent calendar repair, no replay and explicit recovery; accepted PIT count
  followed by scheduling overflow with retained failure and recovery.

Initial validation corrected a test-header extraction qualifier, an expectation
that every25us interval contained refresh for divisors above25, and an overflow
test that confused invalid run duration with accepted-state scheduling failure.
The owner now preflights overflowing run durations without mutation. No existing
chip, CPU or engine behavior was weakened or rewritten.

Current GCC16.2 UCRT64 Debug/Release:149 pass and one intentional public Headland
skip (150 registered). Python50, provenance58 components/296 files, catalogue32
machines/five locales. No MSVC, ASan, GUI, remote CI, hardware or firmware claim.

Next: assemble a minimal functional runner, attempt traced real-BIOS execution,
then integrate DMA service and devices needed for POST and media boot using the
existing owners and corrected classic Headland profile. The
[functional integration decision](pcs286-headland.md#functional-integration-priority-2026-09-25-user-decision)
keeps provisional choices explicit and moves hardware qualification to an
incremental fidelity backlog unless it prevents required behavior. Public
Headland integration and portable boot remain pending. Everything stays local.
