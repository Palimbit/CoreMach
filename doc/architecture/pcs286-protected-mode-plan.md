# Intel 80286 protected-mode implementation plan

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Scope and integration gates

Implement architectural behavior inside the instance-owned 80286 component.
Do not extend the generic CPU or bus ABI with x86 descriptor concepts. Preserve
real-mode regressions, structured host errors, observable bus transactions and
explicit unsupported results until a path is actually implemented. No timing
accuracy claim, implicit 386 semantics or BIOS-specific success shortcuts.

| Block | Deliverable | Acceptance gate | Status |
| --- | --- | --- | --- |
| 1. Interpretation | Selectors, descriptor types and segment ranges | Exhaustive selectors/access bytes and boundary tests | Implemented, standalone helpers |
| 2. Tables | GDT/LDT lookup, table bounds, selector error metadata | Synthetic tables, unusable LDTR, boundary entries, every-transfer host failures | Implemented as private lookup; instruction integration in block 3 |
| 3. Segment loads | DS/ES/SS validation, cached descriptors, CPL/RPL/DPL, accessed bit; LLDT | Null selectors, presence, type and privilege matrices; no premature state commit | Common state path plus private MOV/POP/LDS/LES/LLDT opcode integration; functional public step/run now active |
| 4. Protected execution | Fetch/data/stack permissions, entry via LMSW and far transfer, instruction checks | Synthetic protected programs, bounds and privilege violations | Private C/D1-D10 instruction tranche and joint audit complete under the documented functional contract; public step/run active after joint C/D/E/F review; strict timing separate |
| 5. Faults and interrupts | Protected IDT gates, exception frames/error codes, IRQ/NMI, IRET, nested failure/shutdown | Guest repair/retry, stack failures, double-fault paths, real-mode regression | D10 same-CPL and E2b ordinary inner entry complete privately, including joint escalation, signals and reset programs; F task gates integrated privately; public functional step/run active |
| 6. Privilege transfers | Call gates, conforming code, stack switching, parameter copying, RETF | Same/outer/inner privilege matrices and interrupted transfers | E1/E2a/E2b ordinary returns, calls/gates, TSS stack slots and inner IDT entry complete under the explicit private functional contracts |
| 7. Tasks | TSS, LTR, busy/backlink, task gates, task switches, NT return | Synthetic tasks, invalid TSS and nested-task tests | E2b LTR and F task switching/backlink/save-load/task gates/NT return implemented and tested privately; joint C/D/E/F private review passed; public functional step/run active |

The ordering is incremental development, not permission to expose broken
intermediate execution: blocks 4 and 5 share a public activation gate. Full
protected-mode support is not claimed until all seven blocks pass. Instructions
such as LAR/LSL/VERR/VERW must join the relevant table/privilege blocks rather
than remain disconnected success stubs. IOPL-sensitive instructions and system
instruction privilege checks belong to block 4. Task-switch fault ordering
belongs to block 7. Keep a per-instruction coverage ledger as each block lands.

## Current: public functional activation

[Public API contract and validation](pcs286-protected-public.md) connect
step/run to the reviewed C/D/E/F profile before real event/idle arbitration.
The earlier private spelling now delegates to public step; there is no bypass.
Composed programs, every-transfer host failures and lifecycle/budget tests
run through public APIs. Functional activation is complete for the documented
profile; unsupported encodings and the strict clock gate remain explicit.

Next machine work: Headland/IOC02, AT DMA and board assembly under their
own contracts. Exact timing, populated 80287 and remaining physical fidelity
qualifications are separate; no full CPU/OS/PCS286 boot certification.

## Previous: joint private C/D/E/F review

[Composed programs, source decisions and activation conditions](pcs286-protected-full-joint.md)
complete the joint review under the private functional contracts. Eight
reset-to-HLT programs combine ordinary privilege transitions, task switching,
partial-context repair, REP/events and returns. Every-transfer host failures
and public-gate checks preserve signal/effect/stop ownership; task NMI safely
recovers separate reset/shutdown programs. No CPU behavior change was required.

