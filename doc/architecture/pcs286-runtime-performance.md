# PCS286 runtime performance measurements — 2026-09-26

## Reproduction

### Controlled rearm retention check (2026-09-27)

User observed145% versus150% in GUI; therefore the candidate was rechecked.
First ABBA with GUI paused (zero measured GUI CPU consumption) favored the
candidate, but the GUI process disappeared during the following B run. That
run/unfinished series is excluded. Closure cause is not established here.

A fresh saved-CMOS snapshot was taken after closure. One discarded warmup,
then ABBA BAAB, no GUI/builds/tests concurrently, BIOS1.42,1MiB,12MHz nominal,
read-only DOS3.30a. Each run is20guest seconds; DOS window is seconds15..20.

| Run | Version | Total host s | DOS window host s |
|---|---|---:|---:|
| 1 | before | 11.475 | 3.978253 |
| 2 | rearm reuse | 11.116 | 3.813745 |
| 3 | rearm reuse | 10.849 | 3.787916 |
| 4 | before | 11.506 | 4.001925 |
| 5 | rearm reuse | 10.953 | 3.849142 |
| 6 | before | 11.580 | 4.060155 |
| 7 | before | 11.597 | 3.996149 |
| 8 | rearm reuse | 11.179 | 3.884935 |

Mean total11.539500->11.024250s (-4.46%); mean DOS4.0091205->3.8339345s
(-4.37%). DOS ranges do not overlap in this sample. All20 per-second guest
checkpoints (CS:IP,steps,halted,IO,memory) match across all runs;25,121,946
boundaries and framebuffer digestfaaeacd09fb98a17. Keep the candidate on this
evidence. This headless result does not prove the cause of the live GUI's
145% observation, nor guarantee a particular displayed percentage.
No implementation changes or guest timing changes during this comparison.

### Shared AT adapter redundant rearm (2026-09-27)

Private board benchmark can now attribute inclusive synchronization time to
PIT/RTC/keyboard. Warmup18M, total26M boundaries: baseline sync1.370/0.872/1.059s
respectively. Timers substantially perturb execution; these are not additive
to the encompassing CPU/peripheral totals. A later instrumented run overlapped
the test suite and is unsuitable for a performance comparison.

Shared AT clock link now remembers its last successfully armed absolute native
cycle deadline or disarmed state. It still advances and queries the device;
only an identical scheduler rearm/disarm is avoided. Callback consumption and
explicit epoch reset invalidate it. Zero delay, failed scheduling and an
unrepresentable cycle sum retain existing validation/error paths. No batching,
guest cycle changes, skipped IRQs or platform threads introduced.

Uninstrumented DOS20s A/B/B/A: baseline11.901/11.890s, second candidate11.207s.
The first candidate's summary was truncated from the diagnostic output, so
no two-candidate mean is claimed. Observed reduction about6%, to revalidate in
GUI. Same25,121,730 boundaries,52,966,435 memory calls,2,457,690 IO and framebuffer
digestfaaeacd09fb98a17 in retained summaries. BIOS1.42,1MiB,DOS3.30aRO,saved CMOS;
GUI remained concurrent. Existing clock/board tests pass. New six-mode PIT
comparison checks exact state/edges after frequent fractional sync versus
scheduler-only advancement, gates and epoch reset. Initial new test wrongly
read control port43h; corrected to counter40h, without changing device behavior.
Engine/component suite:152 pass/one known skip/one fixture failure initially;
the corrected failed test passes on rerun (153 passed in aggregate).
Pending GUI rebuild/restart and interactive evaluation.

### Headland follow-up and byte backing experiment (2026-09-27)

