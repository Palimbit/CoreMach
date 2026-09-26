# Private 80286 tasks (F)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Current activation: [public functional step/run](pcs286-protected-public.md)
now use the reviewed profile. Public PE gate statements below describe the
earlier checkpoint; the strict clock gate and stated semantic limits remain.


## Scope and authority

The private decoder executes direct/gated task CALL and JMP, IDT task gates
and current-NT IRET through one task mechanism. LTR remains the E2b load-only
operation. Public PE and strict clocked execution remain blocked. These tests
certify the stated functional profile, not a complete CPU, operating system,
physical stepping, timing model or PCS286 boot. Work remains local and
unpublished on the dedicated branch; PR209 is documentary context.

Primary references, read outside Git:

- Intel [80286/80287 PRM 210498-005 (1987)](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
  figure 8-1, sections 8.2–8.5, tables 8-1/8-2, sections 9.4/9.6/9.7.1,
  table 9-5, SWITCH_TASKS B-12/B-13, INT B-49/B-50, IRET B-51 and JMP B-57/B-58.
  Existing scan SHA-256:
  `ad487ba99b48cd9f61b14c0fe912a04c7cdb4c7c14a18419aa9faf62d8962460`.
- Intel [iAPX 286 Operating Systems Writer's Guide 121960-001 (1983)](https://kib.kiev.ua/x86docs/Intel/486/121960-001_1983_iAPX_286_Operating_Systems_Writers_Guide_1983.pdf),
  printed 7-5: a null LDT selector does not cause a task-switch exception.
  This explicit 286 rule refines the abbreviated B-12 LDT check. It was read
  online, not acquired as a preserved asset or copied to Git.

Source precedence is explicit: table 8-2 and section 8.3 take precedence over
B-58's inconsistent nesting label for JMP. B-13/table 9-5 supply invalid
incoming-segment #TS rules where table 8-1 disagrees. IRET B-51 supplies #TS
for invalid backlink/type, rather than general transfer #GP. Presence faults
are #NP for TSS/CS/DS/ES, #SS for SS and #TS for LDT. Null DS/ES/LDTR are
unusable caches retaining their visible selector; null CS/SS are invalid.
No 386 TSS fields or external emulator implementation were imported.

## Architectural behavior

The selected TSS must be global. Direct CALL/JMP check DPL against CPL/RPL;
task gates check their own privilege/presence and ignore target TSS DPL.
IRET reads the cached current TSS backlink and requires a busy destination.
CALL/JMP require an available destination. Type/privilege precede presence.

Dynamic words at offsets 14..41 save IP, FLAGS, AX/CX/DX/BX, SP/BP/SI/DI,
ES/CS/SS/DS. The saved IP is after a completed CALL/JMP/IRET or software INT,
and the interrupted/restart IP for boundary events/faults. LDTR at offset 42,
privileged stacks and other static fields are not overwritten by the save.

| Operation | Outgoing busy | Incoming busy | Backlink | NT |
| --- | --- | --- | --- | --- |
| CALL / task event | Preserved | Set | Incoming gets old TR selector | Set incoming |
| JMP | Cleared | Set | Preserved | Clear incoming; outgoing preserved |
| IRET with current NT | Cleared | Required, preserved | Preserved | Clear saved outgoing; restore incoming |

Incoming state reads offsets 14..43, normalizes reserved FLAGS bits under the
existing 286 contract, sets MSW.TS and takes CPL from incoming CS.RPL. GDTR,
IDTR, other MSW bits and asynchronous signals are not TSS state. Cache checks
run LDTR, SS, CS, DS, ES. CS may be conforming at DPL <= CPL; otherwise its
DPL must equal CPL. Stack RPL/DPL must equal CPL. Data visibility uses the
ordinary readable/data rules. Code/data accessed bytes use the shared locked
RMW mechanism. A new task's TF first acts after its first instruction; the
outgoing instruction's sampled TF does not leak into the new task.

Task events restore IF/TF from the TSS. They do not clear them like an
interrupt gate. Only a fault that owns an error word pushes that word onto
the new stack; software/INTA vector numbers alone never request one. The push
precedes the incoming IP-limit check. A loaded SP is otherwise checked when
used. Error-stack range gives #SS(0); IP range gives #GP(0). Selection errors
retain selector/TI and originating EXT while clearing RPL.

## Explicit state and bus policies

These are functional policies, not observations of 286 microcode:

- Descriptor contents are snapshots. Incoming availability/presence is
  rechecked with a fresh locked access byte before busy is written; IRET
  checks busy without rewriting it. Outgoing busy clearing is another fresh
  RMW. Whole-descriptor replacement requires external serialization.
- CPU state is staged across each mechanism call. Successful host transport
  with a guest fault after task selection publishes the selected context;
  subsequent faults and saved return IP belong to that context. No old-task
  instruction replay occurs. Unloaded selectors are visible with invalid
  caches until their checks succeed. Successful NT IRET unblocks NMI;
  faulting-IRET micro-order remains qualified by the earlier contract.
- B-12's short-save/short-incoming cases do not specify every usable register
  image. The model exposes a **SELECTED** phase with new TR and retained old
  registers until the complete incoming image has been read. Missing/short
  outgoing TR (limit < 41) gives #TS(new TSS), writing a nested backlink but
  not an out-of-bounds save. Incoming limit < 43 faults after outgoing save
  and linkage. No speculative out-of-limit incoming read is performed. This
  partial-state choice is not claimed as a measured silicon result.
- Save precedes incoming reads. Physical aliases therefore see completed
  writes, including self-aliasing TSS storage through distinct descriptors.
  Aligned words use word transactions; odd words split into bytes, wrapping
  at 24 physical bits. Segment offsets never silently wrap through a limit.
- Ordinary INTA retains its established first-frame-word exclusion. Task
  entry holds the existing INTA exclusion through the mechanism/delivery
  boundary; nested busy/accessed RMWs borrow it. This is a conservative
  functional exclusion profile, not pin timing certification.
- Every host failure returns its exact status, releases LOCK, retains
  completed endpoint effects and callback signals, and latches a stop.
  The failing transfer contributes no successful wait count. There is no
  host-to-guest exception conversion or retry, including after a busy write.

The delivery coordinator applies original -> protection fault -> #DF within
one context. Selection of a new task starts fault handling in that context.
A rejected #DF before selection shuts down; usable task gates support #DF
handling and NMI recovery from shutdown. A 32-attempt diagnostic bound on
successively broken task contexts returns host UNSUPPORTED and stops; it
does not invent guest double fault or shutdown. An unusable SS created by an
actual partial task load can yield #TS when an ordinary gate attempts to use
it; arbitrary impossible imported caches still remain host errors.

D9 string-fault SI/DI/CX corrections now stage in the **outgoing** delivery
image. A task gate saves those corrected values into that task's TSS and
leaves the new handler's registers intact. Ordinary delivery still commits
the correction on successful guest delivery/shutdown. Host failure does not
publish the staged correction, although already completed TSS writes remain.
A later fault in the new task does not apply the correction again.

## Validation and remaining work

`cpu286_protected_tasks.c` contains authored synthetic memory and programs:

- Complete CALL/JMP/return register, static-field, busy/backlink/NT checks;
  24,576 target access/privilege cases, 34,816 incoming segment cases,
  131,072 outgoing/incoming limits and 196,608 FLAGS images.
- Null/global/local selectors, LDT index zero, 768 fresh-byte races,
  expand-down/error-stack rejection sweeps, SP=0 pushes, 24-bit wrap and
  aliased TSS storage. The error-stack sweep exhausts rejected SP ranges
  for three chosen limits; it does not claim every accepted limit/SP pair.
- Decoded task transfers, guest repair of invalid SS/LDT and absent CS/DS
  or bad incoming IP, nested NT returns; software/INTA coverage for every
  vector, SS/STI/HOLD, INTA failures, TF handoff, task #DF and NMI recovery.
- Two complete programs start at reset, establish PE/LDTR/TR with guest
  instructions, repair #TS in another task, CALL/IRET, gate-JMP/direct-JMP,
  INT/NMI/IRQ and finish with an observable marker and HLT. No fixture state
  import or memory repair occurs after reset. All bus boundaries are tested
  before/after five host failures using fresh CPUs, never stopped-CPU replay.
- Every-transfer mechanism and post-selection failures preserve exact RAM
  effects and callback NMI. A synthetic advancing-gate bus exercises the
  32-context diagnostic bound. String faults save the correct outgoing image,
  undergo guest TSS repair/IRET/retry, and retain host-error semantics.

The full Debug/Release, Python and provenance results are recorded in the
current coverage ledger. No MSVC or new remote CI result is implied. Tests
do not resolve physical stepping, D9 restart interpretations, faulting-IRET
NMI timing, exact bus/clock timing or 80287 completeness. The subsequent [joint C/D/E/F review](pcs286-protected-full-joint.md)
passes for the documented private profile; public functional activation remains
the next change, with public API tests;
Headland/IOC02/AT DMA/device assembly and machine boot remain separate work.
