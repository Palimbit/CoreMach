# Joint private C/D/E/F integration review

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Current activation: [public functional step/run](pcs286-protected-public.md)
now use the reviewed profile. Public PE gate statements below describe the
earlier checkpoint; the strict clock gate and stated semantic limits remain.


## Decision

The joint review is complete for the **documented private functional profile**:
protected access and instruction execution (C/D), ordinary privilege transfers
(E), and task switching (F). The new composed programs pass without changing
CPU behavior. They extend the earlier D10 audit rather than treating its
same-CPL result as evidence for later task and privilege paths.

Public `bm_286_step`, `cpu.ops.run` and strict clocked execution remain blocked
in PE. The next implementation change is public functional activation with
tests through those public entry points; this review does not silently enable
them. Strict clocks need separate timing work. It does not certify a complete
CPU/80287, protected OS, physical stepping or PCS286 firmware boot.

## Review of ownership and transitions

The direct bus consumers and private positive opcode/prefix dispatch were
reviewed across `cpu_80286.c`, `access_286.c`, `descriptor_286.c`,
`exception_286.c` and `task_286.c`. The established instruction-family tests
remain active; the new suite targets transitions between their domains.

| Transition | Reviewed ownership | Combined evidence |
| --- | --- | --- |
| LMSW / far jump / loads | Retained real caches until guest reload; shared protected fetch/data/stack checks | Every program establishes PE, LDTR and TR from reset |
| Outer IRET / inner CALL / RETF | Old-CPL FLAGS permissions, SS:SP restoration, parameter copying, cached DS/ES cleanup | User A and task B use the same two-parameter inner gate and return |
| TSS entry / ordinary exception | Newly selected task owns fault IP and raw selectors; IDT inner entry uses its TSS stack slot | B faults loading absent DS; ordinary #NP repairs its descriptor and both data caches |
| Inner-stack fault / task handler | #TS from an attempted CALL saves the faulting task's restart IP | B invalidates its own SS0; another task repairs it and NT IRET retries CALL |
| Ordinary handler / nested task | Interrupt gate clears NT, saves it in frame; called task restores the handler's current context | Inner INT/IRQ handler calls a task, returns via NT, then ordinary outer IRET restores user B |
| REP / HOLD / NMI / IRQ | Completed element persists; HOLD does not consume events; task acceptance discards private REP continuation | REP pauses after one byte, task NMI runs with IF clear, IRQ follows on return, remaining bytes finish |
| Privilege fault / TSS repair | IOPL fault belongs to suspended task; task handler edits saved FLAGS | User A's CLI faults, handler raises its saved IOPL, NT return retries CLI |
| Partial task / #NP entry failure | Contributory fault while invoking #NP becomes #DF in that context | Bad B.SS0 prevents #NP and #DF ordinary entry, causing shutdown |
| Shutdown / task NMI | NMI stages a separate usable context; shutdown deasserts only after successful entry | Recovery task writes an explicit marker and halts safely, then reset clears task state |
| Host failure / external signals | Exact non-OK status, completed endpoint effects, callback NMI, exclusion release and latched stop | Every transfer of composed programs plus recovery entry is injected before and after effects |
| Public API boundary | Private coverage cannot open public execution implicitly | Public step/run/clock reject at every PE boundary reached by composed programs |

Ordinary operands use cached segment checks. Descriptor/IDT/TSS physical reads
are architectural table accesses, not unchecked DS accesses. Far pointers use
complete protected four-byte preflight and checked access to both words.
Direct/task gates select their own descriptor and error rules. The real-mode
IVT, far-frame and operand paths remain separated by the private mode flag.
No new unprotected operand route or host-error conversion was found in this
review's scope.

Task saves include the actual next IP for completed transfers and the correct
restart/incoming IP for faults. D9 string-fault corrections remain in the
outgoing task image, as fixed and tested in F; they do not modify a handler's
registers. Task publication excludes callback-owned NMI state. Ordinary entry
and return publish SS/CPL with their frame and preserve the established
FLAGS/NT rules. Reset remains the only recovery from a latched host stop.

