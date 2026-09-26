# Portable 8254 and the PCS286 timer boundary

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

The reusable `blumach_pit8254` component implements functional timer registers,
input-clock stepping and output transitions. It reuses the existing portable
PIT edge engine and the classic 8254 implementation. This is not a claim that
the PCS286 contains a discrete Intel package, or a complete board scheduler,
refresh circuit or speaker. RTC and KBC still have separate pending AT contracts.

## Evidence and reuse

The primary reference is Intel *8254 Programmable Interval Timer*, September
1993, order 231164-005, [public university mirror](https://www.cs.cmu.edu/~410/doc/8254.pdf).
The 21-page original stays outside Git, canonical asset
`intel8254-datasheet-1993`, SHA-256
`e5397a387d24fff21c8b531a320251f89b103274bbce0c057a654873e494d726`.
Pages 6–17 were read; figures 10–12 on pages 8/9, the odd-divisor text on
page 13 and gate/count limits on page 17 were visually checked. Generic Intel behavior does not establish the
identity, wiring or propagation delays of the integrated PCS286 timer.

The component calls `pit8253_exact.c`, already ported from the functional
classic implementation. Its default 8253 selection and public wrapper remain
in place. The explicit 8254 selection restores classic status read-back and
even internal counts for odd square-wave divisors. The new wrapper is adapted
from the portable 8253 wrapper. Miran Grca, Daniel Balsom, Clara and BluMach
notices and existing GPL/MartyPC-derived provenance are retained.

`src/pit_exact.c` and its header at commit
`4769e40524bc194747b142f3e7ec908ae4df0897` compile unchanged into a test-only
reference target. Their normalized SHA-256 pins are respectively
`9fae55a7f1e7620d1946a33f2d07116e3f966167b6ad788e5498e8ccc3cd21a5` and
`8cd027bb3259abcb59213a4951c964c986d44a715e045cba144eff895086a857`.
No classic source is modified or linked into the portable production component.

## Register and edge semantics

| Boundary | 8254 behavior implemented from Intel |
|---|---|
| Read-back | D3:1 select counters; active-low D5/D4 latch count/status independently. An unread latch retains its first value. Status precedes count without consuming count phase. |
| Status | OUT, NULL and the exact programmed low six control bits, including mode aliases. |
| NULL | Mode programming and completed count writes set it; actual CR-to-CE loading clears it. The first byte of a two-byte write alone does not set it. |
| Reads | Observe CE/latches; never load a pending count, advance clocks or invert an unfinished write. Read and write byte phases are independent. |
| Mode 3 | Odd divisors use even CE; the high half includes the extra pulse. The current divisor's parity remains active until reload. |
| Strobes | Modes 4/5 produce one strobe per write/trigger respectively. Modes 1/5 require a gate trigger, including after a completed one-shot is rewritten. |

The 8254 path deliberately corrects several inherited policies: read-triggered
CE loading, incomplete-LSB inversion, NMOS pre-trigger count 0003h, using a
partially written CR on a periodic reload, applying new divisor parity before
reload, stale NULL after mode-3 reload, and repeated mode-4/5 strobes on count
wrap. The counter also decrements during the strobe-recovery pulse, as required
by continued counting after zero (page 17). These corrections are scoped to 8254; they do not silently change the
existing 8253 model. The classic differential cases cover their common valid
steady operation; independent authored tests cover the corrected sequences.

The incomplete CR policy retains the previous *complete* value until the MSB
arrives. Unsupported/undefined input policy is explicit: reserved read-back
D0, invalid BCD digits, and divisor one in modes 2/3 return UNSUPPORTED before
the offending byte changes state. Earlier LSB effects remain. Counts are never
silently changed into another divisor. Mode programming must precede writes.
An undefined pre-load CE retains its prior deterministic value; this is a model
policy, not a physical measurement. Analog setup/hold races are outside scope.

## Ownership, timing and error contract

Creation copies configuration, borrows the output context and owns one
allocation. It publishes no mappings or output events. Reset selects a
deterministic unprogrammed/NULL state, low outputs and gates 1/1/0; these are
model/AT initialization choices, not defined silicon power-up values. Reset
reports changed outputs. The owner must detach callbacks/mappings before destroy.

Native I/O is byte-wide. Control-port reads return UNMAPPED (the documented
undriven bus is left to an explicit board policy). The I/O adapter propagates
this installed-device result; it does not turn it into successful FF data.
DEBUG reads peek a copy and consume no latch, phase, clock or output edge;
DEBUG writes are rejected. The supplied wait count is preserved, never promoted
to measured zero-wait operation. Invalid calls preserve transaction/state.

Advance uses complete input-clock pulses and retains every output transition.
Quiet intervals use shared exact batch arithmetic, with a 64-bit elapsed count;
overflow rejects the complete request before effects. Runtime necessarily scales
with the number of observable output edges. Odd mode 3 also has an internal
CE-zero boundary before its output transition; batching must stop at that state
change instead of skipping it.

The pure deadline query reports the next OUT transition, or IDLE/zero if none
can be caused by clocks alone. An idle output does not mean that the counter
or NULL cannot change. The clock link below synchronizes elapsed pulses before
I/O, even when the output source is disarmed. No host clock, CPU/ISA wait conversion, IRQ ownership or
physical board clock wiring is implicit in this component.

Callbacks are synchronous. They may inspect output/deadline/DEBUG state but
may not mutate, advance or destroy the timer. Status-returning mutators reject
reentry; reset/destroy invoked during a callback are ignored. No callback
recursively executes a CPU. Device/host failures do not become guest exceptions.

## Validation and remaining work

`pcs286-component.pit8254` checks:

- 4,608 read-back combinations and 196,608 binary counts across byte formats;
- independent latches, frozen status, interleaved reads/writes, NULL transitions,
  DEBUG purity, mode aliases, strict rejection and reset/isolation boundaries;
- 3,510 authored waveform/deadline pulses over modes 0–5, even/odd divisors,
  delayed rewrites, partial writes and gate behavior;
- 432 single-pulse versus batched comparisons with binary/BCD and three channels,
  plus 64-bit quiet intervals, exact capacity rejection and one-shot wrap;
- 73,728 count/output/status comparisons against the actual pinned classic core,
  explicitly checking the corrected one-count difference after strobe recovery;
- allocation failure/cleanup, callback reentry and real AT decoder/PIC IRQ0.

The composition fixture installs the real timer at 40h–43h using an explicit
byte resource in the private board decoder. It programs PIC and PIT through the
actual AT bus, acknowledges the programming edge, and verifies the subsequent
timer IRQ0 vector. This is a tested composition recipe, not the absent PCS286
factory or a clocked BIOS run.

Initial waveform and chunk tests exposed missing internal odd-mode-3 boundaries
in the first extension; both direct-countdown and pending-load paths were fixed.
A later allocation test used the wrong zero-based failure index; only that
fixture was corrected. Final manual review found the inherited missing decrement
on strobe recovery; the 8254 path was corrected and independent count assertions
extended across modes 0/1/4/5, followed by fresh full Debug/Release runs. Failed
and pre-correction logs are retained. Existing 8253 clock/batch
regressions pass. Final GCC 16.2 UCRT64 Debug and Release each pass 137 tests
with one public Headland skip (138 registered). Python tools: 50 pass;
provenance: 48 components/259 files, zero errors; catalogue: 32 machines/five
locales; diff check passes. No new MSVC, GUI, ASan, CI or hardware result.

Next: integrate the clock link below with board 61h glue, then RTC and KBC
using existing classic sources where appropriate. Headland/IOC02 qualification,
refresh wiring, audio rendering and physical timing remain unresolved. The CPU
strict-clock gate stays closed, DMA mem2mem stays disabled for Headland, and
public Headland acceptance remains skipped. No firmware, media, restricted
document, commit or publication is added to Git.

## Engine clock link

`blumach_at_engine_adapters` implements the PIT attachment and common
`bm_at_clock_link_*` operations from `at_clock.h`. KBC/keyboard/FDC/ATA
attachment declarations still have no implementations. This is the timer part
of the time coordinator, not a complete PCS286 board factory.
The later [RTC clock attachment](pcs286-at-rtc-clock.md) shares this adapter's
cursor/deadline implementation in `at_clock.c`; PIT's binding and semantics
are unchanged. RTC uses the same synchronized I/O and ownership discipline.

The link adapts `pit8253_clock.c` at the pinned commit above, retaining its
BluMach/GPL attribution and the existing engine's exact source-edge cursor.
It owns one allocation and borrows the PIT and engine. The caller supplies the
rational native clock; no PCS286 oscillator frequency is asserted by this API.
Only one link can attach to a PIT. Attach requires engine time zero and no
previously advanced PIT pulses; register programming at zero is allowed.
The initial output deadline is computed before registration, which is the
last fallible construction step. Failed attachment leaves no published callback,
slot or dangling allocation. The existing engine has no source unregister.

Map `bm_at_clock_link_io`, not raw PIT I/O. Every ordinary access first advances
the elapsed native pulses, even if OUT has no deadline. DEBUG reads inspect the
existing state without advancing or consuming anything; they do not promise a
fresh value at the engine's current time. After a normal access, the next OUT
transition rearms the source on its original rational phase. There is no polling
of every input edge and no conversion through host elapsed time.

For an external gate change, call `sync`, change the gate, then call `changed`
at the same engine boundary. `changed` checks that the cursor is already current;
it never applies a new gate retroactively to an elapsed interval. Before direct
state inspection, synchronize explicitly. Never also advance an attached PIT.
The engine already defines CPU accesses at exact instruction-start boundaries;
this adapter preserves that policy, without claiming individual bus-phase timing.

Full-machine reset has an explicit sequence: reset the engine, reset every link,
then resume access or execution. Link reset resets the PIT and serviced cursor
and disarms the registered initial deadline. It requires exact engine time zero.
Engine reset alone does not reset these borrowed devices. A decreasing cursor
is rejected rather than guessed to mean reset; equal or larger cursors cannot
identify an unobserved epoch, so the sequence is an owner obligation. CPU-only
warm reset preserves the engine epoch and PIT. Reset is never performed from
inside a CPU, event or pin callback.

Clock/device failures seen by the link latch until explicit full reset; no
elapsed interval is retried. Register rejections do not poison the link, but
elapsed-time synchronization still occurred. I/O stages the caller transaction
until device access and rearming both succeed. Previously published outputs or
accepted register changes remain on scheduling failure. The owner must stop
on an engine run error, including an engine-owned deadline overflow after a
callback. None of these host failures becomes a guest exception. Callbacks may
inspect DEBUG state; mutations reject reentry and callback-time destroy is
ignored. Teardown destroys the engine and mappings before the link, then the
borrowed PIT; there is no pretend detach from a live engine.

`pcs286-component.pit8254-clock` adds 2,160 fine/coarse execution comparisons
across six modes, binary/BCD, three channels, gates and split count rewrites.
It checks exact crystal fractions after off-edge programming and gate restart,
simultaneous outputs and isolated devices on one engine, lazy 64-bit intervals,
idle count wrap, DEBUG purity, callback reentry, CPU instruction-start cursors,
explicit reset epochs (including a new cursor larger than the old one), and
CPU-only reset preservation. Allocation, capacity, duplicate/late attachment,
initial-deadline overflow, retained register effects on rearm overflow and a
test-only injected advance failure exercise cleanup and error paths.

The existing AT/PIC fixture now runs both directly stepped and engine-clocked:
the real private I/O decoder maps the link at 40h–43h, PIC programming and PIT
accesses use the AT bus, and output transitions deliver IRQ0/vector 20h.
This remains an authored integration test, not BIOS execution or physical
refresh/audio validation. No engine, CPU, DMA, PIC, classic source or timing
gate was changed. Port 61h/refresh/speaker and the rest of the board coordinator
remain pending.

Validation of the clock-link delivery: GCC 16.2 UCRT64 Debug and Release each
pass 138 tests with one public Headland skip (139 registered). The existing
8253 clock/batch tests and 8254/classic comparison remain green. Python tools:
50 pass; provenance: 49 components/261 files, zero errors; catalogue:
32 machines/five locales; diff check passes. This adds no MSVC, GUI, ASan,
CI, hardware or firmware validation result.

The subsequent [port61 signal adapter](pcs286-port61.md) supplies GATE2/OUT2
and digital speaker transport. Documentation overrides the classic PIT1-based
refresh toggle: REF DET/error bits require explicit board inputs. The later
[functional refresh coordinator](pcs286-refresh.md) supplies pending requests,
HOLD/HLDA exclusion and logical REF DET events without modeling physical DRAM.
Parity/NMI and the complete board scheduler remain pending.
