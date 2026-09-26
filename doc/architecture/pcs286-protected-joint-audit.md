# Private 80286 joint protection/delivery/return audit (D10)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

The later [joint C/D/E/F review](pcs286-protected-full-joint.md) supersedes
this D10 checkpoint for composed privilege/task integration. The scope and
results below remain the historical D-only record. Public PE remains gated.


[E1](pcs286-protected-returns.md) subsequently extends this audit's private
scope with outer IRET and same/outer RETF, including dedicated frame, privilege,
signal, host-failure and executed repair tests. D10's CA/CB and outer-return
exclusions below describe its historical checkpoint. Public activation remains
unchanged; calls/gates/inner stacks still need their own integration audit.

E2a adds ordinary CALL/JMP/gates and inner call stacks in the separate
[call-transfer contract and suite](pcs286-protected-calls.md). D10's same-CPL
result remains historical; inner IDT entry/loaded-TR establishment and their
joint event audit are still pending. No public gate is lifted.

## Decision and scope

The joint audit is complete for the current **private, same-CPL functional
contract**. It joins the C/D1-D9 access and instruction consumers to bounded
event delivery and ordinary IRET. It found and fixed one boundary-reporting
defect: an INTR acknowledgement returning vector 1 was labelled a sampled
debug exception. Origin now determines that classification; a protection fault
encountered during delivery remains an exception boundary. The frame and
architectural execution were already correct. The regression failed before
the correction and passes afterward.

This is conversation point 2, not completion of the seven-block protected-mode
plan. Public `bm_286_step`, `cpu.ops.run`, default internal PE dispatch and
strict clocked execution remain gated. Passing this private audit does not
change those entry points or expose a switch to bypass them. Public activation
is a separate integration change requiring an explicit supported-scope contract
and tests of those public entry points. Privilege transfers and tasks remain
the next implementation blocks E/F; this audit does not approve them.

## Source review and evidence classification

Documented architectural rules were rechecked in Intel's [80286/80287 PRM,
210498-005 (1987)](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf):
9.1/9.2 (origin, INTA vector, EXT), 9.6.2 (double fault/shutdown and NMI/reset
exit), 9.6.6/9.6.7 (SS/GP and restart/error words), and IRET B-52 (second stack
word, RPL, complete frame, selector and target checks). The locally preserved
scan with SHA-256
`ad487ba99b48cd9f61b14c0fe912a04c7cdb4c7c14a18419aa9faf62d8962460`
was read outside Git; the web retrieval returned 403. No new asset or external
emulator code was copied. Existing authors, licenses and provenance remain.

Observed results below are synthetic guest execution and injected host failures,
not hardware captures. INTA values 0..31 are adversarial controller inputs:
Intel reserves those vector assignments. The test does not recommend using
them in guest software. #DF tests check classification, frame/shutdown and
transport behavior; they do not claim a restartable double fault or replace
Intel's recommended task-gate handler.

The D9 source interpretations (SCAS/LODS and nonrestartable scalar microstate),
strict expand-down interval at limit FFFF, successful-IRET-only NMI unblock,
consecutive SS inhibition and logical LOCK/transfer ordering remain stated
model policies or physical-fidelity qualifications. This audit establishes
their integration consistency, not silicon validation or fault-time pin timing.
No 80287, protected OS or PCS286 firmware/POST result is implied.

## Consumer and ownership review

Review covered all direct bus call sites and the private positive opcode list
in `cpu_80286.c`, plus `access_286.c`, `descriptor_286.c` and `exception_286.c`.

| Consumer | Protected route and ownership | Relevant regression suites |
| --- | --- | --- |
| Required instruction bytes, prefixes, immediates | `next_byte` checks cached CS per required byte, wide cursor and ten-byte bound; faults retain prefix IP | protected-execution, protected-transition, protected-joint |
| MOV, ALU, shifts, multiply/divide, decimal, XLAT, ARPL | `operand_access` / `read_rmw_operand`; complete scalar preflight; architectural results after successful transfers | protected-instructions, protected-instruction-faults |
| LDS/LES, BOUND, table instructions | Complete 4/6-byte preflight, then protected word accesses; no partial destination/table commit | protected-instructions, protected-transition |
| PUSH/POP, near control, PUSHA/POPA, ENTER/LEAVE | Shared SS checks; aggregate reservations/display sources preflight; target checks before final SP/IP commit | protected-stack, protected-enter |
| Segment loads and descriptor queries | Ordinary operand check, then table/selector-specific helper; A-bit commit uses explicit exclusion; rejection is fault metadata or query ZF, never host-status reinterpretation | descriptors, protected-queries, protected-instruction-faults |
| Scalar I/O, string I/O and memory strings | IOPL and complete element preflight precede endpoint effects; completed REP elements retained; D9 fault corrections commit under its explicit policy | protected-iopl, protected-strings, protected-instruction-faults, protected-joint |
| Guest faults and software INT | Private metadata unwinds to bounded delivery, retains first-prefix/next-IP distinction and explicit error-word rules | protected-delivery, protected-instructions, protected-joint |
| TF/NMI/INTR, HOLD, shadows, REP continuation | Boundary arbitration owns origin, accepted signals and INTA; accepted events discard REP decode; handler return resumes prefix and committed element state | protected-execution, protected-strings, protected-joint |
| Entry and IRET | Dedicated descriptor reads plus shared cached-SS checker; staged frame/register commit; entry failures escalate, host failures stop | protected-entry, protected-iret, protected-delivery, protected-joint |