Next: route public functional step/run through the reviewed profile, with
public API execution/failure/lifecycle tests. Keep unsupported-profile guards
and the strict clock gate. This is not full CPU/80287/timing/OS/PCS286 boot
certification. No publication or machine-device implementation is included.

## Previous F: private task switching and return

[F source precedence, state policy and tests](pcs286-protected-tasks.md)
complete the task tranche under the stated private functional contract.
Direct/task-gate CALL/JMP, IDT tasks and NT IRET share TSS save/load and
busy/backlink handling. Faults after selection use the new context; host
failures never become guest faults. D9 string corrections stage in the
outgoing task image. Null LDTR follows the explicit Intel 286 OS guide.

Debug/Release each pass 124 ordinary tests plus the two existing Headland/AT
DMA skips (126 registered). Reset programs, guest repair, every-transfer host
failures, TF/NMI/shadows/HOLD and task #DF/recovery supplement matrices.
Public PE/strict clocks stay closed. Next: joint C/D/E/F review before public
activation; physical microstate/timing and machine devices remain qualified.

## Previous E2b: LTR and ordinary inner IDT entry

[E2b source decisions, contract and tests](pcs286-protected-inner.md) close
LTR and ordinary inner interrupt/trap entry, sharing checked cached TSS slots
with CALL. LTR validates descriptor availability/presence and performs a locked
busy update; it does not validate TSS contents or switch tasks. The former
E2a minimum-limit precondition is superseded by the documented complete-slot
use policy. Missing/short TSS produces #TS; impossible imported caches remain
host errors. Delivery publishes SS/CPL after all writes, retaining signals.

Two authored 68-boundary programs establish TR from reset, repair LTR and TSS
faults in guest handlers, and exercise outer IRET, inner INT/IRQ/NMI, CALL/RETF,
GP/TF and observable completion. Matrices and every-transfer failure tests
cover FLAGS, privilege, TSS/frame limits, exclusion and shutdown recovery.
Debug/Release:123 ordinary passes +2 existing skips (125 total); Python50/50;
provenance40 components/225 files. All C/D/E remains local and unpublished.
Public PE/strict clock gates stay closed. Next F task switches/task gates/NT
return, then the joint full-CPU integration review; Headland/DMA is separate.

## Previous E2a: ordinary calls, gates and inner call stacks

[E2a source, contract and tests](pcs286-protected-calls.md) connect direct and
conforming far CALL/JMP and call gates, including TSS-backed inner CALL and
parameter copies, to E1 RETF. The caller supplies a valid loaded busy TR cache;
LTR and noncanonical TR/TSS fault semantics remain E2b, together with inner IDT
entry and its joint IRQ/NMI/TF/escalation audit. Task/NT switching remains F.
This is not all E2, public activation or a full protected CPU.

New executed tests caught and corrected the second indirect-pointer word using
real access checks and gate JMP retaining the ignored operand offset. Complete
preflight precedes A updates and interleaved stack writes; host failures retain
memory effects and NMI without replay. Prior gap tests remain as guest results.
Debug/Release:122 ordinary passes +2 skips; Python50/50; provenance40 components/
224 files. C/D1-D10/E1/E2a remains local, uncommitted and unpublished.

## Previous E1: ordinary same/outer privilege returns

[E1 source decisions, contract and tests](pcs286-protected-returns.md) add outer
IRET and same/outer RETF. SS:SP/CPL and DS/ES cleanup commit only after complete
validation and both accessed-bit updates. FLAGS privileges use the executing
CPL; RETF preserves FLAGS/NT/NMI and discards parameters on both stacks.
Public PE remains gated. This completes the return tranche, not all block E.

Observed Debug/Release:121 ordinary passes +2 skips; Python50/50; provenance40
components/223 files. All C/D1-D10/E1 work is local and uncommitted/unpublished.
Next E2: direct conforming/far CALL/gate transfers, inner stacks from the TSS,
parameter copying and their joint round trips; task/NT switching remains F.

## Previous D10: joint private access/delivery/return audit

