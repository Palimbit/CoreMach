# Virtual time across CPU architectures

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: the clocked scheduler and timed-device sources are active in the
portable PCS 86. Its V30 runs at 10 MHz and reports one exact native-cycle
duration for every accepted instruction boundary; PIT and RTC remain separate
exact-rate participants. Timing-observation version 38 routes instruction
prefetch and operand or I/O transfers through the instance-owned BCU, places
their inherited EXU order, preserves wait states and rejects unresolved ranges
instead of choosing a convenient value. The local firmware probe completed a
20-second BIOS-and-floppy run with 16,503,008 exact boundaries and no unknown
boundary. This proves the exercised path, not complete cycle accuracy: an
instruction is still the scheduler's indivisible unit, interrupted string
fragments, busy `POLL`, synchronous faults and single-step paths remain
explicitly unresolved, and device models retain the limitations documented
below. The probe reports exact/ranged/unknown totals and the first unsupported
boundary without making timing a frontend or machine-policy dependency.

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
  as well. Near and far indirect jumps now place their operand reads before
  prefetch suspension and queue invalidation. Every decoded
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
- The current PCS 86 local firmware baseline mounts the preserved System Disk
  read-only and completes 20 seconds of strict virtual time: 16,503,008
  instruction boundaries, 11,977 I/O operations, no ranged or unknown
  boundary, and framebuffer CRC `06bd8a15`. The last boundary is the BIOS idle
  loop at `F000:E82E`; the disk image retains SHA-256
  `75E1A068AA5910DB736CE4B53E6B5FC179F83390FF5512D2AF421E57AF3A0C12`.
  This baseline was reached incrementally through real encountered boundaries,
  ending with indirect far jump (`FF /5`) and direct far call (`9A`). It does
  not imply that every architecturally possible V30 path is timed.
- The V30 now exposes a clocked-engine step callback independently of its
  optional diagnostic observer. It reports a duration only for a complete,
  exact scalar boundary; a ranged or unknown result stops with
  `BM_STATUS_UNSUPPORTED` and zero cycles. This is an intentional migration
  guard. The validated 20-second firmware-and-floppy path is fully scalar, but
  unexercised paths can still stop at the first unresolved boundary. The PCS 86
  now uses nanosecond virtual time with its
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
  MM58167 microsecond adapter. Video remains render-on-demand from virtual time.
  XTA DMA service has its own demand-driven timed source: it is disarmed while
  idle and retries only while a transfer is pending, at the existing declared
  functional interval of 32 microseconds. That interval is not presented as a
  physical model of controller or drive latency.

This path provides CPU-type independence without prematurely promising that
all guest CPU models or their buses have the same timing fidelity.
