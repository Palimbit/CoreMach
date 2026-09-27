# Ordinary protected CALL/JMP and inner call stacks (E2a)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Current task integration: [F contract](pcs286-protected-tasks.md) supersedes
this tranche's task/NT unsupported statements. Earlier validation counts below
are historical. Public PE remains blocked. D9 string corrections now stage
in the outgoing task image before a task-gate save.


## Scope and remaining boundary

The private decoder executes 9A/EA and FF /3,/5 for direct conforming or
nonconforming code and call gates. Direct transfers preserve CPL. Gates can
redirect either instruction; only CALL to nonconforming code can enter a lower
numeric CPL, selecting SS:SP from an **already loaded, consistent busy 286 TR
cache** and copying up to 31 words. RETF from E1 completes the round trip.

E2a tests explicitly import initial TR; none is fabricated by CALL. The later
[E2b LTR/inner-event contract](pcs286-protected-inner.md) establishes TR through
guest instructions and supersedes E2a's former limit >=43 helper precondition:
only the complete requested SS:SP slot must fit at ordinary stack use. Missing
TR/short slot produces #TS under that explicit functional policy; impossible
loaded-cache encodings remain host INVALID_STATE. No TSS descriptor reload,
busy update, task switch or backlink operation occurs during ordinary CALL.
Task selectors/gates and current-NT IRET remain outside scope.
Public PE and strict clocked execution stay closed; no new runtime option/API.

## Primary source and explicit decisions

Intel [80286/80287 PRM 210498-005 (1987)](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf):
7.5.1 (PDF142-146), figure 8-1 and 8.2 (PDF152-153), 11.2.1 (PDF187-188),
CALL B-24/B-25 (PDF232-233), JMP B-57 (PDF265). The existing local scan,
SHA-256 `ad487ba99b48cd9f61b14c0fe912a04c7cdb4c7c14a18419aa9faf62d8962460`,
was read outside Git. No new asset, external implementation or manual copied.

- Direct nonconforming code requires RPL<=CPL and DPL=CPL. Conforming code
  requires DPL<=CPL and ignores operand RPL. CS RPL retains the actual CPL,
  following 7.4.3/7.5.1.3's invariant, even where the appendix omits that step.
- Gate DPL must cover both CPL and operand RPL; presence follows privilege.
  Gate target RPL and operand offset are ignored. Only five count bits matter;
  reserved payload bytes never become a 386 offset extension.
- CALL B-24 omits the target presence check. Table 7-3 explicitly requires it:
  target type/privilege, then #NP(target) precede stack processing.
- JMP cannot change CPL. For nonconforming target DPL mismatch, B-57 explicitly
  gives #GP(code selector), taking precedence over 7.5.1.3's gate-selector prose.
- TSS SS/SP slots are read at 4+4*newCPL / 2+4*newCPL. B-25's SS selection
  precedes descriptor checks and SP read. Bad SS selector/table/RPL/type/DPL
  gives #TS(selector), absence #SS(selector). Gate/code failures use #GP/#NP.
- New stack needs `8 + 2*count` bytes. Same-level CALL needs four bytes;
  same-level and conforming gates ignore count and do not access TR. A zero
  count performs no old-stack read. Counted source parameters also require a
  complete protected range: no silent FFFF wrap. The functional preflight
  order is new stack, old source range, target IP, before any write. Old-source
  priority relative to IP is an explicit model choice, not a silicon trace.

## Transfer and failure contract

Complete CS/SS checks and frame bounds precede CS then SS accessed-byte RMWs.
An inner call pushes old SS then old SP, copies parameters backwards with
interleaved reads/writes, and pushes old CS then the decoded next IP. This
keeps parameter zero nearest the return address. The old/new physical stacks
may overlap: tests simulate the specified order byte by byte, rather than
assuming an atomic source snapshot. This is functional bus policy, not measured
286 sequencing. TSS slots receive no explicit writes; aliasing memory retains
normal bus effects. Gates receive no accessed/busy update.

CPU CS/IP/SP/SS/CPL commit after every transfer succeeds. FLAGS, NT, DS/ES,
TR and NMI blocking are unchanged by CALL/JMP. Callback NMI edges survive.
Host failures (including IDLE/UNSUPPORTED) retain exact completed memory effects,
release LOCK and stop without retry or guest escalation. Existing authored
same-CPL handlers can repair guest faults and retry the original prefixed CALL.
The callee can RETF to the caller using E1; this does not exercise inner IDT entry.

Indirect pointers use complete four-byte protected preflight. The second word
now uses the protected access path too; the old raw real access rejected valid
expand-down sources. Real independently wrapped pointers keep their prior
path. Gate JMP now publishes the helper's resolved target IP rather than the
ignored operand offset. Executed tests found both integration defects.

## Evidence and validation

New authored `pcs286-component.protected-calls`:

- 8,192 direct type/CPL/RPL/GDT-LDT cases; 4,096 gate type/privilege/presence
  cases; 8,192 target cases; 12,288 inner SS cases.
- 2,359,296 frame/source checks: all SP values, six limits, both growth
  directions, current return frame/new frame/old parameter source, byte oracle.
- 3,072 executed inner CALL/outer RETF round trips across all privilege pairs,
  both alignments and all 256 word-count bytes, including reserved bits.
- 5,900 before/after every-transfer failures across direct/gate CALL/JMP,
  immediate/indirect sources and zero/31-word frames, five host statuses and
  callback NMI. Exact retained RAM/registers, release and no replay checked.
- 48 guest-only absent-SS/bad-SS-RPL/source-range/absent-gate repair programs,
  followed by IRET/retry/inner CALL/outer RETF. No fixture edits after first step.
- 5,184 physically aliased-stack cases, 30 competing guest faults, six explicit
  invalid private TR contexts, 32 complete-pointer range cases and register #UD.
- 80 executed conforming/direct/LDT-gate/no-TR cases, physical 24-bit wrap of
  TSS reads/stack writes/next fetch, and retained prefix/public-stop checks.

Prior D1/D2/D5 cases expecting unsupported conforming/gate JMP, 9A or FF
far transfers were retained
and changed to their concrete result/fault oracle. No tests were removed.
During test development the alias byte-oracle buffer was enlarged to cover
the maximum negative displacement; an incorrect LOCK test setup was corrected
to give CPL3 IOPL3 before testing unsupported forms. The existing CPL>IOPL
fault priority was preserved.

Baseline E1 Debug:121 ordinary passes +2 existing skips. Final GCC16.2 UCRT64
Debug/Release:122 ordinary passes +2 existing Headland/AT DMA skips (124
registered), assertions/Werror active. Python50/50; provenance40 components/
224 files; catalogue32 machines/five locales. No local MSVC/new remote CI.
All changes remain local, uncommitted and unpublished over
`4769e40524bc194747b142f3e7ec908ae4df0897`, targeting architecture/portable-engine.

Next E2b: LTR and its own busy/TSS fault contract, inner IDT entry using TSS
stacks, error-frame/INTA/IRQ/NMI/TF/escalation integration and reset-to-PE-to-TR
programs without state imports. Then F task switching/NT return. Public
activation, timing and PCS286 board/firmware acceptance remain separate.