[D10 audit and executable evidence](pcs286-protected-joint-audit.md) close
conversation point 2 for the current same-CPL functional contract. The audit
fixes INTR vector 1 being reported as sampled #1, verifies nested faulting-IRET
repair, escalation/shutdown/NMI recovery and runs two reset-to-PE programs with
fault repair, REP/NMI/IRQ, INT3, TF and HLT. Every program-boundary transfer is
fault-injected; public PE gates remain closed and are tested across pending
signals, shadows and idle states. Public activation is a separate integration
change, not implied by private conformance. No full protected-CPU claim.

Debug/Release: 120 ordinary passes +2 existing skips; Python50/50, provenance
40 components/222 files. C/D1-D10 remains local and uncommitted/unpublished.
Next E/F privilege transfers/tasks; extend this audit as their paths are added.
D9 interpretations and physical NMI/SS/ED/timing qualifications remain explicit.

## Previous D9: remaining instructions closed under the PRM-1987 contract

[D9 contract, interpretations and validation](pcs286-protected-instruction-policy.md)
supersede the D3/D8 blanket stops. String protection/IOPL failures and scalar
write-protection failures now deliver guest exceptions. The selected corrected
REP model applies staged SI/DI/CX adjustments; SCAS/LODS and nonrestartable
scalar state policies are explicit, not claims of independently measured silicon.
LAR/LSL use the stated instruction-specific source precedence. Valid private
LOCK MOV/XCHG forms include segment loads with borrowed descriptor exclusion.

The requested remaining-instruction tranche (conversation point 1) is closed
for this functional contract; it is not completion of this plan's blocks 4/5
or all protected-mode support. Hardware capture is a further fidelity check,
not a prerequisite for every documented instruction. Encodings outside the
contract stay unsupported. Public PE remains closed pending the joint access,
delivery and return audit; privilege transfers/tasks remain blocks 6/7.
ED=1/FFFF, faulting-IRET NMI details, consecutive inhibition, physical stepping
and timing fidelity remain separate. D9 stays uncommitted/unpublished.

## D7/D8 checkpoint: instruction integration (superseded by D9)

[D7/D8 contract and validation](pcs286-protected-instructions.md) supersede the
D7 implementation plan below. LDS/LES and BOUND use complete protected four-byte
preflight, preserving real-mode independently wrapped words. Shared scalar ALU,
RMW, shift/multiply/divide/decimal, ARPL, SLDT/STR and software events are connected.
Valid memory/I/O strings and REP use protected operands, with completed-element
state, prefix restart after accepted events and reviewed memory LOCK forms.

At this checkpoint this did **not** close block 4 or the remaining-instruction tranche.
Intel's LOADALL memo and REP erratum do not establish complete fault snapshots
for our model: string protection/IOPL faults and write-protected XCHG/ADC/SBB/
RCL/RCR stopped as emulator gaps. That blanket requirement was replaced by the
explicit D9 functional contract above; the original concern still qualifies
physical fidelity. Joint access/delivery/return certification and E/F
privilege/task transfers remain separate.
Public PE and strict timing gates stay closed; no 80287 or PCS286 boot claim.

## Previous D7 plan: doubleword operand consumers

A post-D6 read-only review identifies LDS/LES and BOUND as a bounded next step.
Intel PRM B-61/B-62 (PDF269/270) describes the complete LDS/LES four-byte pointer,
segment-cache validation and atomic register result; B-22 (PDF230) describes
BOUND's signed inclusive two-word bounds and #5/#UD/operand faults. Both families
were outside the private positive list at the D6 checkpoint.

Before enabling them, replace their real-specific range/second-word paths with
protected whole-four-byte source preflight and staged protected reads. Preserve
the real-mode pointer wrapping behavior separately: its independently wrapped
word offsets must not silently become a protected doubleword access policy.
Test FFFC as the last complete four-byte source in a full segment, FFFD/FFFE/FFFF
rejection, normal/expand-down and SS versus DS/ES/CS permissions, unaligned
transfers and host failure at each read/table/accessed-bit write. LDS/LES must
retain both old destinations until source and selector validation complete,
including a source addressed through the segment or register being replaced.
BOUND must preserve registers/FLAGS, deliver prefix-inclusive #5 on signed
out-of-bounds, #UD for a register source and operand faults before comparison.
Add executed repair/IRET/retry; retain existing real-mode tests and public gates.
This is a source-reviewed implementation plan, not additional opcode coverage.

