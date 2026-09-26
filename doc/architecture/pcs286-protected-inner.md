# LTR and ordinary inner protected events (E2b)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Current task integration: [F contract](pcs286-protected-tasks.md) supersedes
this tranche's task/NT unsupported statements. Earlier validation counts below
are historical. Public PE remains blocked. D9 string corrections now stage
in the outgoing task image before a task-gate save.


## Scope

E2b closes the private LTR/ordinary inner-IDT tranche. The shared decoder
executes 0F 00 /3 from register or protected memory. Interrupt/trap gates can
enter a more privileged nonconforming code segment, selecting its stack from
the cached 286 TSS. E1 IRET restores the outer context; E2a CALL uses the same
stack-slot validation. Authored programs establish TR from architectural reset,
repair guest faults and cross privileges without fixture state imports after
execution begins. This does not open public PE or strict clocked execution.

Task switches, task gates and current-NT IRET remain F. LTR is not a task switch.
No complete protected CPU, OS, silicon stepping, timing or PCS286 boot claim
follows from these private tests. All C/D/E changes remain local, uncommitted
and unpublished on the existing dedicated branch; PR209 is documentary context.

## Primary authority and explicit interpretation

Intel [80286/80287 PRM 210498-005 (1987)](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
figure 8-1, sections 8.2.1/8.4, 9.1/9.2/9.6, 10.2, SWITCH_TASKS B-12,
CALL B-25, INT B-49 and LTR B-72. The existing scan was read outside Git;
SHA-256 `ad487ba99b48cd9f61b14c0fe912a04c7cdb4c7c14a18419aa9faf62d8962460`.
No manual, firmware, media or external implementation was added to the repo.

Documented LTR checks: PE (otherwise #UD), CPL0 (otherwise #GP(0)), global
non-null available TSS (#GP(selector) for wrong type, busy, TI or table bound),
then presence (#NP(selector)). RPL/DPL do not restrict LTR. TR retains the
selected descriptor's base/limit and visible selector. Availability test and
busy set require exclusion. It neither saves/loads task registers nor clears
old busy, changes NT/MSW.TS, changes LDTR, or reads TSS contents.

**LTR does not apply the incoming task-switch minimum limit of 43.** That
requirement belongs to SWITCH_TASKS/8.2.1, not LTR B-72. Every 16-bit limit is
accepted by LTR when other checks pass. The old E2a imported-cache minimum
precondition is superseded by the following explicitly bounded use policy.

For ordinary inner CALL/IDT use, this functional model requires the complete
four-byte SP/SS slot within cached TR.limit: offsets 2..5, 6..9 or 10..13 for
new CPL0/1/2. Missing TR or a short requested slot gives #TS(TR selector|EXT).
**The complete-slot limit check and its priority are an implementation
interpretation**, based on bounded TSS use; the appendix does not enumerate
this microsequence and this is not a measured 286 stepping result. No later
x86 TSS fields or blanket 43-byte ordinary-entry requirement are imported.
A valid cache with impossible null/local selector, nonbusy/nonpresent type,
invalid validity encoding or non-24-bit base remains host INVALID_STATE.
Writing the GDT after LTR does not reload cached TR.

Documented SS selection follows INT B-49/CALL B-25: read SS at 4+4*CPL,
check non-null/table/type/writability/RPL/DPL (#TS(selector|EXT)), then present
(#SS(selector|EXT)), then read SP at 2+4*CPL. The destination needs the entire
frame. An inner event pushes old SS, old SP, FLAGS, old CS, return IP and the
optional error word, consuming 10 or 12 bytes. Figure 9-4/table 9-1 explicitly
show the sixth word; the ten-byte test in INT B-49 is insufficient when an
error word is required. Old stack bounds do not matter for inner event entry.
Conforming code preserves CPL and uses the old stack without TSS access.

Existing source precedence is retained: type/privilege before presence;
range #SS(0) and target-IP #GP(0) carry no selector or EXT information, following
instruction pseudocode rather than the conflicting general EXT wording in
table 9-1. Selection failures carry EXT for an external cause. Frame bounds
precede target IP; all guest rejections precede accessed/frame writes.

## Bus and state contract

LTR reads the descriptor, acquires LOCK, rereads the access byte, rechecks
available type and presence, writes busy, releases LOCK, then commits TR.
Fresh competing busy/type/presence changes produce the relevant guest fault
without overwriting that byte. Other descriptor fields are the initial snapshot;
whole-descriptor replacement requires external serialization. This byte-level
protocol is functional policy, not a measured bus timing model.

Inner event entry validates code, stack and IP, performs CS then SS accessed
byte RMWs, then writes the frame. CPU SS/SP/CS/IP/CPL/FLAGS commit only after
every transfer succeeds. TF/NT are cleared; interrupt gates also clear IF.
Other FLAGS retain their entry value. INTA exclusion covers both acknowledgments
and the first complete pushed word (old SS for inner entry); accessed RMWs borrow
that exclusion. Odd words split into two byte transfers, physical addresses
wrap at 24 bits, and protected ranges never silently wrap at 16 bits.

The delivery coordinator publishes SS and CPL as well as the existing entry
fields. Original -> protection fault -> #DF escalation can use a distinct
inner stack; rejected #DF enters shutdown. NMI can recover through a usable
ordinary inner gate. Task-gate #DF remains unsupported and Intel's task-based
recovery recommendation is not satisfied by these ordinary-frame tests.

Host failure returns its exact status, preserves completed endpoint effects
and incoming callback signals, releases LOCK and latches a stop. It creates
no guest exception and permits no replay, including after an effected busy
write or partial stack frame. Only reset clears the CPU stop. Preflight,
publication and alias effects are explicit functional policy, not silicon
microstate certification. Existing faulting-IRET and D9 qualifications remain.

## Observed validation

New authored `cpu286_protected_inner.c` exercises:

- 16,384 LTR access/CPL/RPL/TI/alignment cases, every 16-bit LTR limit,
  512 competing locked-byte cases, register/memory decoding and 24-bit wrap.
- 24,576 SS type/privilege/LDT/EXT/error-frame cases; 8,192 target-code cases;
  393,216 cached-slot limits shared with CALL and 1,572,864 full-frame bounds,
  including expand-down and an unusable old stack.
- 262,144 FLAGS images; 1,024 software/INTA vector/outer-IRET cases, verifying
  origin rather than vector-based classification and the first-word lock.
- Competing faults, physical wrap, SS/STI inhibition with HOLD, #TS/#GP/#DF,
  shutdown and successful/failed inner NMI recovery followed by reset.
- 6,580 per-transfer/INTA host failures with callback NMI and exact memory
  effects, plus 657 successful callback-edge cases. No retry of stopped CPUs.
- Two 68-boundary programs from reset: guest repair of absent LTR descriptor,
  STR observation, outer IRET, guest repair of bad TSS SS, inner INT/NMI/IRQ,
  parameterized CALL/RETF, #GP repair/retry, TF and observable completion/HLT.
  All 15,140 before/after transport-failure cases across their boundaries use
  fresh CPUs to replay completed instructions, never a stopped CPU.

Previous entry/delivery/CALL tests retain their cases, now checking newly
supported outcomes or architectural rejection instead of old unsupported gaps.
GCC16.2 UCRT64 Debug and Release each pass 123 ordinary tests with two existing
Headland/AT DMA skips (125 registered), assertions and Werror enabled. Engine
only, no GUI, firmware or guest media. Python 50/50; provenance 40 components,
225 files; catalogue 32 machines/five locales. No local MSVC or new remote CI.

Next: F task-switch save/load, busy/backlink, task-gate and NT-return semantics,
then joint full-CPU integration review. Public activation, strict timing and
Headland/IOC02/AT DMA/device assembly remain separate work.
