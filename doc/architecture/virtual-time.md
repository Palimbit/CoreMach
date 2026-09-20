# Virtual time across CPU architectures

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: experimental clocked scheduler and timed-device sources with synthetic
tests; no real CPU or machine uses them yet. The V30 timing observer can now
compose a complete native-clock duration for instruction boundaries without
operand or I/O traffic, while preserving ranges and unresolved boundaries as
such. Version 25 also routes memory and I/O transfers through the same BCU as
prefetch, exposes the clocks needed to hand over an in-flight prefetch, and
advances the BCU concurrently for the documented clock of every byte consumed
from the instruction queue. Every native prefix advances that timeline at its
decode point. The eight native `IN`/`OUT` opcodes therefore have exact complete
boundaries with or without accepted prefixes, including I/O wait states. The
four direct accumulator-memory `MOV` forms, `XLAT`, and memory forms of ModR/M
`MOV` plus all ModR/M ALU forms are also placed, including both transfers of
read-modify-write operations and odd words. ModR/M `TEST` and `XCHG` are placed
as well. Immediate ALU groups `80h`-`83h` now read memory before consuming the
immediate and place their optional write. Segment-register `MOV` memory forms
and immediate-to-r/m `MOV` groups `C6h`-`C7h` are placed too. Group 3
`F6h`-`F7h` memory operands are placed as well. Unsigned `MULU` resolves its
documented one-clock data-dependent interval with the inherited V30
microcode's high-half condition; signed multiply and signed-divide ranges
remain incomplete rather than selecting a value from the range. Byte and word
memory `INC`/`DEC` forms in groups `FEh`
and `FFh` are placed too. Single-word register, segment, flags and immediate
stack pushes and pops are placed as well. Other operand-bearing boundaries stay
unresolved until their EXU position is known. `STOS` and `SCAS` now place their
normal, repeated, zero-count and odd-word transfers too; interrupted repeat
fragments remain deliberately unknown. Relative near `CALL` and both near
`RET` forms now place their stack transfer around an explicit prefetch
suspension and target-queue flush.
`MOVS` and `LODS` now place their source-side transfers as well, including
repeated, zero-count, segment-overridden and odd-word forms.
Accumulator-immediate `TEST` is now classified at its documented four clocks;
it has no operand-bus transfer to place.
`LES` and `LDS` now place both far-pointer reads after their inherited setup,
including odd pointers and accepted segment overrides.
It is not
yet registered as a clocked CPU. The existing PCS 86 engine
still advances one scheduler tick per completed V30 instruction boundary. Its
`scheduler_ticks_per_second` is pacing metadata, not the V30 crystal frequency.
No current machine becomes cycle accurate because of this change.
The PCS 86 configuration can forward the versioned V30 timing observer to
diagnostic consumers. The firmware probe reports complete/exact/ranged/unknown
boundary totals, unknown counts per effective opcode and the first unknown
boundary's execution, transaction and flush context without making timing a
frontend or machine-policy dependency.

## Ownership

An emulated machine owns one virtual timeline. Every CPU, video controller,
timer and other timed component attaches a frequency or a clock relationship
to that timeline. CPU implementations report native cycles consumed; the
scheduler, not the CPU and not the host frontend, orders participants and
events on the common timeline. The runtime may pace that timeline against the
host monotonic clock, but host time must not decide guest-visible event order.

The clock arithmetic in `engine/src/clock_math.c` represents each clock
position as whole virtual nanoseconds plus an exact fractional-nanosecond
remainder. Public clock rates are rational cycles per second, so a crystal
divider such as `14318180/3` is not rounded to an integer frequency. Its
comparison never cross-multiplies clock denominators, and repeated small
advances have the same position as an equivalent bulk advance. The position
representation has no public API. One advance is bounded by the 64-bit
intermediate product; overflow is reported without changing the position.

## Scheduler contract and limits

1. `bm_engine_create()` retains the existing instruction-tick path. The new
   `bm_engine_create_clocked()` opts in to virtual-nanosecond event time and
   separate CPU frequencies. These modes cannot mix CPU registrations. Do not
   relabel the existing V30 ticks as cycles.
2. `bm_engine_add_clocked_cpu()` supplies a cycle-returning step operation
   separate from the existing `run(context, budget, consumed)` operation. A
   clocked CPU reports native cycles for a completed execution boundary. Its
   rational clock rate belongs to machine wiring, not an opcode implementation
   or frontend setting. The callback receives the integer virtual time at the
   start of that boundary, not a timestamp for each bus access. The old
   `bm_cpu_ops_t` layout is unchanged. Like timed sources, clocked CPUs are
   registered while the engine is at virtual time zero; hot-plugging a running
   clock domain is deliberately outside this first contract.