## Block D6 continuation: private ENTER

ENTER now preflights the complete stack reservation and every display source,
then preserves interleaved reads/writes for overlaps. BP/SP publish only after
success; locals are reserved without bus writes. [D6 contract and evidence](pcs286-protected-enter.md)
record 18,432 ENTER/LEAVE cases and 6,210 every-transfer failures, allocation/
display faults, executed repair/IRET/retry and callback NMI retention. Public PE
remains closed. Next: remaining ALU/RMW and operand consumers (including LDS/LES),
strings/REP/INS/OUTS, LOCK and software events, then the joint consumer audit.
D3 source gaps and E/F are unchanged. Work remains local and unpublished.

## Block D5 continuation: private stack and near control flow (prior tranche)

Ordinary PUSH/POP, including segment loads, PUSHA/POPA, LEAVE, near CALL/RET,
indirect near JMP, Jcc and LOOP/JCXZ now use the shared protected decoder path.
Whole aggregate SS preflight precedes transfers; register commits follow success.
[D5 contract and validation](pcs286-protected-stack.md) records 131,104 branch
cases, stack/selector/alias checks, executed POPA repair/IRET/retry and POP SS/
POP SP with pending NMI, plus 7,850 before/after transfer failures. Public PE
remains closed. Next: ENTER, remaining ALU/RMW and operand consumers, strings/
REP/INS/OUTS, LOCK and software events; D3 evidence gaps and E/F remain open.

## Block D4 continuation: private FLAGS and scalar IOPL (prior tranche)

CLI/STI, PUSHF/POPF and simple FLAGS, CPL0 HLT and scalar IN/OUT now join
private execution. POPF preserves privileged bits instead of faulting; denied
I/O performs no device access. [D4 contract and validation](pcs286-protected-iopl.md)
record exhaustive saved FLAGS, privilege matrices, event/HLT checks, stack
repair/IRET/retry and 2,940 transfer failures. Public PE stays closed. Next:
ordinary/aggregate stack and control flow, strings/REP/INS/OUTS, LOCK and software
events; query evidence gaps and E/F remain. Work is still local and unpublished.

## Block D3 continuation: private descriptor queries (prior tranche)

LAR/LSL/VERR/VERW now join table lookup, protected operands, ZF/destination
commit and fault unwind. Query rejection is separate from operand exceptions
and host failures. [D3 scope and evidence](pcs286-protected-queries.md) records
62,464 defined matrix cases, 3,072 explicit gaps, eight executed repair/IRET/
retry streams and 3,160 transfer failures. LSL conforming code and LAR
interrupt/trap gate types still stop pending 286 evidence. Public gates stay
closed; IOPL and the remaining operand/instruction/event audit are next.

## Block D2 continuation: private reset-to-PE transition (prior tranche)

LMSW retains real caches; near/direct nonconforming far JMP, table/MSW/CLTS
and LLDT connect to private protection and delivery. Two authored programs
start at reset and execute fault/repair/IRET/retry without imported state.
See [D2 evidence and exact scope](pcs286-protected-transition.md), including
16,384 jump cases and 1,460 before/after host failures. Raw real-cache 82h is
based on a separately identified 286 hardware report; table decoding is unchanged.
The public gate stays closed. Queries, IOPL, aggregate stack/string/REP/I-O,
software events and remaining instruction consumers still need integration.
Conforming/gate/task transfers remain E/F. C/D1/D2 are local and unpublished.

## Block D1 continuation: private shared decoder integration (prior tranche)

The private `bm_286_pm_step_subset` joins existing MOV/LEA/XLAT/register-XCHG/
NOP decoding, protected scalar access/load faults, bounded event delivery and
same-CPL IRET. An explicit opcode list isolates pending handlers. Eight authored
instruction streams now fault, execute a repairing handler and return/retry
without fixture state edits after setup. Public PE activation is still blocked:
LMSW/far transfer, system/IOPL and the complete operand/event audit remain open.

