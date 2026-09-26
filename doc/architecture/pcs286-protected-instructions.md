# Private protected instruction integration: D7/D8

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Historical D7/D8 checkpoint. [D9 instruction closure](pcs286-protected-instruction-policy.md)
supersedes the unsupported string/RMW/query paths below with an explicit
PRM-1987 functional contract and recorded interpretation limits. Public PE
and the joint activation audit remain separate; no hardware certification.

Local, uncommitted/unpublished continuation on `4769e40524bc194747b142f3e7ec908ae4df0897`.
The public/default PE gates remain closed. The private entry is not installed,
exported through `cpu.ops`, or enabled by a runtime/build switch. Timing remains
UNKNOWN; no OS, physical stepping, populated 80287 or PCS286 boot certification.
**The remaining-instruction block is not complete** while the evidence gates
below remain. Implementation and validation are authored; no external emulator
implementation, captured vectors, firmware or restricted documents are added.

## Primary references and interpretation

Intel, [80286/80287 Programmer's Reference Manual, 1987, 210498-005](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf):
ARPL B-21, BOUND B-22, INT B-49, LDS/LES B-61/B-62, LOCK B-68,
REP B-92/B-93, sections 3.7, 9 and 10.2/10.3. Earlier real-mode arithmetic,
decimal, divide, shift and multiply contracts retain their individual sources
and independent mathematical oracles. ARPL changes only ZF and destination RPL;
SLDT/STR store visible selectors without loading or validating their descriptors.
LDS/LES validate a complete four-byte pointer before segment loading, with both
destinations staged. BOUND uses signed inclusive lower/upper words and #5.

Additional primary evidence is not a generic atomic-retry specification:

- [Intel LOADALL technical memo](https://docs.pcjs.org/manuals/intel/80286/80286_LOADALL.pdf),
  printed page 10, item 18, visually checked: write-protected XCHG, ADC, SBB,
  RCL and RCR can be nonrestartable. NOT is not in that list. Pages 13–14 give
  string-handler adjustments, with operation-specific index/count changes;
  LODS is absent and SCAS says SI in the original scan. No guessed correction.
- [Intel REP erratum, 15 October 1984](https://docs.pcjs.org/manuals/intel/80286/80286_B2_B3_REP-1984-10-15.pdf):
  certain final-element protected faults differ on B2/B3/C2; it does not name
  the first corrected stepping. External INTR restart is documented separately.
- [Intel B2/B3 information sheet, 21 November 1984](https://docs.pcjs.org/manuals/intel/80286/80286_B2_B3_Errata-1984-11-21.pdf),
  page 3: some protected faults can corrupt CX depending on bus activity/waits.
  This reinforces the need for an identified model. These synthetic functional
  tests do not certify B2/B3 fault microstate or pin timing.

See the existing [string restart evidence matrix](pcs286-string-fault-restart.md).
Complete-range preflight, splitting, lock-window edges and staged host-error
state are functional transaction policies, not observed silicon fault order.

## Connected private paths

- Scalar arithmetic/logical/compare/test, INC/DEC, NEG/NOT, shifts/rotates,
  MUL/IMUL/DIV/IDIV, decimal/ASCII adjustment, CBW/CWD and memory XCHG reuse
  the existing handlers. RMW destinations check write permission before read.
  Divide/zero-base AAM can deliver #DE: the private no-fault sentinel is FF,
  rather than zero. Existing real-mode behavior and its oracles are retained.
- LDS/LES and BOUND preflight all four protected bytes, including FFFC as the
  final valid offset in a full segment; the second word uses protected access.
  Real-mode independently wrapped pointer-word offsets remain unchanged.
- ARPL, SLDT and STR join protected dispatch. Real-mode forms deliver #UD;
  LTR and task execution remain separate. WAIT/ESC retain the documented #NM
  guards and explicitly unpopulated extension interface; no fabricated FPU work.
- INT/INT3/taken INTO use software delivery and decoded next IP. Failed gate
  permission uses prefix-inclusive restart IP. Software INT 13 does not gain
  a hardware-fault error word; DPL applies to software gates, and no INTA occurs.
- Valid MOVS/CMPS/STOS/LODS/SCAS/INS/OUTS, byte/word, F2/F3 and DF=0/1 use
  protected operands. CX zero skips element permission and device accesses.
  One successful REP element commits per diagnostic boundary; uninterrupted
  repetition caches the decoded instruction. Accepted TF/NMI/INTR drops that
  cache, saves prefix IP and preserves completed CX/SI/DI changes for IRET.
- Reviewed memory LOCK scalar forms and LOCK MOVS/INS/OUTS use the existing
  exclusion callback. Explicit LOCK checks CPL/IOPL before operand decoding;
  implicit memory XCHG does not use that explicit-prefix privilege check.
  REP exclusion spans successful elements, defers HOLD, and releases on accepted
  events, host stops, reset, state import, destruction or timing/public refusal.
  Public refusal still occurs before the next instruction fetch.

## Explicit unsupported paths

String segment/IOPL rejection stops before current-element effects. It does
not deliver a fabricated scalar rollback frame. Earlier iterations stay committed.
Write-protected XCHG/ADC/SBB/RCL/RCR similarly stop without a guest frame until
their potentially changed fault-state oracle is known. Successful operations
remain implemented. Ordinary tested scalar faults, including NOT write protection,
continue through bounded private delivery. Unreviewed LOCK/REP forms, D3 type
ambiguities, far procedure/privilege transfers and tasks remain unsupported.

Every host non-OK status retains its identity, completed endpoint effects and
latched signals, releases exclusion and stops without automatic replay. Host
IDLE/UNSUPPORTED are not guest faults. Register staging cannot roll back devices.

## Observed validation

`protected-instructions` adds:

- 4,576 protected/real scalar integration comparisons over 286 encoding forms,
  four CPLs, two alignments and both carry inputs. These compare shared handlers;
  the inherited mathematical oracle tests remain the independent ALU evidence.
- 240 doubleword range/segment/CPL/alignment cases, alias/null destinations,
  expand-down boundaries and 125 signed BOUND edge combinations.
- 256 ARPL RPL cases, 16 selector-store cases and 160 write-permission cases,
  including explicit nonrestartability stops and normal NOT/ADD/ARPL #GP frames.
- 8,192 software vector/CPL/gate-DPL/alignment cases; 26 #DE/#BR/#UD/#GP/#NP/
  #NM/software routes; eight decoded handler/IRET/retry streams for DIV, BOUND,
  LDS and LES.
- 7,700 host failures across 27 representative instruction/delivery routes,
  every transfer before/after effects, five host statuses and both alignments.

`protected-strings` adds:

- 672 mode/direction/CPL/alignment cases; all 65,536 initial CX values for one
  STOS iteration; all 14 opcodes with CX zero and unusable caches/denied IOPL.
- String range/null/IOPL evidence gates and later-element denial retaining prior
  work; locked REP and deferred HOLD; explicit LOCK privilege and implicit XCHG.
- TF/NMI/INTR entry, IRET/prefix refetch, unlocked HOLD cache retention,
  callback-latched NMI success/host-stop cases and lock lifecycle checks.
- 2,150 host failures across all 14 opcodes, first/later iterations, before/after
  every transfer and five statuses, with exact RAM/I/O effects and no replay.

GCC 16.2 UCRT64 Debug and Release: 118 ordinary CTests passed, two existing
Headland/AT DMA skips per build (120 registered), assertions active and warnings
as errors. Python tools: 50/50. No new remote CI or local MSVC result is claimed.
The evidence gates require additional primary clarification or independently
captured handler-entry state on identified hardware; another implementation or
a successful final buffer alone cannot close them.
