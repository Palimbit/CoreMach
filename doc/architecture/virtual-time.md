# Virtual time across CPU architectures

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: experimental clocked scheduler with synthetic tests; no real CPU or
machine uses it yet. The existing PCS 86 engine still advances one scheduler
tick per completed V30 instruction boundary. Its
`scheduler_ticks_per_second` is pacing metadata, not the V30 crystal frequency.
No current machine becomes cycle accurate because of this change.

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
   `bm_cpu_ops_t` layout is unchanged.
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
- The existing PCS 86 suite remains unchanged and green. V30 migration is a
  later change, with independently measured instruction-cycle coverage and
  known unknowns; Z80 integration must not alter the legacy V30 tick meaning.

This path provides CPU-type independence without prematurely promising that
all guest CPU models or their buses have the same timing fidelity.
