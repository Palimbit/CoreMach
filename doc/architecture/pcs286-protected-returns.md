# Ordinary protected returns: IRET and RETF (E1)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Current task integration: [F contract](pcs286-protected-tasks.md) supersedes
this tranche's task/NT unsupported statements. Earlier validation counts below
are historical. Public PE remains blocked. D9 string corrections now stage
in the outgoing task image before a task-gate save.


## Scope

Historical E1 scope below. [E2a ordinary calls/gates](pcs286-protected-calls.md)
now extends the private path to direct/conforming CALL/JMP and call gates,
including inner call stacks from an already loaded TR. LTR and inner events
remain E2b; tasks remain F. E1 test counts below are its original validation.

The private decoder now executes ordinary same/outer-CPL IRET and RETF
(CF, CB, CA iw). Outer returns restore SS:SP and CPL and invalidate cached
DS/ES that the resumed level cannot use. This is the completed return tranche
of block E, **not completion of privilege transfers**: far CALL, call gates,
inner interrupt entry/TSS stack selection and task switches remain unsupported.
Current-NT IRET still selects the unsupported task-return path. Public PE and
strict clocked execution remain gated; no runtime option exposes the private path.

## Intel source and interpretation

Source: Intel [80286/80287 PRM 210498-005, 1987](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
7.5.2 (PDF146-148), 11.2.1 (PDF187-188), IRET B-51/B-52 (PDF259-260),
RET B-94/B-95 (PDF302-303) and the existing 10.1 FLAGS contract. The preserved
scan remains outside Git, SHA-256
`ad487ba99b48cd9f61b14c0fe912a04c7cdb4c7c14a18419aa9faf62d8962460`.
No document, firmware or external implementation was added to the repository.

Three source discrepancies need explicit treatment:

- IRET B-52's outer conforming-code inequality conflicts with 11.2.1 and RET
  B-95. E1 follows the latter consistent rule: conforming DPL <= return RPL;
  the return RPL determines the resumed CPL. This is stated source precedence,
  not a claim to have measured every 286 stepping or copied 386 semantics.
- The DS/ES cleanup list prints an OR between CPL and RPL checks; 7.5.2 also
  contains a reversed numeric inequality in its prose. E1 follows the protection
  requirement in 7.3/7.4 and the stated purpose of 7.5.2: ordinary data and
  nonconforming readable code must satisfy both CPL and RPL. Readable conforming
  code is exempt. Otherwise an outer caller could retain inner data access.
- RETF B-95 does not spell out the final parameter adjustment as clearly as
  7.5.2: the immediate skips parameters on the current frame and is added to
  the restored SP. The new SP is not range-checked during return; its next use
  can fault. E1 follows the explicit 7.5.2 description.

Additionally 11.2.2 explicitly gives zero size for ED=1/limit=FFFF and requires
expand-up for a full 64 KiB segment. It corroborates the existing interval
formula against the contradictory earlier prose noted in block C; no special
case or range behavior was changed in E1.

## Check and commit contract

Both returns first check and read the second stack word, then reject a return
RPL below the executing CPL with #GP(selector). Current-NT IRET is selected
before ordinary stack access. RETF never examines NT as a task-return request.

| Path | Frame/check order after RPL |
| --- | --- |
| Same IRET | Full six-byte frame, CS type/privilege/presence, IP, FLAGS |
| Same RETF | CS type/privilege/presence, top IP word range, IP; discard need not be read |
| Outer IRET | Full ten-byte frame, CS checks, SS selector/type/RPL/DPL/presence, IP, saved SP/FLAGS |
| Outer RETF | Full `8 + immediate` byte frame, CS checks, SS at `SP + 6 + immediate`, IP, saved SP |

Code must be executable; nonconforming DPL equals return RPL, conforming DPL
must be no greater. Presence follows type/privilege; rejected code gives #GP
or #NP with selector metadata, and an out-of-range IP gives #GP(0). Outer SS
must be non-null, in its table, writable data with RPL=DPL=new CPL; structural
rejection gives #GP(SS selector), absence gives #SS(SS selector). LDT index zero
is valid when its table/cache bounds permit it. Frame limits use cached SS and
contiguous wide arithmetic; no word or aggregate silently wraps at FFFF.

IRET's FLAGS decisions use the **executing CPL and incoming IOPL**, before CPL
changes. RETF preserves FLAGS, NT and NMI blocking. Only successful IRET
unblocks NMI; pending edges survive. The existing faulting-IRET policy remains
explicit, without claiming physical unblock timing.

Outer DS/ES cleanup uses cached rights, not new descriptor reads or A writes.
Visible selector/table bounds are checked as listed in B-52/B-95; a shrunken
table or unusable LDTR invalidates the selector. A table descriptor's changed
bytes do not replace the hidden cache. Rejected caches are zeroed and marked
unusable; retained caches remain unchanged. There is no reload-time privilege
check on ordinary subsequent accesses.

All guest checks and frame reads precede the only writes: CS A-bit RMW, then
outer SS A-bit RMW. CPU CS/IP/SP/FLAGS/SS/CPL/DS/ES commit only when both succeed.
These are functional transfer/commit choices, not physical bus sequencing.
If the second update fails after the first took effect, that memory effect
survives; CPU state and pending signals are retained, locks release and the CPU
stops without replay. Host IDLE and other non-OK statuses never become guest
exceptions. No generic engine/bus ABI was extended.

## Evidence

The new authored `pcs286-component.protected-returns` suite contains:

- 16,384 CS type/privilege/GDT-LDT cases and 24,576 outer SS cases.
- 1,572,864 outer IRET FLAGS cases using a per-bit oracle and old-CPL permissions.
- 4,194,304 frame checks: every SP across eight limits, both growth directions,
  same/outer returns and both instructions, with a byte-wise wide oracle.
- 262,144 cases covering every RETF immediate and every restored SP, including
  adjustment wrap and deliberately unusable new stack positions.
- 6,336 cached DS/ES validity, privilege, conforming-code, table-bound and
  no-descriptor-reload cases.
- 1,620 before/after host failures across each transfer of same/outer IRET/RETF,
  both alignments and five statuses, with a fresh NMI edge at each failing call.
  Tests compare exact retained RAM, CPU fields, release and no replay.
- 28 competing frame/selector/type/presence/IP fault cases; 40 executed returns
  and next target fetches; LDT SS, RETF with NT, deferred-SP-fault/shutdown and
  physical 24-bit wrap cases.
- 24 guest-only programs: outer return faults on absent SS; the inner handler
  repairs its descriptor, discards its own error and IRETs to retry. The outer
  program then faults on the correctly invalidated DS, repairs it in a same-level
  handler and finishes its load/NOP. No fixture imports or memory patches occur
  after execution starts. These do not claim inner interrupt entry support.

Existing tests that formerly expected outer IRET or CA/CB to stop remain in
the suite, now asserting their concrete guest rejections. No cases were removed.
The initial new test compilation caught an unused helper; an original-frame
preservation assertion uses it, with warnings-as-errors retained.

Baseline D10 Debug reproduced 120 ordinary passes +2 existing skips. E1 GCC16.2
UCRT64 Debug/Release each pass **121 ordinary CTests +2 existing Headland/AT DMA
skips** (123 registered), with assertions/Werror. Python50/50; provenance40
components/223 files without errors; catalogue32 machines/five locales.
No local MSVC or new remote CI. All changes remain local, uncommitted and
unpublished over `4769e40524bc194747b142f3e7ec908ae4df0897`.

Next E2: direct conforming transfers, far CALL/call gates, TSS-backed inner
stack selection and parameter copying, joined to these returns and the D10
audit. Task switching/NT return remain F. Public activation, physical timing
and PCS286 board/firmware acceptance are separate and remain incomplete.