User reports unlimited GUI speed rising to150% with the window index.
This is observed interactive speed, not additional hardware-fidelity evidence.
Reviewed route planning, wait conversion and RAM/ROM backing. A single-byte
backing specialization, after all validation and permission checks, was tested
and rejected: A/B/B/A20guest seconds baseline13.948/13.631s versus candidate
13.632/14.763s. Concurrent GUI/host variation is substantial; no repeatable gain.
All four runs:25,121,730 boundaries,52,966,435 memory calls,2,457,690 IO,
digestfaaeacd09fb98a17 and identical sampled CS:IP. The saved CMOS snapshot
differs from earlier sessions; compare within this set, not across sessions.
Source experiment reverted completely and benchmark rebuilt successfully.
GUI16456 remains running unchanged. Next measured candidate: peripheral
synchronization, previously~1.94s versus~1.15s inclusive memory in the board
profile. Do not remove validation, fragment preflight or dynamic wait accounting
to gain speed. No claim that Headland emulation or optimization is complete.

### Legacy Headland window index (2026-09-27)

The legacy mapper scanned up to93 windows for each internal memory route.
It now derives a1024-byte page-to-window index at initialization and on every
EMS-slot/all-map effect. Later enabled windows still win; no byte data or host
pointers are cached. CPU A20, address translation, permissions and backing
bounds remain live. The configured-geometry profile is unchanged.

POST/DOS20s A/B/B/A, BIOS1.42,1MiB,read-only DOS3.30a,same saved CMOS:
baseline13.426/13.329s; indexed11.630/11.944s (~12% lower total mean).
DOS seconds15..20 cost roughly0.88–0.98s before and0.66–0.73s after.
All four runs:25,121,882 boundaries,52,966,612 memory calls,2,457,657 IO,
digestfaaeacd09fb98a17 and matching sampled CS:IP. GUI concurrent throughout;
these are not isolated hardware benchmarks or a promise of a GUI percentage.

New diagnostic command, outside the product runtime:

```text
pcs286-runtime-benchmark BIOS BOUNDARIES --dos-board-profile FLOPPY CMOS_HEX WARMUP_BOUNDARIES
```

26M boundaries,18M warmup starts at14.617717666 guest seconds, BIOS keyboard
service F000:CA45. Remaining8M boundaries represent6.045910334 guest seconds,
23,265,655 memory calls and112 IO. Inclusive host timers perturb the sample:
CPU+sync4.791->3.299s; nested memory2.632->1.148s; peripherals1.886->1.943s;
DMA~0.297s; video~0.280s. Do not add inclusive memory to CPU+sync.
The reduced memory duration explains the CPU+sync reduction; next likely
target is peripheral synchronization, without skipping device deadlines.

153 engine/component tests pass; one known public Headland acceptance skip.
New independent old-priority scan checks all1024 pages after each mutation
in the existing route matrix; alignment and bounds are also asserted.
Existing EMS,shadow,A20,backing,configured-geometry tests pass. Provenance
audit has zero errors, with original port notices retained. GUI not restarted.

### AT transfer copy experiment — rejected (2026-09-27)

User observed DOS unlimited speed rising from 100% to 110% after the wait
conversion cache. This is an interactive observation, not a hardware timing
validation. A follow-up experiment removed the redundant private transfer
copy from the CPU bus entry point while retaining checks and publish-on-success.
POST/DOS 20-second A/B/B/A runs: baseline 15.291/15.869 s; candidate
15.206/16.170 s. All runs produced 25,121,882 boundaries, 52,966,612 memory
calls, 2,457,657 IO calls and framebuffer digest faaeacd09fb98a17.
Inputs: BIOS 1.42, 1 MiB, read-only DOS 3.30a and one snapshot of saved CMOS.
GUI remained running throughout, so host contention is a confounder.
There is no repeatable gain; the source experiment was fully reverted and
the benchmark rebuilt. No guest timing changes and no GUI restart.
Next: attribute CPU instruction/memory dispatch overhead with a less noisy
measurement environment before considering further changes.

### Headland last-conversion reuse (2026-09-27)

