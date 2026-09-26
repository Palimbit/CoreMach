# 80286 string-fault restart: evidence and implementation gate

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Current task integration: [F contract](pcs286-protected-tasks.md) supersedes
this tranche's task/NT unsupported statements. Earlier validation counts below
are historical. Public PE remains blocked. D9 string corrections now stage
in the outgoing task image before a task-gate save.


Current D9 update, 2026-09-24: [the PRM-1987 functional contract](pcs286-protected-instruction-policy.md)
implements private protected string fault delivery and the selected corrected
REP snapshot. Official documentation suffices for documented behavior; hardware
capture is not a blanket prerequisite. SCAS/LODS and bus-order interpretations
are explicit. The matrix below remains a physical-fidelity acquisition plan;
it no longer blocks the entire functional instruction path.

Prior D8 checkpoint: valid private protected string/REP/I/O paths
join shared protected access. Segment/IOPL rejection still stops as an explicit
evidence gap before current-element effects. This is not guest fault delivery;
prior completed elements remain committed. See [D7/D8 scope and tests](pcs286-protected-instructions.md).
The evidence matrix still applies to hardware claims. D9 records the policies
that replace this checkpoint's blanket execution stops.

Original research baseline: `25ec581fb37b73998f16557779b8eefb2f088097`.
This decision does not disable working string instructions or external-interrupt
restart. It concerns their still-unsupported segment-fault paths.

## Primary evidence

Original scans inspected visually on 2026-09-24:

- [Intel LOADALL technical memo](https://docs.pcjs.org/manuals/intel/80286/80286_LOADALL.pdf),
  printed pages 13–14. Page 14 describes handler adjustments, not an atomic
  instruction rollback. MOVS distinguishes source from destination faults;
  CMPS starts with destination. SCAS explicitly says SI in the scan, not just
  the web transcription. LODS is absent. This is an unresolved document
  inconsistency, not permission to implement an SI change for SCAS.
- [Intel REP restart erratum, 15 October 1984](https://docs.pcjs.org/manuals/intel/80286/80286_B2_B3_REP-1984-10-15.pdf),
  page 1. Its scope is protected-mode violations. B2/B3 and C2 have a final
  element counter discrepancy for MOVS destination, INS memory destination
  and OUTS IOPL faults: zero rather than FFFF. Future corrections are announced
  without naming a first corrected stepping. INTR restart is separately
  described as working on every stepping.

Neither source establishes a complete real-mode register/fault-stage oracle
for our core. A PCS286 model name or clock frequency does not identify silicon
stepping. No PCjs implementation was consulted or copied.

## Required evidence matrix

This is a test acquisition plan, not invented expected CPU state.

| Operation | Distinguish fault sites | Unresolved observations required |
| --- | --- | --- |
| MOVS | Source / destination / both invalid | SI, DI, CX and completed read/write effects at each site |
| CMPS | Source / destination / both invalid | Index order, saved FLAGS and stopping comparison |
| STOS | Destination | DI, CX and whether any byte was written |
| LODS | Source | AX, SI, CX; independent evidence missing from memo |
| SCAS | Destination | DI versus the memo's SI, FLAGS and CX |
| INS | I/O privilege / memory destination | Consumed port reads, DI and CX |
| OUTS | Memory source / I/O privilege | Completed reads/writes, SI and CX |

For each row record: exact manufacturer/part/stepping; real versus protected
mode; ordinary versus explicitly loaded hidden cache; byte/word; DF=0/1;
no REP/F2/F3; initial CX=0/1/2/FFFF; first/later/final element; source override
including SS; aligned and boundary-crossing accesses. Not every combination
applies: IOPL checks require protected execution. Mark inapplicable cases
explicitly rather than fabricating corresponding real-mode tests.

Capture the entry state of the guest handler, saved IP/FLAGS/frame, memory
effects and independently observable I/O. Then test handler repair plus IRET
and completion. A successful final buffer alone cannot certify restart.
Preserve conflicting observations rather than excluding them from a corpus.

## Current implementation and decision

At the baseline, `execute_string` stages the current element's registers until
successful data accesses. Earlier completed iterations remain committed.
`execute_string_io` preflights the memory segment before port access.
These are functional host-error policies, not measured silicon fault order.
The selected SingleStepTests regression does not cover strings/REP and cannot
close this evidence gate.

Do not route every failed `data_access` into #13: a transport failure is not
a guest protection violation. Do not reuse scalar whole-instruction rollback
as proof of string fidelity. Do not add stepping flags to the generic engine.
If confirmed variants become necessary, they belong in the 80286 component;
the machine selects a documented CPU model and the scheduler remains neutral.

Implementation order after evidence is sufficient:

1. Private typed fault stage and guest-fault snapshot, separate from host errors.
2. One supported mode/model and one operation with an independent oracle.
3. Handler-entry and guest repair/IRET/retry tests, including final iteration.
4. Remaining operations only as their individual evidence becomes available.

No public ABI, clock policy or production CPU behavior changes in this tranche.
The independent next implementation block is instruction-fetch and control
target limit faults; string evidence remains explicitly open, not a prerequisite
for unrelated CPU work. Protected execution, timings and board completion are
still separate gates before PCS286 boot validation.