[Scope, source review and integration evidence](pcs286-protected-execution.md)
record the exact allowed forms, 128 MOV cases, 88 fault cases and 2,080 injected
host failures. TF/NMI/INTR, MOV SS shadows and HOLD join the same CPU instance.
This is private subset execution from imported caches, not entry into PE or
full protected program/OS support. C/D1 remain local and unpublished.

## Block C continuation: private cached access foundation

`bm_286_pm_check_access` validates complete fetch/data/stack ranges and cached
permissions without reloading descriptors or repeating load-time privileges.
`bm_286_pm_access` performs bounded transfers with staged reads, exact retained
write effects and a per-instance host-stop latch. Entry/IRET now share the SS
checker while preserving their existing check order. Tests join access faults
to private delivery/repair/IRET/retry; no protected opcodes execute yet.

[Source, contracts, coverage and open evidence](pcs286-protected-access.md)
record 12,288 permission cases, 19,267,584 range cases and 4,500 injected host
failures. The ED=1/FFFF source contradiction remains explicitly open. Block D
must connect operand checks, aggregate stack/string restart, privilege checks
and event unwind before either PE gate can open. This tranche is local and
unpublished on top of 4769e40524bc194747b142f3e7ec908ae4df0897.

## Block B continuation: private bounded delivery (not public activation)

`bm_286_pm_deliver` connects the private entry helper to source classification,
return IP/error policy, #DF/shutdown and a bounded three-attempt coordinator.
It arbitrates pending #1/NMI/#9/INTR, preserves shadows and edges, acknowledges
INTR only once and stages eligible NMI recovery from shutdown. A host-error
latch forbids replay; endpoint errors and unsupported task/inner paths never
become guest faults. The #9 input is synthetic pending metadata, not an NPX.

[Source and validation](pcs286-protected-delivery.md) describe the exact origin
matrix, error words, functional lock/commit policy and remaining gates. Private
repair/IRET/retry is tested; actual protected program execution is still gated.
Handoff C protected accesses and D instruction/event integration come next.

## Earlier block 5 entry foundation

`bm_286_pm_enter_event` validates a complete IDT gate, software gate DPL,
presence, target selector/code, current stack capacity and target IP. It handles
same-CPL nonconforming code and conforming code at DPL <= CPL. CS RPL becomes
CPL. FLAGS/CS/return-IP and optional error code are pushed; TF/NT are cleared,
and interrupt gates additionally clear IF. SS is the existing cached stack,
not reloaded from its descriptor. SP zero can allocate at the top of 64 KiB;
frames crossing the segment boundary are rejected, including expand-down bounds.

The private caller supplies return IP, EXT/software origin and error-code
presence; it owns event arbitration, NMI blocking, INTA and instruction unwind.
Returned guest fault metadata is NOT delivered recursively. Task gates and
inner privilege transfers explicitly return unsupported. Public PE execution
still refuses before fetch. No protected access checks or escalation are implied.
The subsequent private IRET helper is described below.

Architectural preflight precedes all writes. Accessed-byte RMW precedes frame
writes as a functional transaction policy, not a pin-level ordering claim.
CPU registers commit only on success. Host failures preserve status and release
LOCK; partial descriptor/stack memory effects survive and must not be replayed.
Only successful bus transfers contribute waits. Timing remains unknown.
The helper requires serialized, non-aliasing inputs and no existing bus lock.

Source: [Intel 80286/80287 PRM, chapter 9 and INT appendix B](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf).
Tests are synthetic, including every transfer failure before/after effects;
they are not hardware traces or evidence that the PCS286 boots in protected mode.

## Block A continuation: private same-CPL IRET

`bm_286_pm_iret` follows Intel B-52's second-stack-word/RPL/full-frame checks,
validates code/privilege/presence/IP, restores FLAGS under section 10.1 and uses
the existing accessed-byte RMW. CPU state and successful NMI unblock commit only
after all transfers complete. Current NT and outer returns remain unsupported;
the handler must discard its own error code. Pending NMI/trap and shadow are
preserved for future instruction-boundary integration. Faulting-IRET NMI timing
remains an evidence gate, not an assumed later-x86 behavior.