Each memory owner now retains the last successful pure wait conversion,
keyed by both full clock rates and accumulated cost. It never caches data,
routes, accesses or errors. Configuration/rate/cost changes miss the cache;
initialization clears it. Additional endpoint waits are still accumulated.

DOS run with saved CMOS, BIOS1.42,1MiB and read-only DOS3.30a,20guest seconds,
A/B/B/A: before17.649/17.715 s, after15.802/15.483 s. All four have
25,122,003 boundaries and framebuffer digestfaaeacd09fb98a17; sampled CS:IP,
IO/memory counts match. Approximate11–12% lower whole-run time under concurrent
GUI load; individual DOS windows fluctuate, not a universal percentage claim.
153 Release engine/component tests pass/one known Headland skip.
1800 new bounded conversions exercise rational rates/costs and cache hits
against a ceil-ratio oracle; each hit still invokes the backing. Invalidated
clock rejected before backing. Existing failure/overflow/multi-fragment tests
remain green. Live GUI not restarted during measurement.

### DOS idle versus diagnostic CMOS (2026-09-27)

Benchmark now accepts --dos-profile FLOPPY [OUTPUT.ppm [CMOS_HEX]], samples
CS:IP, halted, cumulative IO/memory and boundaries once per guest second.
It reads a 1.44 MiB image into a read-only callback; no writable media backend.
Optional CMOS is exactly128 bytes as256 hex digits, external and never bundled.

Important correction: diagnostic CMOS reaches built-in Setup, NOT DOS.
100 guest seconds there took99.649 s and showed video BIOS code around
F000:96xx. A screenshot at20 seconds verified Setup. Do not use that run to
explain DOS idle performance.

With a read-only copy of the user's saved CMOS, BIOS1.42 and DOS3.30a, the
30-second screenshot verifies A>. Samples from15..30 guest seconds show
approximately1,323,210 boundaries and3,848,180 memory calls per guest second,
only18..20 IO calls, halted=0 at every sample. CS:IP visits DOS segments
029D/0070 and BIOS F000:CAxx/E833. Inspection of local firmware CA1B/CA33
shows keyboard-buffer head/tail checks and keyboard-service dispatch.
Inference: active console/keyboard polling, not sustained HLT sleep.
Snapshots alone do not prove HLT never occurs between samples.

No Qt rendering or pacing is present in this run. DOS seconds cost
1.36..1.77 host seconds under concurrent GUI/host load; this is NOT a directly
comparable speed percentage to the GUI's live figure. Its whole30-second run
took40.058 s. Final digest b8bbcd094aec1157.
Three targeted runtime/board-services/timed-source tests pass.
No optimization or guest-timing change in this tranche. Next: attribute
CPU execution/stack/interrupt paths versus memory-routing cost for this loop,
not fake an idle skip or change cycles to inflate the speed indicator.

### Exact-time cycle-cursor reuse (2026-09-27)

Repeated timed-source cursor queries now reuse a per-source result only for
identical nanoseconds, phase and denominator. Rates remain immutable; arming
and disarming do not change the answer. Reset invalidates the cache. Device
advance, IRQs and deadline updates still execute normally; no batching is added.
Three guest seconds A/B/B/A: before1.715/1.748 s, after1.642/1.648 s;
3,751,163 steps and digest87dfbd75f6e852cd identical. Approximate5% gain in this
sample, with GUI concurrent, not a universal benchmark. 153 selected Release
tests pass/1 known Headland skip. Added repeated fractional callback queries
and100 arm/disarm/query iterations; reset and CPU-dispatch tests remain green.

### Whole-board profile and redundant waits (2026-09-27)

Ten guest seconds (BIOS1.42, 1 MiB, empty installed floppy): headless
8.995/9.010 s; 500 software renders 9.254/8.583 s, of which 0.379/0.356 s
inside rendering. Identical 12,183,758 boundaries and digest 90fc16a0f5dd12ed.
Concurrent GUI and host variance prevent a precise whole-render overhead claim.
This excludes Qt delivery/presentation and does not reproduce DOS with media.

