# Coordinated AT keyboard and controller

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`bm_at_keyboard_pair` now composes the existing native keyboard and 8042 into
one bidirectional, clocked subsystem. Actual commands, bytes and inhibition
flow between those devices. The existing shared PIT/RTC clock adapter attaches
the pair to the engine; no second scheduler, extra FIFO, cycle-polling loop or
fabricated ACK was added. Production PCS286 board A20/reset wiring and the
machine factory remain pending; this is not a portable firmware boot result.

## Reuse and explicit timing profile

The pair owns the attributed [native controller](pcs286-at-kbc.md) and
[native enhanced keyboard](pcs286-at-keyboard.md). Their command/scan/translation
implementations and authors remain. The classic sources are unchanged. Hardware
evidence remains IBM6280070 for the controller and IBM6183355 for the enhanced
keyboard; the originals remain local-only in the canonical library.

Both configurations must use the same **logical service rate**, with equivalent
rational fractions accepted. Native delays must be expressed in that domain.
This is a selected functional timing profile, not a claim that the real MCU and
keyboard share an oscillator. Unequal rates reject explicitly; asynchronous
oscillators/resampling are not implemented by this pair. Existing independent
attachments remain available, with their documented peer-coordination limits.

Internal send/command/inhibit callbacks and their contexts must be empty in the
supplied configurations. The pair installs its own wiring. IRQ, A20 and CPU-reset
callbacks remain explicit, required, fallible board outputs. Construction
publishes no callbacks and starts with the keyboard inhibited consistently with
the controller's initial command byte. No native mutable handles escape.

## Common-boundary execution

Each advance jumps to the earliest active deadline or the requested endpoint:

1. Both devices consume that elapsed interval without publishing effects.
2. The controller settles pulse restoration, input consumption and output
   publication in its existing order.
3. The keyboard settles its phase, transmission and typematic events, taking
   account of controller commands or inhibition just applied at this boundary.

Controller-before-keyboard is a declared deterministic functional policy for
simultaneous events, not measured silicon priority. All effects see equal native
cycle counters. Commands can replace a due reply/scan or cancel reset acceptance;
inhibition can suppress a due send. The keyboard checks the captured phase and
current queue/timer/inhibit state before executing those old pending actions.
Both devices' ordinary standalone advance functions reuse the factored private
elapse/settle helpers, preserving their prior tests and behavior. These helpers
are not a public partial-step API.

Keyboard output reaches the already-settled controller directly. Its immediate
inhibit feedback uses the native keyboard's existing narrow send-time exception,
without recursively executing either peer or calling a clock wrapper. IBF/OBF
backpressure retains unsent bytes. No additional wire delay is inserted; only
the explicit native service intervals already configured are used.

Tests make the policy observable: F5 at a scan deadline cancels that scan and
produces a real keyboard ACK later; AD inhibits a due send until AE releases it;
FF during BAT is retried through retained IBF, and another FF at the acceptance
deadline cancels the old reset phase. A 64-profile input/byte/output-delay matrix
covers earlier, equal and later input consumption, including repeated IBF
backpressure before acceptance.

## Engine, I/O, input and lifecycle

Attach with `bm_at_keyboard_pair_attach_clock`, map `bm_at_clock_link_io`, and
deliver key events through `bm_at_keyboard_pair_clock_input`. One source owns
the pair's cursor and next deadline. Ordinary I/O and input synchronize both
devices first, including from the engine's effective CPU instruction boundary.
DEBUG and state inspection remain pure and can show lazy state until sync.
Do not separately attach or advance the encapsulated devices.

Mutating callbacks/reentry reject. Creation/attachment failure releases partial
allocations and publishes no dangling source. Destroy engine, then link, then
pair. Premature destruction of an attached or busy pair is refused.

For a healthy whole-peripheral reset: sync, pair reset, changed at the same
boundary. For an engine epoch reset: engine reset then every link reset before
execution or signals. Native lifetime counters survive; pending protocol and
sticky native failures are cleared according to the native reset contract.
CPU-only reset preserves this subsystem. The board must latch a requested CPU
reset for a safe architectural boundary, never reset a running CPU from an
output callback.

## Failures and validation

A native/output failure stops the pair at the consumed common boundary. Already
accepted effects and partial output publication survive; pending work is never
replayed or converted into a guest response. Sticky native state distinguishes
a host CAPACITY failure from transport backpressure. Reset publication itself
can fail and stop. A clock scheduling error retains the accepted native mutation
and remains in the clock link; the pair's snapshot alone is not a query for
adapter-only failure. Ordinary rejected I/O does not poison healthy state.

`pcs286-component.keyboard-pair` tests native and real-engine protocol paths,
ACK/BAT/FF hold and release, LEDs/ID83→41, translated make/break, the simultaneous
events above, 64 delay profiles, pure DEBUG/reentry, randomized native and engine
partition equivalence, actual cascaded PIC IRQ1/INTA/vector31h/read/EOI, exact
fractional IRQ time, CPU-dispatched I/O and host input, CPU-only reset preservation,
allocation failures at every construction stage, failed/duplicate/capacity-limited
attachment, rate/configuration rejection, native overflow, warm/engine resets,
premature destruction, scheduling overflow and before/after failures on IRQ,
A20 and CPU-reset outputs (including host CAPACITY). Failed reads retain caller
transactions while accepted device effects remain visible.

Synthetic test rate is 3000000/2Hz, with equivalent 1500000/1Hz keyboard input.
POR3/BAT5/byte2/acceptance3 and input2/output3/self-test5/pulse6 cycles are scheduler
oracle values, not physical hardware timings. The old native, individual-clock,
CPU, chipset and other device tests remain intact.

Local Debug/Release:147 pass plus one intentional public Headland skip
(148 registered); Python50; provenance56 components/289 files without errors;
catalogue32 machines/five locales. No new MSVC, ASan, GUI, physical hardware,
authentic MCU, BIOS/media execution or remote CI evidence. Strict CPU timing
remains gated; Headland mem2mem stays disabled. All work is local, without
commit, push, PR, CI change or merge.

Next: board composition of A20/reset/IRQ/input, preserving memory/CPU ownership
and safe reset boundaries. The full machine coordinator, storage, audio and
PCS286 qualification remain separate. The enhanced keyboard's missing input
identifiers and normal-set2 Pause discrepancy, and the controller's low-DATA/
diagnostic/serial/Mitsubishi243 limitations remain explicit.

The private [board control owner](pcs286-board-control.md) now connects IRQ1,
A20 and safe CPU reset using this pair. Full board scheduling/lifecycle remains
pending; no strict CPU timing or machine registration follows.