3. The engine selects the participant at the earliest virtual position. Equal-time
   participants have a stable registration order; same-time events have their
   existing insertion order. A halted participant is suspended until a signal
   wakes it; it must not spin or advance the host clock.
4. A bus transaction already carries `wait_states`. The CPU adds applicable
   waits to its native-cycle result. Do not add a second CPU-level wait callback
   or silently claim that merely counting waits places individual bus accesses
   at their electrical time.
5. Interrupt controllers assert lines and supply acknowledge data through
   machine/CPU wiring. The CPU interprets that data according to its ISA.
   Z80 IM 0, IM 1 and IM 2 must not be collapsed into an IM 2-only callback
   on the generic engine's CPU operations.
6. The first clocked execution level is deterministic **instruction-boundary**
   scheduling. An indivisible instruction may cross a timer deadline. Such an
   event is processed after the instruction, but the nominal event timestamp
   remains unchanged and instruction side effects can therefore be visible
   before the event. Synthetic tests make this limitation explicit. No
   frontend or machine may describe this level as bus-cycle accuracy.
   A later bus-phase/micro-operation contract is required where exact ordering
   within an instruction materially affects a machine.
7. `bm_engine_add_timed_source()` represents a device deadline generator, not
   the device itself. Its rational rate and first delay are machine wiring. A
   zero first delay registers it initially disarmed, which is required for
   counters and transfer engines that do not run until guest software programs
   them. Reset restores the registered initial state rather than retaining a
   runtime arm or disarm decision.
   At each exact deadline the callback returns the positive native-cycle delay
   to its next activation, or `BM_STATUS_IDLE` with zero cycles to disarm until
   explicitly armed or reset. The engine does not own or reset the callback
   context.
   Registration is limited to the construction/reset boundary at virtual time
   zero in this initial contract, and `max_timed_sources` is an explicit engine
   capacity independent from queued one-shot events.
8. `bm_engine_arm_timed_source()` atomically installs or replaces a deadline;
   `bm_engine_disarm_timed_source()` cancels one. The positive delay counts
   source-clock edges strictly after the effective boundary, on the source's
   phase grid anchored at machine time zero. Calls outside CPU execution use
   the current exact engine boundary. Calls made from a clocked CPU step use
   that CPU's exact instruction-start position, including its fractional
   phase, even though the callback's legacy `start_ns` argument exposes only
   the integer floor. This makes guest programming deterministic but still
   does not locate the I/O write within the instruction. A source cannot arm
   or disarm itself from its firing callback; it uses the callback result for
   self-scheduling. Failed reprogramming, including arithmetic overflow,
   leaves the prior deadline unchanged.
9. Timed-source deadlines retain their exact sub-nanosecond phase.
   `bm_engine_now()` remains the floor in virtual nanoseconds for compatibility;
   `bm_engine_now_exact()` exposes the normalized fractional part. At a shared
   exact boundary, queued one-shot events run in insertion order, timed sources
   run in registration order, and one-shot events added by those sources are
   drained before CPU execution resumes.
10. A fractional deadline waking a halted CPU is rounded forward to the next
   integer nanosecond because the current CPU boundary API does not represent
   a cross-domain rebase. This is deterministic and prevents time travel, but
   is another explicit instruction-boundary approximation rather than a claim
   of edge-accurate interrupt sampling.

## Required proof before a real CPU migrates

- Two synthetic CPUs with different frequencies and an event source use one
  timeline. The current test proves one split-versus-combined run trace;
  broader interleaving and external-input tests remain required.
- Clock conversion has no accumulated rounding drift, handles equal-time
  ties, represents a divided-crystal rate without integer-Hz rounding and
  rejects overflow atomically.
- Tests cover an event between two instruction boundaries, recording the
  deterministic delay rather than asserting false cycle accuracy.
- HALT/idle, wake-up, reset and invalid-progress paths have initial tests.
  Events created during a CPU step are recognized before the next step.
  A 1:1000 synthetic rate test guards against starvation. Synthetic tests also
  cover a bus transaction adding two wait cycles and an interrupt line asserted
  between instructions. They do not establish a real Z80/808x acknowledge
  protocol or cycle-level placement of the bus access. Sustained mixed
  workloads with guest-visible transactions still need testing before a real
  CPU migrates.
