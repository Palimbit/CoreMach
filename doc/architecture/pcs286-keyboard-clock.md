# Individual KBC and keyboard engine clocks

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

The native [8042](pcs286-at-kbc.md) and [enhanced keyboard](pcs286-at-keyboard.md)
now have working individual engine attachments. They reuse `at_clock.c`, the
same exact cursor/deadline adapter already used by PIT and RTC. No second
scheduler, polling loop, CPU timing implementation or keyboard protocol was
introduced. A separate [coordinated pair](pcs286-keyboard-pair.md) now supplies
bidirectional wiring under an explicit common service-clock profile. Production
PCS286 board composition remains pending.

## Reuse and ownership

`bm_kbc8042_attach_clock` and `bm_at_keyboard_attach_clock` validate the requested
rate against the native component's configuration. Equivalent rational rates
are accepted using reduced fractions, without overflowing cross-products.
Different or zero rates reject before allocation. Attach requires engine time
zero, an unadvanced native component and no existing clock owner. All fallible
construction completes before publishing ownership; source-capacity failure
leaves no live callback to freed adapter state.

Native layouts moved into private headers; protocol behavior is unchanged.
Original native authors/licenses remain intact. The attached components refuse
premature destruction. Required teardown order is engine, links, then devices;
the engine has no unregister API. The existing adapter derives from the portable
8253 clock implementation at `4769e40524bc194747b142f3e7ec908ae4df0897`.
New typed input wrappers are authored composition code, not external emulator
code. Hardware semantics and evidence remain those of the linked native models.

## Synchronized inputs and observations

The KBC is mapped through `bm_at_clock_link_io`. Before ordinary I/O, the link
consumes the elapsed native edges and afterwards arms the next actual deadline.
DEBUG bypasses synchronization and stays observational. A rejected ordinary
access still accounts for elapsed time, but does not publish a caller result.
The keyboard has no I/O ports: its I/O wrapper returns UNSUPPORTED.

Typed owner-boundary operations are:

- `bm_kbc8042_clock_receive` for an externally supplied keyboard byte;
- `bm_at_keyboard_clock_command` for a controller command;
- `bm_at_keyboard_clock_inhibit` for the semantic inhibit level;
- `bm_at_keyboard_clock_input` for a host-neutral key event.

Each performs one guarded sync/apply/rearm sequence. A wrong link type rejects
unchanged. Ordinary unsupported inputs or backpressure do not poison the link;
native callback failure or scheduling failure does. Accepted effects survive
a later scheduling error, and failed elapsed intervals are never replayed.
No wrapper supplies an ACK or converts a host error into a guest response.

Deadlines use the engine's exact effective scheduling boundary, including
instruction-start time inside clocked CPU dispatch. Native state may be lazy
while a source is idle; read its snapshot after explicit sync when a current
snapshot is needed. Read-only debugging never silently advances it.

## Reset and composition limits

For a healthy peripheral reset, synchronize, reset the native device and call
`changed` at the same boundary. Native lifetime cycles survive; pending protocol
work is cancelled according to the device contract. For an explicit engine
epoch reset, reset the engine then reset every borrowed link before any run or
signal. Resetting only the device cannot clear a failed clock link. CPU-only
reset preserves engine, links and keyboard/controller state.

These typed functions are not recursive bridges between two attached peers.
A KBC callback that synchronizes the keyboard can encounter a simultaneous
keyboard output that calls back into the busy KBC. Mutating a lazy peer directly
would instead apply the new input retroactively to elapsed clocks. Both are
incorrect integration strategies. Callback mutation/reentry remains refused;
native inhibit feedback during a byte send retains its narrower documented
exception, requiring an already synchronized composition owner.

The coordinated pair now establishes those boundaries with private silent
elapse of both peers followed by ordered settlement. Its dedicated tests cover
coincident input/output/inhibit, partition equivalence, FF ACK/BAT and host
failures. Use that owner for the supported common logical rate; do not cross-wire
these individual wrappers recursively. Board A20/reset requests, full scheduling
and the PCS286 factory remain separate. No portable boot is claimed.

## Validation

`pcs286-component.keyboard-clock` uses an explicit synthetic 1500000Hz profile
with exact fractional nanosecond edges. It tests POR/BAT, byte delivery,
typematic after inhibition, idle-input rearm, controller self-test/input/output/
reset-pulse timing, real PIC IRQ1/INTA/read/EOI, command backpressure without
invented ACK, pure DEBUG, rejected and mistyped input, allocation/capacity/rate/
attachment checks, warm and engine resets, retained lifetimes, callback reentry,
host failures before/after effects and during typed receive, deadline overflow,
instance isolation, random run partitioning and clocked CPU input boundaries.
Two independent devices also share one engine; this is not peer wiring.

Individual-clock block Debug/Release:146 pass plus one intentional public Headland skip
(147 registered). Python50, provenance55 components/286 files without errors,
catalogue32 machines/five locales. The first new CPU fixture omitted mandatory
reset/signal callbacks; correcting the fixture made its boundary test run.
No new MSVC, ASan, GUI, hardware, firmware/media, remote CI or physical timing
evidence. Strict CPU timing remains gated; Headland mem2mem stays disabled.
All work remains local without commit, push, PR, CI change or merge.