[Source review and tests](pcs286-protected-iret.md) distinguish architectural
rules from transaction policy. Private entry/handler/return tests pass without
opening either public PE gate. Next work is exception escalation and protected
access checks, followed by their joint instruction/event integration gate.

## Block 1 contract

`descriptor_286.h` is private, not installed. Parsing accepts eight readable
bytes without alignment requirements and never changes them. It classifies
segment/system descriptors independently of presence, extracts 24-bit segment
bases and 16-bit limits, and preserves the final reserved word for diagnostics.
Gate payloads are intentionally deferred. Reserved bits do not acquire 386
meanings or invented fault semantics. Null selector detection is distinct from
LDT index zero.

The range helper checks a nonempty contiguous segment-relative range, without
16-bit wrapping. It does not check presence, read/write permission, CPL, RPL,
DPL, physical address formation or A20. It rejects non-segment descriptors.
Callers must perform the remaining access checks when integrated in later blocks.
No global mutable state, allocations, host calls, bus accesses or CPU changes.

Tests cover all 65,536 selectors, 256 access bytes, 65,536 reserved words,
all segment limits at boundaries, all offsets for five selected limits in
both growth directions, byte ranges against a separate wide per-byte oracle,
overflow, zero length, source preservation and a full 64-KiB code range.
Windows host tests are not evidence of execution on PowerPC big-endian.

## Block 2: table access

Private `bm_286_pm_lookup_descriptor` uses the existing `bm_bus_access_fn`
transaction contract with DATA reads, little-endian aligned words or split
bytes on odd bases. No direct RAM pointer, writes, accessed-bit update or LOCK.
Increasing-address transfer order is a functional policy, not a physical bus
trace. Addresses wrap to 24 bits; A20 remains a motherboard responsibility.
Only successful transfers contribute reported waits, matching the CPU path.
No partial descriptor is published on host failure; endpoint effects are not
rolled back and the helper does not retry.

The return status preserves host errors, including IDLE (not a continuation).
On OK, a separate reason distinguishes found, null selector, unavailable LDT
and table overrun. These are not automatically #GP: later instruction/task
callers choose fault vector or non-faulting query semantics. Selector error
metadata clears RPL; EXT is supplied by the caller. Cached LDTR.valid is
authoritative; this helper neither reloads nor validates the LDTR descriptor.
Presence/type/permissions of the fetched descriptor are left to block 3.

Tests enumerate every selector and every limit for GDT/LDT, including LDT
index zero; exercise odd/even transfers, physical wrap, and five host statuses
at every transfer before/after effects. Protected execution remains disabled.

## Block 3a: segment-load preparation

`bm_286_pm_prepare_load` connects lookup to ordinary DS/ES, SS and LLDT rules.
It returns either a prepared cache, a guest #GP/#NP/#SS with error code, or an
unchanged host status. It never delivers an exception or modifies CPU state.
It is NOT suitable for task switches or privilege-level stack switches, which
have different checks/faults. The instruction caller still owns operand-fetch
ordering and the SS interrupt shadow.

DS/ES accept data or readable code. Readable conforming code does not use the
ordinary data privilege comparison. SS requires writable data and matching
privilege levels. LLDT requires CPL zero and a GDT LDT descriptor; it ignores
descriptor DPL. Null DS/ES/LDTR produce an unusable prepared cache; null SS
requests #GP(0). Null hidden-field zeroing is emulator policy, not silicon
retention evidence. Type/privilege rejection takes precedence over presence.

The access byte is preserved; `needs_accessed_write` explicitly prevents a
prepared data/stack cache from being mistaken for a completed load. Accessed
writeback/exclusion and cache commit are supplied by block 3b below; instruction
dispatch and protected fault delivery remain pending. Tests enumerate 49,152 type/CPL/RPL/table/alignment cases,
48 null cases, before/after transfer failures, and invalid table/cache inputs.
Reference: Intel PRM B-61 (LDS/LES), B-66 (LLDT), sections 7.4 and exception
12 definition. See [LDS/LES](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=271),
[LLDT](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=276),
[segment types](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=138),
[stack faults](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=221).