- The existing PCS 86 suite remains green. V30 observation version 30 advances
  prefetch during each instruction-queue read and composes complete
  native-clock boundaries only where prefetch,
  queue-read and EXU placement is proven. Unprefixed direct and DX-addressed
  `IN`/`OUT`, direct accumulator-memory `MOV`, `XLAT`, and memory forms of
  ModR/M `MOV` plus all ModR/M ALU forms now place their inherited internal
  waits and operand cycles on that timeline, including the computation interval
  between a memory read and write. ModR/M `TEST` and `XCHG` are placed too, and
  immediate ALU groups preserve operand-before-immediate ordering. Segment-register
  and immediate-to-r/m `MOV` memory forms now have placed transfers too. Group 3
  reads, `TEST` immediates and `NOT`/`NEG` writes are positioned without
  collapsing documented arithmetic ranges. `FEh`/`FFh` memory `INC`/`DEC`
  reads and writes and the common single-word `PUSH`/`POP` forms are positioned
  as well. Every decoded
  prefix
  advances it before the next queue read. Other operand offsets, realised
  values inside signed arithmetic ranges, a repeatedly busy `POLL`, and
  synchronous fault or single-step interrupt boundaries remain known unknowns.
  An immediately ready `POLL` is the documented seven-clock case. Accepted NMI and maskable INT
  boundaries now use the NEC V30's documented 38- and 49-clock aligned-stack
  costs, add the three actual odd-stack transfer splits, and retain any BCU
  prefetch handoff separately. Software `INT` and `IRET` now
  place their vector and stack transfers in inherited microcode order, and
  indirect near `CALL` places its independently aligned target read and stack
  write. The PCS 86 now consumes these durations at 10 MHz in the clocked
  engine. Z80 integration remains independent of the V30 clock domain.
- The current PCS 86 firmware baseline completes 1,385,861 observed instruction
  boundaries before its known unsupported endpoint. All 1,385,861 now have a
  complete exact boundary duration. Placing `STOSW`
  (`ABh`) and `SCASW` (`AFh`) removed 262,193 unknown boundaries without
  changing the endpoint or framebuffer CRC. Placing relative near `CALL` and
  near `RET` then removed another 1,457, and `MOVS`/`LODS` removed another 312.
  Classifying accumulator-immediate `TEST` removed another 28.
  Placing `LES`/`LDS` removed another 15, software `INT`/`IRET` removed 5, and
  indirect near `CALL` removed 3. The final two unsigned byte multiplies use
  the documented 27-or-28-clock interval plus the inherited V30 microcode's
  explicit extra-clock condition: both firmware instances multiply zero and
  therefore take 28 execution clocks. This derived condition remains marked
  for hardware confirmation; it is not presented as a statement found in the
  NEC manual. The initial clocked migration reached the same `F000:85F9`
  unsupported endpoint with the same instruction and I/O counts. Its
  deterministic framebuffer CRC was `cd3694c5` rather than the
  instruction-tick baseline `e8bbbd1f`, because cursor/blink rendering receives
  the new elapsed nanosecond timestamp; CRTC state and the endpoint were
  unchanged. Accepting one V30 word-I/O transaction across the indexed VGA
  `3D4h/3D5h` pair then removes that component-boundary limitation: the same
  BIOS proceeds through timer programming and IRQ0. With the preserved System
  Disk mounted read-only, it reaches 5.778816 seconds and identifies the next
  strict timing guard at `RETF imm16`, after activating FDC IRQ6. This measured
  distribution sets the next
  integration order instead of opcode-table convenience.
- The V30 now exposes a clocked-engine step callback independently of its
  optional diagnostic observer. It reports a duration only for a complete,
  exact scalar boundary; a ranged or unknown result stops with
  `BM_STATUS_UNSUPPORTED` and zero cycles. This is an intentional migration
  guard: the firmware path plus accepted NMI, maskable INT and immediately
  ready `POLL` are scalar up to the first unresolved far-return timeline. The
  PCS 86 now uses nanosecond virtual time with its
  V30, PIT and RTC registered as independent exact-rate participants;
  single-step, synchronous-fault and busy-`POLL` paths still stop explicitly if
  reached before their timing is completed.
- Synthetic timed-source tests cover an exact 3 Hz fractional sequence,
  split execution, reset rearming, stable same-time ordering, explicit idle,
  dynamic arm/reprogram/disarm, exact CPU-boundary arming, overflow atomicity,
  invalid progress and waking a halted CPU from a fractional deadline. A
  separate adapter now drives the real 8253 component from the exact
  `14.31818 MHz / 12` PC clock and verifies mode-2 output edges without making
  the chip own the scheduler. It deliberately fires once per input edge as a
  correctness baseline, with transition batching and lazy counter
  synchronization covered independently. The PCS 86 uses that adapter and the
  MM58167 microsecond adapter. Video remains render-on-demand from virtual time;
  XTA service retains an explicit functional 32-microsecond event pending a
  device-level deadline model.

This path provides CPU-type independence without prematurely promising that
all guest CPU models or their buses have the same timing fidelity.