New --board-profile executes the provisional board timeline without the outer
runtime scheduler, with optional inclusive host timers in memory/IO callbacks.
Twelve million steps before: CPU+sync 6.575 s, DMA0.482 s, peripherals3.409 s,
video advance0.425 s. Inclusive memory3.364 s (19,095,923 calls), IO0.330 s
(1,577,292 calls); nested/instrumented times must not be summed as exclusives.
Manual2,810,015/fallback9,189,985 steps; guest9.848815083 s.

Headland repeated wait conversion even when a device added zero waits.
Reuse the already validated conversion in that case, and short-circuit zero
cost after validating both clock rates. No waits or checks are removed.
After: CPU+sync4.830 s, memory1.761 s, peripherals3.345 s; same access/event
counts and guest time. Noninstrumented three-second A/B:
2.442/3.056 s before versus1.856/1.969 s after; identical 3,751,163 steps and
digest87dfbd75f6e852cd. Host variance limits precise percentage claims.
153 Release engine/component tests pass, one known public Headland skip;
rational waits, failures and overflow remain covered. GUI not yet restarted.

### 2026-09-27 provisional-clock follow-up

Release, BIOS1.42, 1 MiB, empty installed floppy, GUI concurrently open.
New --blocks-estimate mode keeps the same fixed peripheral timeline as --blocks
to isolate estimator overhead. One million boundaries: CPU+sync
0.4441/0.4466 s without estimates versus 0.4564/0.4516 s with them
(about 2%, noisy/instrumented). Normal runtime executes 1,478,188 boundaries
in the first guest second. Old boundary/1,000,000 guest-time reporting was
incorrect after the clock switch and is now fixed.

Clock comparison avoids division for equal denominators and zero fractions.
Three guest seconds before: 2.309/2.327/2.341 s; after: 2.218/2.201/2.203 s.
Each run: 3,751,163 boundaries, framebuffer digest 87dfbd75f6e852cd.
Approximate 5% reduction, not an isolated-host or universal speed claim.
153 selected Release engine/component tests pass; one known Headland skip.
A bounded exhaustive cross-product oracle covers comparison shortcuts.
Live GUI was not restarted and still uses the preceding binary.

Build `pcs286-runtime-benchmark` in Debug and Release with UCRT64 GCC 16.1.
Run `pcs286-runtime-benchmark <local-combined-BIOS> <boundary-count>`.
The tool uses the same runtime as Qt, in 5 ms guest chunks, without rendering,
pacing or floppy media. Startup is excluded from the printed `clock_s` interval.
Firmware remains external. These measurements used BIOS 1.42, SHA-256
`afbd051666869f3f58f23e52f9dd468fb9ad9f629d2dbccfda5324e83a897621`.
The original Debug GUI remained running: timings are approximate, not isolated
laboratory measurements. Windows C `clock()` readings are reported as measured
elapsed intervals, not an exclusive per-thread CPU profile.

| Boundaries from reset | Debug before | Release before | Release after |
|---|---:|---:|---:|
| 200,000 (0.2 provisional guest seconds) | 5.375–5.492 s | 1.845–1.847 s | 0.199–0.207 s |
| 1,000,000 (1 provisional guest second) | not measured | 13.228 s | 1.345–1.401 s |

Release alone improves the first interval about 2.9 times. The clock change
improves Release about 9 times in the short interval and 9.4–9.8 times in the
longer interval. This is not a guarantee of real-time performance or physical
12 MHz accuracy. The tool has no frame-rendering cost, and later BIOS workloads
can differ. Gprof produced no useful samples on this host; no percentage-of-time
claims are derived from that failed profiling attempt.

## Cause and change

`bm_clock_cycles_at_or_before` previously bracketed and binary-searched from
zero even for integral nanosecond timestamps. Peripheral synchronization calls
this frequently, and the search grows with the absolute source cycle index.

