<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# RTC attachment to the engine clock

`bm_at_rtc_attach_clock` now implements the native RTC clock connection in
`blumach_at_engine_adapters`. It uses the engine's existing exact32768/1 Hz
source cursor and the [functional RTC](pcs286-at-rtc.md), without changing CPU,
engine time arithmetic, PIC, DMA or the classic implementation. This is a
component attachment, not a complete or bootable PCS286 board.

## Reuse and scope

The cursor, event callback, synchronized I/O, rearming and ownership logic from
the already tested `pit8254_clock.c` is shared in `at_clock.c`. Private static
device operations bind it to PIT or RTC; no new public generic device ABI or
second scheduler is introduced. PIT register/edge behavior is unchanged. The
original derivation from `pit8253_clock.c`, pinned at
4769e40524bc194747b142f3e7ec908ae4df0897, remains in the provenance inventory.
RTC's only native-core change is moving its state definition into a private
header and adding one attachment-owner pointer.

Manufacturer evidence and limitations remain those of the native RTC delivery:
Motorola MC146818A ADI1026R3 tables5/6, figure15 and register definitions,
asset `motorola-mc146818a-datasheet`, plus the IBM5170 register context. No new
document acquisition, copied restricted material, ROM or media is needed here.
Exact engine fractions preserve the **implemented** RTC pulse intervals; they
do not turn its quantized UIP interval or chosen initial phase into measured
silicon timings. DSE, other bases/test modes, exact PCS286 part/128-byte profile
qualification and the unconnected SQW output retain their stated limitations.

## Synchronization and ownership

Attach at engine time zero to a RTC that has not advanced native cycles. Initial
CMOS/register programming at zero is permitted. Only one link can own a device.
The next native deadline is computed first; engine source registration is the
last fallible construction step. Failure leaves no claimed device, published
callback or retained link allocation. The existing engine has no unregister:
destroy it and unmap I/O before releasing links and borrowed devices.

Map `bm_at_clock_link_io` at70h–71h. Every ordinary access first advances all
elapsed native edges, even while the source has no scheduled event or the
register access will be rejected. The staged caller transaction is published
only after device I/O and rearming succeed. DEBUG reads bypass synchronization
and all destructive read effects; they intentionally observe the last serviced
state, not a guaranteed current wall-clock value. State/export callers wanting
fresh CMOS must synchronize explicitly first. Never separately advance a linked
device.

The source schedules UIP/update boundaries and the next uncaptured PF. Sticky
PF can make a SET-stopped calendar's source idle, but the engine's rational
oscillator cursor continues. Reading C clears PF and rearms its next phase-aligned
edge. RS/DV/SET changes similarly recalculate deadlines without moving the engine
clock origin. The clock is not polled every pulse. CPU I/O uses the engine's
exact instruction-dispatch cursor; the global committed `now_exact` timestamp
can differ during an in-flight CPU step.

## Reset and failure contracts

For a **warm board RTC reset**, at one boundary: synchronize the link, call
`bm_at_rtc_reset`, then `bm_at_clock_link_changed`. Stop on any failure. This
retains calendar, CMOS, update state, integer divider phase and fractional engine
phase while applying documented register/output reset effects. A callback may
inspect DEBUG/state but cannot mutate, reset or destroy either active object.
CPU-only reset leaves the engine, RTC and its link alone.

A **full emulator epoch reset** is a separate lifecycle action. Synchronize
every healthy RTC link before resetting the engine, then reset every link at
exact engine time zero before any run/access. This avoids losing unserviced
elapsed native edges. RTC link reset invokes its warm device reset and rebases
the link's serviced engine cursor to zero; it does not clear battery-backed
calendar or the lifetime native-cycle count. The integer divider/update phase
survives. Engine reset starts a new fractional oscillator epoch, an explicit
emulator policy, not a physical warm reset. PIT retains its existing reset
semantics. No decreasing/equal/increased cursor is used to guess a new epoch.

Native advance, output and scheduling errors latch at the link; consumed time
and already accepted endpoint/register effects are retained. No failed interval
is retried or converted to a guest exception. In particular, a failing read-C
output can leave C cleared while the caller's read result remains unchanged.
Register-only rejections are nonsticky; an undefined-calendar failure encountered
while advancing time is sticky at the link and stops execution. Directly
resetting the native device cannot silently erase the link's failure.

An explicit full reset can recover output/scheduling failures when the host
condition has been resolved. A failed reset is also retained, and the owner
must stop until a successful recovery/reconstruction. Reset after a partial
failure does not replay or manufacture the unconsumed suffix. As for PIT, an
engine-owned deadline-overflow error returned after source firing requires the
owner to stop; the link cannot claim to have observed an internal engine failure
that occurs after its callback returns.

## Validation

`pcs286-component.at-rtc-clock` exercises exact rational times before/at UIP,
actual transfer and completion; read-C reactivation after long idle periods;
off-edge programming; stopped/depleted CMOS programming; DEBUG purity; rejected
I/O; warm reset during update and explicit epoch reset with larger new cursors;
allocation/capacity/duplicate/busy/late/previously-advanced attachment failures;
IRQ publication failure before and after the recipient, failing C acknowledgment,
sticky native failure, reset failure, reentry and deadline overflow. Authored
CPU steps at65536 Hz verify elapsed RTC edges and periodic acknowledgment at
instruction boundaries; this is not a claim of clocked public286 execution.

Real PIC IRQ8/vector70h delivery and real checks/NMI masking use the clocked
register path. A PIT and RTC share one engine while RTC results and output
timestamps are compared with independent partitioned runs. Existing PIT tests
retain their2160 fine/coarse comparisons, and native RTC tests retain their336
calendar rollover cases. One initial new test confused committed engine time
with CPU dispatch time; its expectation was corrected without modifying engine
code or device semantics. GCC16.2 UCRT64 Debug and Release each pass143 tests
with one public Headland skip (144 registered). Python50; provenance53 components/
277 files without errors; catalogue32 machines/five locales; diff check passes.
Commands and results are recorded in the canonical dated worklog.
No hardware, firmware, media, GUI, MSVC, ASan or CI evidence is
claimed by these tests.

Next: KBC/keyboard contracts and reusable classic implementation, then full
board scheduling and qualified memory/error sources. Existing Headland public
acceptance remains pending, mem2mem stays off and the CPU strict-clock gate
remains closed. Work remains local, without publication.
