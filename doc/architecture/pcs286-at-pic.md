# Cascaded AT PIC: implemented boundary and next integration gate

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`blumach_at_pic` is a separate, instance-owned pair. The existing XT PIC and
PCS86/M15 callers are untouched. This is a derived rewrite of the portable
single-PIC implementation at `ab5e8cbc527d615464bee0448a948846e8f4d1b0`;
Andrew Jenner and Miran Grca's notices are retained. No firmware is included.

## Reference and implemented subset

Reference: Intel **8259A**, order **231468-003**, December 1988,
[manufacturer datasheet mirror](https://www.cs.cmu.edu/~410/doc/8259A.pdf),
pp. 7, 9–19. Text extraction was inspected; remote rendering of the relevant
figures was unreliable. No physical chip measurements were performed.

The x86 subset supports programmable vector bases, ICW3 master/slave identity,
fully nested priorities, masks, edge/level requests, separate specific and
nonspecific EOIs, priority rotation, special masks and two acknowledge phases.
Selection is fixed in phase 0; phase 1 supplies the vector and completes AEOI.
AEOI on a slave follows the 1985-and-later variant. Spurious level 7 does not
create an ISR bit for that unit; a spurious slave leaves the master's cascade
ISR set. ICW1 requires a fresh input edge in edge mode. OCW3 without RR preserves
the previous read selector. Special-mask nonspecific EOI skips masked ISR bits.

## Explicit implementation policies and omissions

- This is a synchronous boundary model, not pin-level timing. Short-pulse widths,
  synchronizer delays and INT deassert/reassert propagation are not modeled.
- A pair freezes selected master/slave levels together at phase 0. Subsequent
  input changes cannot replace the acknowledged vector. No claim is made about
  subcycle races on the cascade bus. A caller may acknowledge after INTR fell,
  which exposes the spurious-master path.
- IRQ0..7 address master pins, IRQ8..15 slave pins. External cascade-pin drive
  is ORed with slave INT. Normal AT wiring must leave that external pin alone;
  the raw-pin test uses it to exercise a selected slave with no pending request.
  This is a diagnostic fixture, not a claimed PCS286 source of spurious IRQ15.
- Emulator reset clears pins, masks all and requires initialization. It does
  not pretend the 8259A has a hardware reset pin or a known power-on vector.
- Poll, SFNM, buffered and MCS-80/85 modes reject explicitly without state
  mutation. Their implementation/testing is still pending. The API is not a
  fully complete arbitrary 8259A emulator.
- A selected missing/mismatched slave returns UNSUPPORTED before either unit
  accepts the request. Open-bus electrical behavior is left to future board work.
- Byte I/O only; board owns word splitting and waits. Reads/inspection have no
  effects; DEBUG writes reject. Programming during a frozen INTA pair rejects.
- Creation registers no callbacks; destruction invokes none. Reset or detach
  the output before destroying a live consumer. No callback may reenter PIC.

## Tests and status

The original `pcs286-component.at-pic` acceptance test now executes, not skips.
`at-pic-contract` adds all 15 non-cascade IRQs, 2,048 combinations of rotated
priority and masks, higher/lower-priority nesting, held/deasserted edges, level
retriggering, IRQ7/15 spurious cases, phase ordering/vector freeze, mixed AEOI,
rotation and special masks. It checks relocated ports/cascade wiring, mismatch
failure, initialization, observational DEBUG, errors and allocation failure.
These are authored contract tests, not captured hardware vectors.

The 286 CPU has **not** changed in this packet. Its event-delivery paths still
return UNSUPPORTED. The next gate is real-mode interrupt entry/return: two INTA
callbacks, IVT lookup, FLAGS/CS/IP frame, IF/TF rules, STI/CLI, IRET and NMI/trap
inhibition. It must test failures without repeating acknowledgements or rolling
back completed external side effects. Then wire this PIC into the authored
RAM/ROM/AT diagnostic. Protected delivery and timing require separate evidence.

Headland and AT DMA remain absent. No runtime registration, firmware POST,
GUI launch or PCS286 hardware validation follows from the PIC tests.