For an integral timestamp, compute the exact floor of time divided by the
source period with the existing overflow-safe integer multiply/divide helper.
Keep the old search for fractional timestamps and preserve saturation at the
maximum representable cycle index. No frequencies, device events, instruction
boundaries or guest timing constants change.

Tests verify the selected edge is at/before the target and its successor is
after it (or unrepresentable), covering 1,090 combinations of rates/timestamps,
fractional periods, sub-nanosecond periods, random full-range timestamps,
overflow and saturation. Existing fractional-cursor tests remain in place.
Release full suite: 168 passed, existing public Headland test skipped (169 total).

The running GUI is still the old Debug process; building new binaries does not
change its performance. Restart in the Release executable to test the change
interactively. Avoid concurrent sessions sharing the same persisted CMOS key.

## Second measurement round

The benchmark now accepts `--blocks` to attribute wall time within the shared
board: CPU plus its synchronization, DMA, peripheral advancement, and video clock.
This diagnostic mode excludes the outer runtime scheduler and rendering. Timer
calls perturb results; it is not an uninstrumented CPU profiler. Two 1-million
boundary runs measured CPU/sync 0.700–0.749 s, DMA 0.039–0.042 s, peripherals
0.666–0.719 s and video clock 0.035–0.036 s. Do not equate video clock with rendering.

An AT clock link now returns immediately from pure `sync` when its source cycle
cursor equals its last-serviced cursor. It still checks retained errors, backward
time and reentry. I/O, external-input changes, apply and reset paths still rearm,
even at the same timestamp. This relies on the existing ownership contract:
attached devices must not be advanced independently and mutations must notify
their clock link. It does not suppress scheduled device edges.

Alternating A/B runs (Release, same BIOS/config, no render or media):

| Run | Before second optimization | After |
|---|---:|---:|
| 1 | 1.344 s | 1.078 s |
| 2 | 1.333 s | 1.134 s |
| 3 | 1.652 s | 1.200 s |

Median elapsed reduction is about 16%, with visible host variability. A longer
optimized run completes 5 provisional guest seconds in 5.733 s. No real-time GUI
or exact physical 286 speed claim follows from these headless measurements.
The Release suite again passes 168 tests with the same public Headland skip.
New PIT regression compares 200 successive boundaries with 100 redundant syncs
each against one sync, including same-time external gate changes, asserting
identical state and timestamped output edges. Existing RTC/keyboard/failure/
reentry/reset tests also pass. GUI benchmark and longer boot remain next steps.

## Third measurement round: rendering and cursor reuse

The user reports the updated Release GUI now runs nearly in real time. This is
interactive observation, not a physical 12 MHz calibration or completed boot test.

`--render` renders 50 frames per provisional guest second in the runtime benchmark.
It measures core framebuffer generation only, not Qt scaling/presentation. Both
normal and render modes now produce a final frame and an endian-independent
pixel-word digest for A/B comparison. This digest is not a firmware hash or proof
that all intermediate device states match. Frame allocation sizes are checked.

`bm_at_clock_link_sync` was reading the cycle cursor once for its no-elapsed test
and then again in `advance`. It now passes the already validated cursor into a
private helper. There is no callback or mutation between reading and using it;
other paths still query as before. Retained errors and reentry checks remain.

Three alternating 1-million-boundary Release comparisons with rendering:
1.021/0.965 s, 1.012/0.976 s, 1.030/0.989 s (before/after). Median reduction 4.4%.
Final digest agrees: `d85ec0d1c3f83b65`. This early interval is mostly blank and
does not represent a busy display workload.

A 10-million-boundary comparison with 500 rendered frames measured 11.274 s
before and 10.573 s after (6.2% reduction); final pixel digest agrees:
`9c800b50908be715`. Render-only intervals: 0.319/0.310 s, roughly 3% of total.
An additional optimized run measured 10.377 s. Host scheduling and the open GUI
remain sources of noise; do not promise the same speedup on other machines.