Descriptor/IDT reads are architectural table accesses, not DS operands; their
direct physical bus route is intentional. Operand LOCK adapters retain the
existing owner and mark transfers, rather than bypassing protection. I/O uses
its own checked route. The remaining `data_access` calls belong to real far
CALL/RETF, real IVT/IRET and FF far-pointer paths. Private dispatch excludes
9A/CA/CB and FF /3,/5,/7 before operand transfers; CF is intercepted by protected
IRET, software faults/INT use metadata, and public event acceptance refuses PE.
The real string routes select protected operand access when the private flag is
set. No reachable unchecked real data/stack/IVT route was found in this scope.

All non-OK accepted host transfers retain their status and endpoint effects;
the CPU latches stop and releases exclusion, without guest fault conversion or
replay. A header incorrectly suggested that state import could clear that stop:
the existing implementation rejects such an import. The header now correctly
states that CPU reset alone clears it. No recovery behavior was changed.

## New executable evidence

`pcs286-component.protected-joint` adapts the existing authored execution
fixture; it uses no ROM, external vector corpus or direct event/IRET helper calls.

- 4,096 INTA vector/interrupt-or-trap-gate/CPL/alignment cases check origin,
  six-byte frames, FLAGS, two acknowledgements and executed IRET restoration.
- 48 nested guest programs exercise rejected IRET selectors (null/type/absent),
  target limit and second-word/full-frame stack limits. The handler repairs the
  original frame or reloads SS, removes its own error word and IRETs to the
  faulting IRET; that instruction retries and returns to the original program.
- 96 instruction/boundary cases combine #GP, #UD, IRET faults, software INT 8,
  sampled #1, external INTR/NMI, #NP, #DF and unusable-stack shutdown. They check
  actual IDT attempt count, error/return words, origin and one-time INTA.
- Four shutdown scenarios check NMI recovery with a usable frame, rejected
  recovery with an unusable stack, no repeated blocked NMI attempt, and reset.
  The successful NMI handler explicitly redirects the saved IP to a safe NOP;
  it does not pretend the failed #DF instruction is restartable.
- 6,190 failures cover before/after every transfer in the escalation/IRET
  routes, five host statuses, both alignments, and both INTA phases. Tests check
  exact retained RAM, unchanged architectural state, lock release, no fabricated
  shutdown and no private/public replay.
- Two 46-boundary programs start at architectural reset, load tables, request
  PE and far jump, repair a null-DS #GP, copy with REP interrupted by NMI then
  IRQ, execute INT3, raise/handle a sampled trap and reach HLT. After reset,
  the fixture only supplies bus transactions and external signal edges; it
  neither imports state nor patches memory. The reset-vector RAM alias is a
  test harness mapping, not a PCS286 motherboard model.
- 6,500 failures cover every transfer of every boundary in those programs,
  including startup and each handler/IRET. Earlier completed instructions and
  writes remain; each injected run uses a fresh CPU to reach the test boundary.
- 1,164 NMI callback edges during IRET rejection/escalation verify pending-edge
  retention on success and host failure with both incoming blocked states.
  These test the declared faulting-IRET policy, not a physical-timing claim.
- 864 public step/run/clock cases cover pending signals, all shadows, HLT,
  shutdown and HOLD. They permit idle waits but no PE fetch, INTA or frame
  access; refusal latches stop and cannot be rescued by private execution/import.

The first whole-program assertion mistakenly expected AX to keep the handler's
selector value. The successfully retried MOV loads BEEF, so that expected value
was corrected; the core needed no change for that test. No cases were removed.

## Validation and remaining work

Baseline D9 Debug reproduced 119 ordinary passes and the two existing skips.
After D10, GCC 16.2 UCRT64 Debug and Release each pass **120 ordinary CTests
plus two existing Headland/AT DMA skips** (122 registered). Assertions and
warnings-as-errors remain enabled. Python tools pass 50/50; provenance covers
40 components/222 files without errors; catalogue passes 32 machines/five
locales. No local MSVC or new remote-CI result is claimed.

All C/D1-D10 changes remain local, uncommitted and unpublished over
`4769e40524bc194747b142f3e7ec908ae4df0897`. The destination remains
`architecture/portable-engine`, never master. PR #209 is documentary context;
the existing implementation PR and CI have not been changed.

Next: E privilege transfers (including stack changes/call gates/outer returns),
then F tasks/NT return. Public activation, physical fidelity/timing and board
integration remain separately scoped work. The joint audit is closed for the
current private contract; it must be extended whenever those paths are added.
