# PCS286 private descriptor queries (D3)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Local, uncommitted continuation of C/D1/D2 on `4769e40524bc194747b142f3e7ec908ae4df0897`.
The private decoder adds LAR (`0F02`), LSL (`0F03`), VERR/VERW (`0F00 /4,/5`).
Public PE activation remains blocked. No new external implementation code,
firmware/media execution, timing or OS/machine acceptance is claimed.

## Source and D9 resolution

Intel [80286/80287 PRM 1987, 210498-005](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
11.3/11.3.1 (PDF189/190), LAR B-60 (PDF268), LSL B-71 (PDF279),
VERR/VERW B-111/112 (PDF319/320), LLDT B-66 (PDF274), appendix D (PDF333).
The local original was read outside the checkout; no manual was added to Git.

Documented: these unprivileged queries report selector rejection through ZF.
LAR returns the access byte in the high byte, with zero low byte; LSL returns
the 16-bit limit. VERR/VERW check readable/writable segment type and visibility.
Ordinary visibility requires DPL >= CPL and DPL >= RPL; readable conforming
code ignores those comparisons for VERR. Only ZF changes; real mode gives #UD.
Operand addressing can still cause #GP(0)/#SS(0). Presence is not one of the
listed query acceptance conditions, unlike a segment load.

The former two unsupported cases now follow the
[D9 source precedence and functional contract](pcs286-protected-instruction-policy.md):
B-71's explicit nonconforming LSL condition rejects conforming code with ZF=0;
B-60/11.3's visible-descriptor LAR rule includes defined interrupt/trap types.
This is an explicit reading of the 1987 text, not imported later-x86 semantics
or a claim of measured behavior on every 286 stepping. LAR acceptance does not
make an incorrectly placed descriptor usable as a transfer gate.

## Implemented contract

`bm_286_pm_query` is a private read-only helper. Null selectors, unusable LDTR,
table overruns, unsupported architectural types and insufficient visibility
return a negative query. Invalid host context/endpoint failures retain host
status. No descriptor accessed-bit write, cache reload or bus lock occurs.
Descriptor contents are read afresh, independently of loaded ordinary caches.

LAR covers data/code, available/busy TSS, LDT, call/task/interrupt/trap gates.
LSL covers data, nonconforming code, available/busy TSS and LDT. Reserved
system types return ZF=0; VERR/VERW reject all system descriptors. LSL returns
the raw limit even for expand-down data, ignoring the reserved final word.

The common system decoder reads the source before destination commit, including
register aliasing. On a negative query the LAR/LSL destination is unchanged;
VERR/VERW never modify it. Operand fault unwind uses the first prefix and
existing delivery/IRET; host errors stop without replay or ZF changes. Reported
waits include successful transfers only. Timing and transfer order are functional
policy, not a silicon measurement. LOCK/REP on these queries remain outside
the selected documented prefix contract.

## Observed validation

`pcs286-component.protected-queries` executes authored opcodes with:

- 65,536 access-byte/CPL/RPL/query/GDT-LDT/alignment combinations, all with
  contract-defined outcomes after D9 (formerly 3,072 host evidence stops).
  Checks all architectural fields, non-ZF flags, P=0/P=1, reserved descriptor
  words, no A update/lock/write, waits and preserved destination on rejection.
- Null/bounds/missing-LDT edges, all eight source/destination register aliases,
  fresh table bytes versus loaded cache, absent writable descriptors.
- Memory source success and null/range/execute-only-CS/SS faults, real-mode #UD
  before operand reads, and impossible imported DS as a host error.
- Eight executed operand-fault/repair/IRET/retry sequences, with no fixture
  edits after execution starts.
- 3,160 before/after every-transfer host failures across four queries, ordinary
  lookup and operand-fault delivery, five statuses and two alignments. Exact
  retained RAM, architectural commit, lock release and no replay are checked.

GCC16.2 UCRT64, engine-only Debug/Release with assertions: 113 ordinary CTests
pass plus two existing Headland/AT-DMA skips (115 registered). Python: 50 tests;
provenance: 40 components/215 files, no errors; catalogue: 32 machines/five locales.
MSVC unavailable locally; no new remote CI or publication.

Those original D3 validation counts describe that checkpoint; D9 Debug/Release
now pass 119 ordinary CTests plus two skips each. Next: joint protection/
delivery/return audit before public activation; E/F transfers and physical
fidelity qualifications remain separate.