Release suite: 168 passed, the same public Headland skip. Tests were relinked
without replacing the running Qt executable. No change to clock frequencies,
instruction count, render output policy or guest-visible device semantics.
The current GUI process does not yet include this third small optimization.

## Fourth round: Qt presentation, not guest clock acceleration

User authorized restarting the GUI for measurement. The old per-event `qInfo`
trace strongly perturbed Windows execution; its measurements were discarded.
Opt-in latency tracing now stores at most 65,536 records in memory and emits
them at process exit, with no guest keys, contents or asset paths. It remains
instrumentation with some overhead, not a zero-cost profiler.

Comparable wall-time windows 5–25 seconds after startup, BIOS1.42,1MiB and
read-only preserved DOS3.30a floppy, current persisted CMOS:

| Metric | Before GUI changes | After |
|---|---:|---:|
| Frames ready / UI deliveries per second | 50.29 | 62.51 |
| Paint submissions per second | 50.29 | 62.51 |
| Median frame preparation | 1214.5 us | 996 us |
| Guest seconds / wall second | 0.632 | 0.678 |
| Median emulation chunk | 5932 us | 5656 us |

These are two live runs, not identical instruction windows; persisted CMOS,
host load and guest phase can differ. FPS is producer/UI/submission cadence,
not measured monitor scanout. More FPS does not establish accurate CPU speed.
In particular this run still falls short of real-time guest advancement despite
the user's earlier near-real-time impression and the headless results.

Changes: keep a phase-aligned 16ms presentation deadline, skip missed deadlines
without catch-up bursts, sample host time after emulation; render directly into
an owned QImage instead of vector + full image copy. Each snapshot retains its
own immutable image; no reused writable UI buffer or new emulation thread.
The guest tick pacer, clock constants and device event order are unchanged.
Cadence tests cover overshoot, missed intervals, reset and repeated 6ms polling.

Existing design already separates the Qt UI and emulation worker. Parallelizing
guest CPU/device execution is not justified by these measurements. Continue
profiling guest CPU/peripheral work before adding cross-thread synchronization.

## Fifth round: reuse initialized clock periods

Timed source cycle-count queries were reducing the same source rate to a period
on every call. They now use the immutable period fields already initialized in
the source deadline. Only the period is used, not its future timestamp/phase.
Reset and rearm retain/reinitialize those fields as before. This adds an internal
clock-math entry point, not a public ABI change or mutable global cache.
Fractional target search also reuses the period across its candidate positions.
All calculations remain overflow-checked integers, including saturation.

Three alternating Release A/B runs, 1 million boundaries plus 50 frames:
0.994/0.712 s, 0.972/0.708 s, 0.969/0.745 s. Median reduction approximately 27%.
Final pixel digest remains `d85ec0d1c3f83b65`. At 10 million boundaries and
500 frames: 11.438/7.485 s (34.6% elapsed reduction), same final pixel digest
`9c800b50908be715`. This is headless core rendering with empty floppy; the GUI
session with read-only DOS media was left running. Not a GUI speed guarantee.

Instrumented block attribution (1 million boundaries), before/after:
CPU+sync 0.516/0.460 s, DMA 0.036/0.037 s, peripherals 0.452/0.278 s,
video clock 0.034/0.035 s. Timer/host noise applies; this identifies a useful
peripheral synchronization improvement, not exclusive CPU instruction costs.

Extended exact-floor tests include fractional targets and a cached period stored
in an arbitrarily advanced deadline; the cached and rate-based APIs agree, leave
input unchanged, and selected/successor edges bracket the target. Invalid cached
periods reject without modifying the output. Existing scheduling/event/reset
tests validate integration. Running GUI is deliberately not replaced in this round.

## Sixth round and interactive DOS boot evidence

