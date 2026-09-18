# Virtual time across CPU architectures

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: design and clock-arithmetic proof, **not an active scheduler contract**.
The existing PCS 86 engine still advances one scheduler tick per completed V30
instruction boundary. Its `scheduler_ticks_per_second` is pacing metadata, not
the V30 crystal frequency. No current machine becomes cycle accurate because
of the clock helper added with this document.

## Ownership

An emulated machine owns one virtual timeline. Every CPU, video controller,
timer and other timed component attaches a frequency or a clock relationship
to that timeline. CPU implementations report native cycles consumed; the
scheduler, not the CPU and not the host frontend, orders participants and
events on the common timeline. The runtime may pace that timeline against the
host monotonic clock, but host time must not decide guest-visible event order.

The clock-arithmetic proof in `engine/src/clock_math.c` represents each clock
position as whole virtual nanoseconds plus an exact fractional-nanosecond
remainder. Its comparison never cross-multiplies frequencies, and repeated
small advances have the same position as an equivalent bulk advance. It has
no public API or scheduler effect yet. One advance is bounded by the 64-bit
intermediate product; overflow is reported without changing the clock.

## Proposed scheduler contract

1. Keep the existing instruction-tick engine path intact while the new path is
   developed and tested. Do not relabel its ticks as cycles.
2. Define an explicitly clocked execution operation separate from the existing
   `run(context, budget, consumed)` operation. A clocked CPU reports native
   cycles for a completed execution boundary. Its frequency belongs to the
   machine wiring, not an opcode implementation or frontend setting.
3. Select the participant at the earliest virtual position. Equal-time
   participants have a stable registration order; same-time events have their
   existing insertion order. A halted participant is suspended until a signal
   or event wakes it; it must not spin or advance the host clock.
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
   event is serviced at the next boundary and its lateness is observable. No
   frontend or machine may describe this level as bus-cycle accuracy.
   A later bus-phase/micro-operation contract is required where exact ordering
   within an instruction materially affects a machine.

## Required proof before a real CPU migrates

- Two synthetic CPUs with different frequencies and an event source use one
  timeline. Repeated runs, different host speeds and split versus combined
  run requests produce the same guest-visible trace.
- Clock conversion has no accumulated rounding drift, handles equal-time
  ties and rejects overflow atomically.
- Tests cover an event between two instruction boundaries, recording the
  deterministic delay rather than asserting false cycle accuracy.
- HALT/idle, wake-up, reset, interrupt delivery, bus waits and failure paths
  have explicit tests. No participant starves under a sustained mixed load.
- The existing PCS 86 suite remains unchanged and green. V30 migration is a
  later change, with independently measured instruction-cycle coverage and
  known unknowns; Z80 integration must not alter the legacy V30 tick meaning.

This path provides CPU-type independence without prematurely promising that
all guest CPU models or their buses have the same timing fidelity.