## Block 3b: accessed writeback and cache commit

`bm_286_pm_commit_load` consumes a prepared plan, performs an access-byte
read/OR-one/write under the existing lock callback, releases exclusion and
only then commits the destination cache. Null/LDTR plans do not write A.
All valid data/code plans request the RMW, even when the initial descriptor
snapshot had A set: a bus master might clear it before the locked read.
This corrects the earlier preparation-only optimization. The captured physical
access-byte address is independent of later table-register changes.

Intel PRM [11.1](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=189)
specifies an indivisible locked access-byte update. Byte-sized RMW after the
descriptor reads is our functional transaction policy, not measured pin timing.
The locked read preserves the current byte's other bits. It does not make
concurrent replacement of an entire descriptor transactional; the caller must
serialize the same instruction and must not nest an already-owned bus lock.

All non-OK statuses release the lock, preserve the destination and consume the
plan, preventing replay after possible endpoint effects. Only successful
transfers add waits. Missing lock support refuses before accessing the byte.
Tests cover 1,024 combinations of byte contents, initial A and alignment,
physical wrap, five errors before/after read/write, no replay, and no-write
null/LDTR loads. PE dispatch, SS shadow and fault delivery remain pending.

## Common load-path integration

MOV-to-ES/SS/DS, POP ES/SS/DS and LDS/LES now call the same private
`bm_286_load_segment_state` path. Real mode retains its previous cache values
and never invokes descriptor callbacks. Protected state uses preparation and
locked writeback/commit. LDS/LES general-register and POP stack-pointer changes
remain after successful segment loading; SS shadow stays instruction-owned.
No new public API or protected-step bypass exists.

The public PE gate remains before fetch, and the instruction execution helper
also still refuses PE. Tests exercise protected architectural states through
the common load path, not protected opcodes. Guest rejection metadata must not
be delivered through the existing real-mode frame. LLDT is supported by the
private state load path but its opcode dispatcher remains pending. Block 3
is therefore not complete as a protected instruction feature.

Added tests sweep all selectors for three real-mode destinations (196,608
loads), test protected read/write failures before/after effects for each target,
and preserve SP/IP/registers/shadow while checking cache commit. Existing real
MOV/POP/LDS/LES and LOCK suites exercise the actual connected instruction path.

## Next: handoff C access protection and D dispatcher integration

Protect complete CS fetches, DS/ES data operands and SS stack ranges, including
null caches, permissions, privileges, expand-down and multibyte/REP boundaries.
Connect the resulting guest metadata to the private delivery coordinator while
preserving instruction unwind, accepted signals, INTA and host-stop ownership.
Complete LLDT/system/query and privilege checks, then demonstrate a synthetic
program entering PE, faulting, repairing and returning with IRQ/NMI/TF tests.
Do not remove either public/internal PE barrier before that joint gate passes.
Task/privilege transitions remain separate E/F work; no direct-RAM bypass,
automatic transport replay or timing invention is permitted.

## Evidence and completion

Use authored synthetic programs first: construct a GDT, request PE, far jump,
load segments, write an observable result and recover from a deliberate fault.
Then extend to interrupts, privilege transitions and tasks. Firmware boot is a
later integration check, not a substitute for these tests. Continue optional
real-mode SingleStepTests; they do not certify protected behavior.

Primary reference: Intel, *80286 and 80287 Programmer's Reference Manual*,
1987, 210498-005: chapters 6/7 (selectors/descriptors), 9 (interrupts),
11 (protection), appendix B (instruction/type tables), appendix C (386 differences).

- [Intel PRM scan](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf)
- [Descriptor layout, section 6.5](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=118)
- [Table bounds](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=131)
- [Expand-down segment rules](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=190)
- [286/386 descriptor distinctions](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=335)

Reused CPU implementation retains its existing authors and derived-rewrite
provenance. The new helpers have separate provenance; this is not a claim that
the complete emulator is independent of its historical source.