The GUI was rebuilt with round-five clock reuse and launched with the preserved
DOS3.30a floppy read-only. User screenshot and report confirm the portable GUI
reaches MS-DOS3.30a Rev1.03 and the A> prompt. Visible BIOS diagnostics report Pass,
one floppy present, no fixed disk, and the UI shows A:RO with605reads/0writes.
The user reports it is functional. This is observed DOS prompt evidence, not
exhaustive OS, storage-write or physical timing validation. The separate benchmark
still has an empty floppy: this distinction must not be applied to the GUI.
Screenshot retained locally in the canonical library, not in this repository.

Round-five benchmark:30provisional guest seconds/1500frames in29.857wall seconds.
Round-six experiment extends cached-period reuse to rearming the next source
edge. Short A/B pairs:0.755/0.694,0.751/0.704,0.746/0.695s (median7.5% reduction).
Thirty-second after run:26.528s, versus29.857s before (11.2% reduction); same final
pixel digest b2a168cb2cd618cf. These empty-floppy headless results do not certify
DOS workload speed. Cached-next-edge tests agree with the original rate-based
path across integer/fractional/extreme targets and error cases. Debug/Release:
168pass, same1publicHeadlandskip. The live GUI remains on round five so the user's
successful DOS session is not interrupted by this measurement.

## Rejected integral-period shortcut, 2026-09-26

With the user's GUI left running, 3 million boundaries attributed 1.173s to
CPU/synchronization, 0.825s to peripherals, 0.107s to DMA and 0.103s to video
clock advancement. Instrumentation overhead means these are diagnostic only.
An exact integral-period inverse shortcut was trialled, then removed: 3s guest
rendered runs were 2.161/2.657/2.625s versus baseline 2.506/2.208/2.874s; no
reliable gain under concurrent GUI load. Final digests agreed (a27114736f18fdf5).
Retained only additional fractional-target/extreme integral-period regression
tests; Debug clock-math passes. No live GUI replacement. Next measurements
should pause the interactive machine and compare interleaved runs, before any
claim of improvement. All these benchmarks have an empty floppy, not DOS media.

## Isolated baseline and CPU accounting, 2026-09-26

After the user closed the GUI, three Release runs of 10 million boundaries
with 500 rendered frames took 6.880/6.884/6.807s; final image digest remained
9c800b50908be715. This is about 145% of the provisional virtual timeline,
not a DOS workload benchmark or a physical 12MHz PCS286 speed claim.

The diagnostic --blocks mode now classifies returned functional boundaries
without changing board execution. Ten million steps: 6,016,341 instruction
boundaries, 3,346,824 REP iterations, 2 interrupts, 3 exceptions, 7 reset
events, and 636,823 steps without a completed CPU boundary. All 9,363,170
completed CPU boundaries reported UNKNOWN timing. No instruction count can
be inferred by simply adding the REP iterations. IDLE results are not read.

Instrumented attribution: CPU plus synchronization 4.573s, DMA .424s,
peripherals 3.733s, video clock .359s. These instrumented times include timer
overhead and must not be compared as throughput against the uninstrumented
6.8s runs. They cannot isolate interpreter cost from synchronization cost.

M15 registers bm_808x_step_clocked_provisional at 14318180/3 Hz; PCS86 uses
bm_808x_step_clocked_nec_ranges_provisional at 10MHz. Both report per-boundary
cycle counts with provisional qualifications. PCS286 instead registers a
1MHz timed callback: functional CPU step, DMA service, advance the separate
peripheral engine by 1000ns, advance video by 1000ns. Its strict CPU callback
still refuses execution because instruction timing is UNKNOWN. Therefore
600/800/100% GUI observations are not comparable CPU-efficiency benchmarks.

Next order: instrument repeated synchronization separately; optimize only
provably redundant work with event-order regression tests; establish sourced
286 per-boundary timing and tests before registering it as a clocked CPU.
Do not replace the diagnostic 1000ns with an arbitrary speed multiplier, skip
REP interrupt boundaries, or batch peripherals across observable I/O/events.
