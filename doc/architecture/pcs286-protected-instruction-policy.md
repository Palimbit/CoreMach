# Protected instruction closure: PRM-1987 functional contract (D9)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

The remaining-instruction tranche is implemented and tested under this explicit
functional contract. This replaces D3/D8's blanket evidence stops. It does not
enable public PE or finish the joint access/exception/return audit, privilege
transfers, tasks, populated 80287, timing or PCS286 board integration. Local,
uncommitted/unpublished on `4769e40524bc194747b142f3e7ec908ae4df0897`.

## Source precedence and model limits

Primary contract: Intel, [80286/80287 PRM 1987, 210498-005](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
table 9-3, sections 9.6/11.3, B-60/B-68/B-69/B-71/B-100, individual string entries,
B-92/B-93 and appendix C-2 item 11. The instruction-specific conditions take
precedence over less explicit general descriptions in that same manual.

Intel's [LOADALL memo](https://docs.pcjs.org/manuals/intel/80286/80286_LOADALL.pdf),
p14, supplies string-handler index/count corrections. The [1984 REP erratum](https://docs.pcjs.org/manuals/intel/80286/80286_B2_B3_REP-1984-10-15.pdf)
defines a corrected counter behavior distinct from B2/B3/C2. We select that
corrected behavior as a functional model; this does not identify or certify
a particular physical stepping. The earlier B2/B3 bus-dependent CX erratum
is not emulated. There is no new generic ABI or runtime model-selection flag.

Official documentation suffices to implement documented behavior; hardware
capture is not a blanket prerequisite. Omissions are not automatically declared
architecturally undefined. The following interpretations/policies are explicit,
and must not be described as locally observed silicon facts:

- **LAR:** apply B-60/11.3's visible-descriptor rule to every defined 286
  descriptor type, including interrupt/trap gates queried in GDT/LDT. Reserved
  types reject. Query acceptance does not make a gate usable for a transfer.
- **LSL:** follow B-71's explicit nonconforming condition; conforming code
  rejects with ZF=0, preserving the destination. This resolves the earlier
  uncertainty by source precedence, not by importing 386 behavior.
- **SCAS fault:** use its architectural DI for the memo's adjustment, following
  the PRM SCAS definition. The memo says SI; using DI is a declared interpretation
  of that inconsistency, not an independently verified correction from Intel.
- **LODS fault:** retain the current element's AX/SI/CX. The memo omits LODS;
  this is a deterministic pre-operation policy consistent with the selected
  functional instruction model, not proof of fault microstate on all silicon.
- **Nonrestartable scalar writes:** deliver the specified #GP, keeping
  pre-operation registers/FLAGS and leaving the operand untouched. Intel does
  not guarantee restart for write-protected XCHG/ADC/SBB/RCL/RCR. These tests
  establish emulator policy, not a guarantee that hardware resumes equivalently.
- Complete element/operand preflight and retained host effects are functional
  bus policies; they do not claim measured microcode or peripheral fault order.

## Guest string fault snapshot

`delta` is +1/+2 for DF=0 and -1/-2 for DF=1. The table describes changes from
the start of the **faulting element**, preserving all previously completed ones.
CX changes only with REP. No decrement is applied twice by the outer REP loop.

| Operation and failure | SI | DI | CX with REP |
| --- | --- | --- | --- |
| MOVS source | +delta | unchanged | -1 |
| MOVS destination | +delta | +delta | -2 |
| CMPS destination | unchanged | +delta | -1 |
| CMPS source | +delta | +delta | -2 |
| STOS destination | unchanged | +delta | -2 |
| INS IOPL | unchanged | +delta | -1 |
| INS destination | unchanged | +delta | -2 |
| OUTS source or IOPL | +delta | unchanged | -2 |
| SCAS destination, interpretation above | unchanged | +delta | -2 |
| LODS source, policy above | unchanged | unchanged | unchanged |

CMPS checks destination before source; MOVS checks source first. I/O privilege
precedes memory for INS/OUTS. A denied element performs no operand/device
callback under the complete-element preflight policy. Successful prior elements
and their endpoint effects remain. Source SS limit faults deliver #SS(0); other
range/access and IOPL faults deliver #GP(0), with prefix-inclusive saved IP.
Explicit LOCK privilege failure occurs before the string operation and therefore
does not apply string corrections. REP CX=0 performs no element access/check.

Corrections are staged and published after successful guest delivery. If a host
callback fails during delivery, instruction registers retain their prior state,
completed writes remain, NMI edges remain latched, and the host status stops the
instance without replay. Impossible imported caches remain host errors. No
transport status by itself can request a guest exception.

## LOCK coverage

PRM C-2 recommends XCHG, MOV, MOVS, INS and OUTS. Their valid forms in the
private decoder now execute, including register-only MOV/XCHG, immediate MOV,
both memory directions and MOV segment loads. Explicit prefix CPL/IOPL applies
even without an external operand; register-only operations need no external
lock window. Memory XCHG's implicit exclusion is not an explicit-prefix IOPL
operation. Previously supported memory ALU/shift LOCK forms remain available
under their existing functional contract; no new pin-timing claim is made.

A protected segment load borrows an existing instruction exclusion window:
descriptor transfers carry LOCKED, and helper-local lock edges cannot release
the outer owner. Actual release occurs on completion, guest unwind or host stop.
Register-source segment MOV acquires exclusion before descriptor access. No
instruction fetch occurs while acquiring this window. Null destinations, SS
shadow and accessed-bit writeback keep their ordinary segment-load semantics.
Undocumented prefix combinations/reserved encodings remain outside this contract;
they do not acquire fabricated later-x86 #UD semantics.

## Observed tests

- Existing descriptor matrix now executes all **65,536** cases, replacing 3,072
  host stops with contract-defined ZF/destination outcomes. All old cases remain.
- **7,648** new string fault snapshots cover byte/word, DF, CPL, repeat kind,
  CX=1/2/FFFF, alignment, null/range/access/IOPL and SS override.
- **180** decoded guest programs repair segment state and register adjustments,
  execute IRET and finish, including faults after a completed REP element. No
  fixture writes or architectural imports occur after execution begins.
- **88** additional LOCK forms/selector cases cover memory/register MOV/XCHG,
  segment destinations, null/rejected selectors and SS shadow.
- **8,750** additional before/after host failures cover every transfer in 22
  representative string-fault, locked segment-load and scalar-fault routes,
  five host statuses and both alignments. RAM/I/O effects and no replay checked.
- Impossible imported caches remain host errors; NMI during fault delivery is
  retained on successful entry and on host stop. Existing valid-string, scalar,
  explicit-LOCK IOPL, REP/IRQ/NMI/TF and public-gate regressions remain active.

The remaining limitations above are fidelity qualifications of the selected
model, not unimplemented exception paths or a claim that this is a complete
protected CPU. The next block is the joint protection/delivery/return audit.

GCC 16.2 UCRT64 Debug/Release each pass 119 ordinary CTests plus the two existing
Headland/AT DMA skips (121 registered), with assertions and warnings-as-errors.
The Debug full run completed before the user-requested pause; Release was
rebuilt and validated on resumption. Python tools: 50/50; provenance: 40
components/221 files, no errors; catalogue: 32 machines/five locales. No local
MSVC or new remote CI evidence. Restricted scans remain outside Git.
