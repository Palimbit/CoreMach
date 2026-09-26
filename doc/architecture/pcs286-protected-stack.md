# PCS286 private protected stack and near control flow (D5)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Local uncommitted continuation of C/D1-D4 on
`4769e40524bc194747b142f3e7ec908ae4df0897`. Public protected step/run/clocked
entry remains blocked. No new remote CI, hardware timing, OS or PCS286 boot claim.

## Source and exact scope

Intel [80286/80287 PRM 1987, 210498-005](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
CALL B-23 (PDF231), LEAVE B-64 (PDF272), LOOP B-70 (PDF278), POP B-83/B-84
(PDF291/292), POPA B-85 (PDF293), PUSH B-87 (PDF295), PUSHA B-88 (PDF296),
RET B-94 (PDF302), plus the earlier branch/segment/access contracts. Original
read outside Git; no restricted document, ROM, media or external code copied.

Documented: PUSH SP saves its incoming value on the 286. PUSHA saves AX, CX,
DX, BX, original SP, BP, SI, DI; POPA restores the reverse order and discards
the saved SP. CALL near saves the following IP without reloading CS; relative
IP arithmetic is modulo 65536. RET near pops IP and optionally releases the
immediate byte count. LEAVE reads the old BP from SS:BP. LOOP decrements CX
without modifying FLAGS; Jcc/JCXZ do not decrement CX. POP segment validates
and caches its selector; POP SS inhibits interrupts through the next instruction.

Private positive list adds 50-61, 06/0E/16/1E, 07/17/1F, 68/6A, 8F /0,
C2/C3/C9, E8, 70-7F, E0-E3 and FF /2,/4,/6. Existing E9/EB remain supported.
FF INC/DEC, far CALL/JMP and reserved fields stop before operand access.
ENTER, far calls/returns, arithmetic/RMW, LDS/LES, strings/REP/INS/OUTS, LOCK
and software events remain separate work; direct nonconforming far JMP and
same-CPL IRET retain their earlier bounded contracts.

## Access, commit and failure contract

Ordinary stack words always use SS through protected operand access. Segment
overrides affect explicit memory operands but never the implicit stack. Memory
EA is decoded from the incoming registers. PUSH SP and indirect CALL SP consume
the incoming SP; both POP SP encodings replace SP after the read. POP SS reads
through old SS and commits the new cache and incremented SP only on success.

PUSHA/POPA preflight one complete 16-byte SS range, including the discarded
POPA slot, before any stack transfer. Normal and expand-down bounds share the
cached checker. A block crossing FFFF is rejected; PUSHA from SP=0 can allocate
FFF0..FFFF, and POPA from FFF0 can finish at SP=0. POPA skips the discarded word's
bus read. Other words preserve existing access order, odd splits, wait accounting
and 24-bit address behavior. Complete-range preflight and omitted discarded-slot
read are functional policies, not a measured 286 physical bus trace.

Near targets use protected CS fetch validation before IP commits. CALL checks
the target before writing the return word; RET reads its word then checks the
target, and LOOP stages CX until a taken target succeeds. Untaken branches do
not validate the unused destination. RET's immediate adjustment and LEAVE's
final SP use 16-bit arithmetic; released bytes are not memory accesses. These
orders do not certify silicon precedence for simultaneous protection failures.

Architectural registers stay unchanged on endpoint failure, even after earlier
writes completed. Endpoint effects remain, original host status is returned,
locks are balanced and the instance stops without replay. Guest protection
faults retain the first-prefix restart IP and use bounded private delivery.
The public PE gates, timing UNKNOWN and instance ownership are unchanged.

## Observed synthetic validation

`pcs286-component.protected-stack` adds:

- 640 register/immediate cases across all CPLs, alignments, general registers
  and every sign-extended immediate byte; explicit segment pushes and aliases.
- 56 PUSHA/POPA round trips, normal/expand-down bounds, wrap endpoints and a
  deliberately replaced saved-SP slot; POP segment null/cache/accessed-bit cases.
- 131,104 Jcc/LOOP/JCXZ cases: all condition flags and signed displacements,
  counter zero/one/underflow and unchanged FLAGS; untaken out-of-range targets.
- CALL/RET round trips, immediate cleanup, indirect register/memory forms,
  SP aliases, BP-based memory/segment overrides, target wrapping and memory alias.
- 24 delivered SS/GP/NP fault cases, including aggregate range/wrap, LEAVE,
  near targets, LOOP CX staging, read-only destination and segment load failures.
- Two executed POPA #SS/MOV SS/IRET/retry programs with no fixture edits during
  execution, and two POP SS/POP SP sequences retaining a callback-latched NMI.
- 7,850 failures before/after every transfer in 30 routes, both alignments and
  five host statuses. Full architecture, exact retained RAM, balanced locks and
  rejection of replay are checked, including descriptor accessed writes and
  fault-delivery reads/writes. FF unsupported-field gates are checked separately.

GCC16.2 UCRT64 Debug and Release: 115 ordinary CTests pass plus two existing
Headland/AT DMA skips per build (117 registered). Assertions enabled, warnings
as errors. Existing real-mode regression stays covered. No physical vectors.

## Remaining work

ENTER requires a separate audit of nesting/display reads, allocation bounds,
wrap and overlap; its real-mode handler is still blocked in protected execution.
Remaining ALU/RMW/operand consumers, strings/REP/INS/OUTS, LOCK and software
events must join protection/restart before joint review of public activation.
D3 query-type evidence gaps, E/F privilege/task transfers, ED=1/FFFF, REP errata,
faulting-IRET NMI, consecutive inhibition and physical timing remain open.
No full protected program, OS, 80287 or portable PCS286 acceptance is claimed.

Subsequent local [D6 ENTER integration](pcs286-protected-enter.md) closes the
ENTER item above; the other public activation gates remain.