## Authored executable evidence

`cpu286_protected_full_joint.c` adapts the existing F fixture and introduces
no ROM, external vectors, guest media or copied implementation. The 64 KiB
physical RAM alias is a harness mapping, not a PCS286 board approximation.

- Eight programs vary bus alignment, global versus local user selectors,
  and a guest-created invalid inner-stack slot. Four take 102 boundaries;
  four take 108. They establish PE/LDTR/TR, outer IRET to user A, copy
  parameters through inner CALL/RETF, enter task B, repair its partial DS
  load through ordinary #NP, optionally repair SS0 through a task #TS,
  interrupt REP with HOLD/NMI/IRQ, nest a task call inside ordinary inner
  handlers, return with both NT and ordinary IRET, repair IOPL, and reach
  an observable final marker and supervisor HLT.
- Guest code itself removes descriptor presence while its old DS cache is
  still valid. The later task load must reload that descriptor and fault.
  Handlers repair memory and reload caches themselves. The fixture performs
  no memory edits or architectural imports after the final initial reset.
- 146,480 failures cover before/after every bus transfer of every boundary
  in those eight programs, with five host statuses and a fresh callback NMI
  at the failure. Exact CPU state and retained RAM effects are checked.
  Each run uses a fresh CPU to reach its boundary; no stopped CPU is replayed.
- 2,304 public step/run/clock checks use those actual executed PE contexts,
  including task handlers and pending events. They perform no fetch, INTA,
  descriptor/frame access or state change and cannot be resumed privately.
- Four additional programs reach real shutdown through a partial incoming
  task's #NP, invalid inner SS0, #DF and failed ordinary #DF entry. NMI enters
  a separate task, writes a recovery marker and halts there. It does **not**
  claim to restart the instruction involved in double fault. Reset is then
  checked. All 3,880 before/after recovery-entry failures retain shutdown,
  accepted NMI, exact memory effects and host-stop behavior.

An initial new-test expectation incorrectly treated a stack fault during #NP
delivery as an independent #TS. The existing coordinator correctly escalated
to #DF and shutdown. The suite now retains that path and separately creates
a direct inner-CALL #TS for repair/retry. No emulator change or removed case
was used to make the review pass.

## Authority, qualifications and activation gate

This review uses the source decisions in [C/D](pcs286-protected-joint-audit.md),
[E](pcs286-protected-inner.md) and [F](pcs286-protected-tasks.md). Intel's
[80286/80287 PRM 210498-005 (1987)](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf)
sections 8.3, 9.6.2 and #NP B-10 support task-state ownership, contributory
fault escalation and reloading DS/ES after an ordinary handler for a partial
task load. The existing scan/extract was read outside Git; no acquisition or
manual redistribution occurred. Earlier authors, licenses and provenance stay.

Observed results are synthetic execution, not hardware captures. Existing
D9 string restart interpretations, short-TSS partial images, 32-context host
bound, faulting-IRET NMI ordering, consecutive SS inhibition and logical
LOCK/INTA policies remain explicit qualifications. Reserved encodings and
prefix combinations outside the documented profile still stop with host
UNSUPPORTED; populated 80287 execution and exact clocks remain unimplemented.

Public activation must route the supported functional profile through the
public step/run APIs and rerun composed execution, host failure and lifecycle
tests through them. It must not expose a diagnostic bypass, remove unsupported
guards, manufacture guest errors from host statuses or relax the timing gate.
Headland/IOC02/AT DMA and full machine assembly remain separate work.

GCC16.2 UCRT64 Debug and Release each pass 125 ordinary tests and two existing
Headland/AT DMA skips (127 registered), with assertions and Werror enabled.
Python passes50/50; provenance covers41 components/229 files without errors;
catalogue32 machines/five locales. No MSVC or new remote CI result is claimed.
Canonical audit details are recorded in the dated worklog.
All changes remain local and unpublished against
`architecture/portable-engine`; PR209 remains documentary context.
