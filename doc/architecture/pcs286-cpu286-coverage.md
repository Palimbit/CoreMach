# PCS 286 portable Intel 80286: coverage and implementation decision

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Latest tranche: real-mode implicit stack faults

Supported PUSH/POP, PUSHF/POPF, near/far calls and returns, IRET,
PUSHA/POPA and ENTER/LEAVE now unwind valid-stack limit failures to #13.
An unusable six-byte exception frame enters guest shutdown without recursive
delivery or fabricated successful execution. Invalid imported SS caches and
host endpoint errors remain explicit errors, not guest exceptions.

Intel documents [PUSH SP=1 shutdown](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=297),
[PUSHA SP=1/3/5 shutdown and SP=7/9/11/13/15 #13](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=298),
and [POP](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=294) /
[POPA](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=295)
word-overrun #13. Existing whole-operation preflight is retained, including
POPA's discarded slot. Preflight order, unchanged registers/no partial pushes
on shutdown and short imported segment limits are functional model policies,
not measured silicon behavior. PUSH/CALL memory sources can be read before
discovering a stack fault. No new physical timing or protected-fault claim.

Tests cover 19 prefixed stack forms, every endpoint failing before/after
external effects, the documented PUSHA odd-SP cases, short limits and invalid
caches, NMI failure on an unusable recovery stack, INTA lock release and no
repeat acknowledge after shutdown. A guest LEAVE fault handler repairs BP,
IRETs and retries successfully before HLT. Faulting POP SS and IRET do not
prematurely reload SS or unblock NMI. The SS-load suite compares architectural
fields instead of unspecified structure padding (exposed by GCC Release).

GCC UCRT64 and MSVC Debug/Release pass 105 ordinary tests, with the two
existing Headland/AT DMA skips. The unchanged optional SST subset, 30 Python
tests, catalogue and provenance checks also pass.

Fetch/branch-target faults, strings, multiword pointer faults, protected-mode
delivery and timing remain unfinished. The machine is not declared bootable.

## Previous tranche: restartable scalar operand-limit faults

Scalar ModR/M memory operands, accumulator moffs and XLAT now request real-mode
#13 when a valid segment cache cannot contain their access. A private decode
marker unwinds the instruction before fault delivery; a host status alone
never requests an exception. The rejected operand is not read or written,
register results are not committed, and explicit/implicit instruction LOCK
does not leak into the exception frame. Invalid imported caches still refuse.

This applies to existing scalar MOV/segment loads, ALU, shifts, MUL/DIV,
XCHG and the scalar operands of supported stack/control instructions. It does
not automatically enable faults on implicit stack accesses, fetch, strings,
or multiword-pointer preflights. POP memory can read its stack source before
discovering a destination limit violation; no claim of zero earlier reads or
silicon access precedence is made. Full stack/compound restart rules remain
the next block, not a recursive exception from the generic bus accessor.

Source: Intel 80286 PRM [5.2, segment overrun](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=107):
restart at the first instruction byte and no real-mode error-code word.
Short imported segment limits are functional conformance inputs, not an
assertion that a normal real-mode segment has a non-FFFF limit.

Authored tests add 168 scalar opcode/segment/stack-alignment combinations
across 21 encodings, every transfer failing before/after its external effects,
and an entirely guest-driven address repair with IRET/retried store/HLT.
Existing scalar negative tests now assert #13 rather than unsupported; the
multiplication fixture permits writes only while testing its exception frame.
LOCK rejection before an invalid operand leaves frame transfers unlocked.
Four GCC UCRT64/MSVC Debug/Release configurations pass 105 ordinary tests,
with the same two Headland/AT DMA skips, existing SST selection and 30 Python
tests. Catalogue/provenance remain green. No physical timing or boot claim.

## Previous tranche: real-mode IVT-limit escalation and shutdown

An interrupt vector outside IDTR.limit now attempts real-mode exception 8.
Its frame saves the first instruction byte (including prefixes), even when
the original request was INT, and contains no error-code word. If vector 8
also lies outside the IVT, the core enters guest shutdown, signals its optional
shutdown callback once and releases bus exclusion. Entry returns OK with a
SHUTDOWN boundary and no delivered vector; subsequent steps return IDLE.
INTR does not wake shutdown. HOLD remains serviceable.

Real-mode NMI can leave shutdown when its vector and existing frame path work;
successful entry deasserts the shutdown output and blocks further NMI until
IRET. An out-of-limit NMI during shutdown leaves the CPU shut down and NMI
blocked until reset. Reset also clears the shutdown output. Host bus errors
are never converted to #8 or guest shutdown: they stop the diagnostic runner
without replaying transfers, retaining completed endpoint writes.

Sources: Intel 80286 PRM [5.2 / real-mode exception 8](https://manualsdump.com/en/manuals/intel-80287model-80286model/110730/107),
[9.6.2 / failed delivery and NMI recovery](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=172)
and [11.6 / shutdown signals](https://manualsdump.com/en/manuals/intel-80287model-80286model/110730/195).
This implements the bounded **real-mode IVT-limit** rule, not the protected
double-fault combination matrix. Register retention on shutdown, preflight
ordering and combined lock/signal edges are functional policy, not silicon
captures. Unusable stack frames and protected-mode NMI remain unsupported.

Authored `cpu-table-faults` tests cover 262,144 vector/limit combinations,
guest LIDT repair followed by IRET/retry, #13-to-#8 delivery, sampled-trap
shutdown, NMI/reset/HOLD/INTR behavior, even/odd frames, and failures before
or after each transfer. Existing tests now assert delivered exceptions or
shutdown instead of the old unsupported result; they were not removed.
All four GCC UCRT64/MSVC Debug/Release configurations pass 105 ordinary tests,
with the same two explicit Headland/AT DMA skips. Existing SST and 30 Python
tests pass; provenance covers 36 components/201 files and catalogue checks pass.

Next: general real-mode memory/stack faults and their architectural restart
boundaries. Invalid-opcode coverage, protected execution, untrapped ESC and
physical timing remain unfinished; this does not make the PCS286 bootable.

## Previous tranche: real-mode descriptor-table registers

SGDT/SIDT and LGDT/LIDT (`0F 01 /0..3`) now store/load instance-owned GDTR
and IDTR in real mode. The six-byte operand contains a 16-bit limit and a
24-bit base. Loads ignore the sixth byte; stores retain inherited FF readback
for it. FLAGS and segment caches are unchanged. Loads commit only after all
reads succeed; failed stores retain completed external writes without replay.
No ABI change, descriptor lookup or protected-mode execution is introduced.

Primary references: Intel 80286 PRM [B-65, LGDT/LIDT](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=275)
and [B-101, SGDT/SIDT](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=311).
The latter calls the last stored byte undefined; FF is compatibility policy,
also described for the 286 in Intel's
[80386 System Software Writer's Guide](https://www.bitsavers.org/components/intel/80386/231499-001_80386_System_Software_Writers_Guide_1987.pdf).
Inherited logic was reviewed in pinned `x86_ops_pmode.h`; its globals, 386
branches and timing constants were not imported.

Register operands deliver restartable #6. The complete six-byte memory
operand is preflighted against the segment limit and overruns deliver #13,
without an error word. Whole-operand preflight and three ascending word
transactions are functional policy, not measured silicon bus/fault ordering.
Odd words use the existing byte-fragment path. Invalid imported caches,
unsupported prefixes and unimplemented groups remain explicit gaps.

Authored tests extend `cpu-extension`: 80 group/address/override/alignment
combinations, every transfer failing before/after external effects, 32 invalid
register forms, and 28 end-of-segment cases including the last valid operands.
A guest-only LIDT/INT/IRET/HLT program proves that the relocated IDTR is actually
used, and reset restores the real-mode IVT. These are not hardware-derived
SST cases and do not establish timing or complete exception escalation.

Next: general real-mode fault delivery and escalation before protected-mode
execution. The existing unknown-timing gate remains; loading an IDTR whose
limit is too small does not implement the subsequent #8/shutdown path.

## Previous tranche: real-mode MSW system instructions

`0F 01 /4` SMSW writes the 16-bit MSW to a register or word memory operand;
`0F 01 /6` LMSW loads its low four control bits; `0F 06` CLTS clears TS.
FLAGS and unrelated architectural state are preserved. LMSW retains reserved-one
286 readback and cannot clear PE; the current real-mode entry can set PE, but
the next protected boundary refuses before fetch. This does **not** implement
protected instructions, descriptor checks or a runnable protected-mode transition.

Sources: [Intel LMSW](https://manualsdump.com/en/manuals/intel-80287model-80286model/110730/277)
and [MSW control](https://manualsdump.com/en/manuals/intel-80287model-80286model/110730/182),
checked against pinned inherited `x86_ops_pmode.h` and `x86_ops_misc.h`.
No 386 CR0 branches or timing constants are ported. Real-mode memory overruns
deliver #13 without error code; invalid imported caches and unimplemented
groups/prefix combinations remain gaps. Failed memory transfers do not commit
MSW/registers or replay partial writes. Preflight order remains functional policy.

Tests extend `cpu-extension`: 2,048 register/control cases, all 65,536 LMSW AX
inputs, eight CLTS controls, 40 addressing/alignment/override cases and each
transfer failure before/after effects. A guest-only program loads MP+TS, faults
on WAIT, executes CLTS in its #7 handler, IRETs, retries WAIT and reads SMSW
before HLT. Additional tests cover #13, reset and PE-entry refusal.
This is authored functional evidence, not a timing or physical-bus capture.
All four GCC UCRT64/MSVC Debug/Release suites pass 104 ordinary tests, with
two explicit Headland/DMA skips; the existing SST selection and 30 Python
checks pass. Catalogue/provenance remain green (36 components/200 files).

Next system block: descriptor-table register instructions; general fault
escalation, protected execution and timing remain separate unfinished work.

## Previous tranche: BOUND overrun and processor-extension fault gates

BOUND now delivers real-mode vector 13 when its complete memory pair exceeds
the segment limit, with first-prefix return IP and no error-code word.
An invalid imported segment cache still stops as unsupported. General data,
fetch, stack and instruction-length #13 paths are not implemented by this change.

The current CPU interface is explicitly **unpopulated** (BUSY/ERROR inactive).
WAIT delivers #7 when MP and TS are both set; otherwise it completes without
changing data registers or FLAGS. ESC D8..DF delivers #7 when EM or TS is set.
Untrapped ESC still stops unsupported: no handshake, stores or x87 results are
invented. This is not complete absence-detection support or an 80287 emulator.
MSW is imported by the tests; guest LMSW/SMSW/CLTS remain pending.

Sources: Intel PRM [5-7](https://manualsdump.com/en/manuals/intel-80287model-80286model/110730/107)
and [10-4](https://manualsdump.com/en/manuals/intel-80287model-80286model/110730/182),
plus BOUND B-22 cited below. Logical ESC #7 delivery occurs after opcode fetch,
before fetching ModRM/displacement or touching extension operands; this is an
explicit functional decode policy, not a physical-prefetch or fault-priority
capture. Existing SS-deferred-trap clearing policy applies. LOCK/REP forms,
protected delivery, bad-frame/IVT escalation and shutdown remain pending.

Tests add 144 WAIT/ESC/MSW/prefix combinations, #7 frame/retry/TF checks,
inhibited INTR, bad-frame/IVT refusals, and each-transfer failures before/after
effects on even/odd stacks. BOUND extends its negative tests to delivered #13,
tests all three offending offsets, retry after EA repair and frame failures.
No rollback or retry of completed writes. Timing remains UNKNOWN.
GCC UCRT64/MSVC Debug/Release pass 104 ordinary tests with two Headland/DMA
skips; existing optional SST selection, 30 Python checks, catalogue and
provenance (36 components/200 files) pass. No firmware or media executed.

## Previous tranche: real-mode BOUND and bounded fault delivery

`62 /r` reads two consecutive signed word bounds and checks its word register
inclusively. Out-of-range (including inverted bounds) delivers vector 5;
ModRM register second operands deliver vector 6. FLAGS/registers are unchanged
on an in-range result. Fault entry saves the first prefix IP, FLAGS and CS,
clears IF/TF, and does not acknowledge the PIC. IRET can retry after a handler
repairs the range. Existing DIV/AAM vector 0 uses the same private fault helper.

The [Intel PRM, B-22](https://manualsdump.com/en/manuals/intel-80287model-80286model/110730/232)
documents signed inclusive bounds, vector 5, invalid-register vector 6 and
vector 13 for offsets FFFD..FFFF. The last condition remains **unsupported**,
not a delivered guest fault: the four-byte pair is preflighted without wrapping.
Protected mode, LOCK/REP BOUND, general #UD/#GP/#SS/#DF and shutdown escalation
are not implemented by this tranche. No general unsupported opcode is turned
into #UD. Bus failures still stop the emulator without replaying partial effects.
Deferred SS-trap clearing on a fault and preflight ordering remain functional
policies, not physical-chip precedence evidence. Timing stays UNKNOWN.

Authored tests cover 2,744 register/range combinations (SP uses safe stack
values), every 16-bit AX index against [-1,1], 20 address/segment/alignment
combinations, the FFFC pair, all 64 invalid register encodings, prefix restart,
IRET/TF, inhibited events and failure before/after every transfer of four fault
forms. They do not replace silicon captures. GCC UCRT64 and MSVC Debug/Release
pass 103 ordinary tests with two explicit Headland/AT DMA skips. The existing
optional SST selection is unchanged. No BIOS, POST or machine-availability claim.

Next: complete real-mode fault/absent-80287 handling, then system instructions,
protected execution and a separately evidenced timing model.

## Previous tranche: bounded LOCK REP transfers

Real-mode LOCK MOVS, INS and OUTS (byte/word) now work with no repeat prefix
or F2/F3. A repeated transfer retains private exclusion between diagnostic
steps, without refetching prefixes or yielding ownership to HOLD. CX=0
performs no data/I/O access and acquires no window. Memory and port fragments
carry LOCKED. Completion or failure releases the window; completed external
effects are never rolled back or retried.

Accepted INTR/NMI/TF suspends the repetition and releases its window before
handler entry. INTR then acquires its own existing acknowledgement window.
IRET redecodes the original prefix using committed CX/SI/DI. This combined
event/pin ordering is an explicit functional policy pending hardware traces,
not a claim about every stepping's physical LOCK edges. F2 compatibility and
STI/SS shadow policies remain those of the existing REP implementation.

Reset, destruction, valid architectural import and strict-clock refusal also
release an active window. Invalid calls/import leave it unchanged. Reset
releases after resetting CPU state so an arbiter's newly asserted HOLD is not
erased. Adapters must outlive the CPU. Host pauses/zero execution budgets
retain the window; they are not guest instruction completion.

[Intel's 286 hardware reference](https://www.bitsavers.org/components/intel/80286/210760-002_80286_Hardware_Reference_Manual_1987.pdf)
identifies locked MOVS/INS/OUTS and count-dependent HOLD latency.
[Intel's REP restart note](https://www.pcjs.org/documents/manuals/intel/80286/rep_restart/)
documents restart after external interrupts, but does not by itself certify
the combined LOCK/event pin sequence. Sources were consulted as indexed
extracts/transcriptions; no restricted asset enters Git.

The actual CPU/AT fixture covers 4,320 opcode/prefix-order/repeat/segment/
alignment/port/DF/count configurations, all six forms interrupted by
INTR/NMI/TF or direct HOLD, first/later-iteration failures before/after effects,
I/O wrap, segment-limit stops, zero count with invalid unused segments,
import/reset/destroy/strict-clock cleanup, pending-request reset and a complete
65,535-element transfer. Existing non-LOCK tests and SST selection remain.
Four local compiler/configuration suites pass 102 ordinary tests with two
Headland/AT DMA skips, plus 30 Python checks and catalogue/provenance checks.
No ABI layout, scheduler, other CPU or AT implementation changed.

Still outside this tranche: LOCK on other string/register-only forms,
protected descriptor windows, complete guest faults and physical timing.
Timing remains UNKNOWN and strict-clock execution refuses. No BIOS/POST claim.
Next useful CPU block: real-mode exceptions/BOUND and the absent-80287 path;
protected automatic windows should accompany protected execution.

## Previous tranche: automatic real-mode INTR exclusion

Accepted INTR now requires the existing bus-lock adapter before either INTA
callback. The logical exclusion window spans both acknowledgements and the
first stack word; remaining frame writes and IVT reads are outside it.
This follows Intel's B-2/later clarification in the
[1984 information sheet](https://www.pcjs.org/documents/manuals/intel/80286/b2_b3_info/).
Early-stepping behavior is not modeled. Odd fragments of the first word remain
together as a logical-word policy, not certified physical pin timing.

An absent adapter refuses before PIC or memory side effects. Any acknowledge,
preflight or memory failure releases exclusion, latches the CPU stopped and
never retries irreversible effects. A requester becoming pending during INTA
cannot access until release and the subsequent HOLD/HLDA boundary. NMI, sampled
TF and software INT do not acquire this INTA-specific window.

The actual CPU/AT fixture tests aligned/odd stacks, both INTA phases, the
second-phase vector, exact LOCKED coverage, release before remaining frame
writes, contention and failed acknowledgements/each memory transfer. Memory
failures are injected before and after effects; missing adapters and invalid
stack/IDT preflight are checked. Existing CPU/PIC/AT integration now connects
the real arbiter; single-master unit fixtures explicitly track lock pins.

GCC UCRT64/MSVC Debug/Release pass 102 ordinary tests with two device skips;
GCC Debug also runs the unchanged SST selection. Thirty Python checks,
catalogue and provenance pass. No ABI layout, scheduler or other CPU changed.
Timing remains UNKNOWN; no BIOS/POST claim. LOCK REP and protected automatic
windows remain pending. This does not claim complete LOCK support.

## Previous tranche: locked memory MOV and shifts/rotates

F0 now supports real-mode memory MOV 88-8C/8E, A0-A3 and C6-C7,
including segment-register transfers, and documented memory Group 2 operations
C0/C1/D0-D3. Register-only forms and undocumented /6 remain host unsupported;
this is not a claim that the 286 raises #UD for every unimplemented LOCK form.
LOCK REP and automatic interrupt/descriptor windows still need separate work.

Acquisition moved from the operand-read helper to validated memory access.
This covers write-only MOV without inserting a destination read. Instruction
bytes are fetched before acquisition; all odd-word fragments remain locked
through architectural commit. Existing zero-count shifts still read but do not
write, preserving flags: inherited functional policy, not physical bus evidence.
Errors stop and release the bus, preserving any completed writes without retry.
No ABI, scheduler, other CPU or AT arbiter implementation changed.

The scope is supported by [Intel's 286 hardware reference, table 3-5](https://www.bitsavers.org/components/intel/80286/210760-002_80286_Hardware_Reference_Manual_1987.pdf),
which includes locked MOV and memory rotates. The [286 PRM compatibility appendix](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=336)
also distinguishes its wider LOCK scope from the 386. These are indexed manual
extracts, not physical captures. No table timings were adopted. Existing pinned
MOV/segment/shift provenance and author notices are retained.

The real CPU/AT fixture adds 450 locked/unlocked MOV comparisons and 21,504
shift comparisons (all 256 raw counts, seven documented groups, six opcode
forms, both alignments and rotating segment overrides). It checks exclusion of
a competing synthetic requester, LOCKED attributes, balanced pins, release
after commit, no spurious MOV reads, masked-zero no-write, IRQ deferral and
invalid-segment refusal. Eleven forms now inject failure at every transfer,
both before and after effects, including write-only MOV and zero-count shifts.
Existing independent arithmetic/data-transfer tests remain active.

GCC UCRT64/MSVC Debug/Release pass 102 ordinary tests with the two explicit
Headland/AT DMA skips. GCC Debug also passes the unchanged selected SST corpus;
these additions are authored tests, not new SST certification. Thirty Python
checks, catalogue and provenance pass (36 components / 198 files).
Timing remains UNKNOWN. No firmware, BIOS/POST, or complete-machine claim.

## Previous tranche: memory XCHG and bounded LOCK RMW

Memory XCHG 86/87 now asserts a private bus-lock window automatically, including
odd-word fragments. Explicit F0 is supported for memory-destination
ADD/OR/ADC/SBB/AND/SUB/XOR (register/immediate sources), INC/DEC/NOT/NEG and XCHG.
This is a bounded implementation, **not the 386 legal-LOCK table applied to a
286**. Other 286 LOCK forms, particularly MOV, shifts/rotates, repeated strings
and automatic interrupt/descriptor locking, remain pending. Unsupported
combinations produce a host stop, not invented guest #UD.

The existing `bus_lock` callback must be supplied for memory XCHG or LOCK RMW;
NULL refuses before operand access. A synchronous adapter must establish the
exclusion window, not merely observe it. LOCK is asserted before the first
operand read, remains active through all writes and architectural commit, and
is released before the boundary trace. Operand transactions carry LOCKED;
instruction fetches do not. Host failures latch a stop and release LOCK while
preserving already-completed writes; no rollback/retry. Unsupported segment
limits are preflighted without asserting LOCK, not delivered as guest faults.
Pin callbacks may signal events, but may not re-enter reset/import/execution.

Evidence: [Intel 80286 PRM, section 3.1.1](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=57)
documents automatic memory-XCHG locking; [Intel hardware reference, table 3-5](https://www.bitsavers.org/components/intel/80286/210760-002_80286_Hardware_Reference_Manual_1987.pdf)
also lists locked MOV, rotates and repeated transfers, establishing why the
broader 286 scope cannot be rejected as illegal. No table timing is adopted.
Pinned inherited `x86_ops_xchg.h`, `x86_ops_misc.h` and the existing arithmetic
handlers were consulted at `87c3fb4876eaad086921bc3444569da026286c36`.
The new XCHG source path is recorded as derived-rewrite; author notices remain.

`cpu286_lock.c` connects the actual CPU and AT arbiter with authored RAM.
320 XCHG cases cover every byte/word register, source overrides, alignment and
implicit/explicit LOCK; competing DMA-requester reads are refused during the
window and permitted only after release/HOLD/HLDA. This is a synthetic master,
not a DMA chip implementation. Another 430 cases compare supported locked RMW
forms with their already-oracle-tested unlocked forms. Tests verify exact
LOCKED attributes, unmarked fetches, balanced pin edges, commit before release,
IRQ deferral, segment/refused-form preflight and every transfer failure before
and after effects for odd-word XCHG/ADD/NOT. Reset recovers without a leaked lock.

GCC UCRT64/MSVC Debug/Release pass 102 ordinary tests, with two Headland/AT DMA
skips. GCC Debug adds the unchanged optional SST selection, not new lock/bus
capture evidence. Thirty Python tests, catalogue and provenance pass (36
components / 198 files). No public ABI, AT arbitration code, scheduler or
other CPU changed. Physical timing remains UNKNOWN; no BIOS/POST claim.

Next: complete the remaining 286-specific LOCK scopes, especially LOCK REP and
automatic interrupt locking, before presenting LOCK as fully implemented.

## Previous tranche: real-mode string I/O

INSB/INSW and OUTSB/OUTSW (6C-6F) now transfer one element per step, with
optional F2/F3 count-only repetition through the existing continuation path.
INS uses fixed ES:DI; OUTS uses DS:SI or the selected segment override. DX
remains the port throughout the block. DF controls signed index movement;
FLAGS, AX and the unused index remain unchanged. Without REP a single element
executes even at CX=0, leaving CX unchanged. Repeated CX=0 accesses no data
or ports and does not check unused data segments.

Scalar IN/OUT and string I/O share one private logical port-transfer helper:
aligned words use one word transaction, odd words two byte transactions, with
16-bit port wrap at FFFF. OUTS reads the full memory operand before output;
INS consumes the full input before writing memory. Registers and CX commit
only after all transfers succeed. A host error can leave input consumed,
partial output or partial memory written; the CPU stops and never retries
those effects. Tests cover callbacks failing both before and after an effect.

Invalid data segments/word limits are refused before any port access. This
preflight is an explicit functional policy, not guest #GP/#SS delivery or a
claim about 286 fault precedence. The inherited early-index/count updates on
guest abort are not used to disguise missing guest exceptions. Repeated
iterations reuse the existing interrupt/HOLD/TF and import/reset contracts.
The previous F2 compatibility and STI/SS shadow policies remain unchanged.

Evidence: Intel 210498-005 chapter 4 and Appendix B INS/OUTS entries,
including [OUTS B-82](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf)
and the [chapter 4 text mirror](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=92).
The [Intel 386 INS](https://pdos.csail.mit.edu/6.828/2018/readings/i386/INS.htm)
and [OUTS](https://pdos.csail.mit.edu/6.828/2018/readings/i386/OUTS.htm)
descriptions corroborate shared 16-bit semantics, not 286 timing or privileges.
Pinned inherited `x86_ops_io.h` and `x86_ops_rep_286_2386.h` were consulted at
`87c3fb4876eaad086921bc3444569da026286c36`; derived-rewrite provenance and
authors are preserved. No hardware timing constants were transplanted.

`cpu286_string_io.c` checks 2,880 opcode/repeat/segment/DF/memory-alignment/
port/count combinations, exact ordered transactions, per-element input/output
values, unused invalid segments, first/later failures at every transfer before
and after effects, INTR/NMI/TF/IRET, mid-element HOLD, segment/offset and 24-bit
physical wrap. Scalar I/O, memory-string and REP regression suites remain active.
The first compilation caught an unspaced hexadecimal literal ending in E next
to a plus sign in the new test; corrected the token, with no production change.

Validation: GCC UCRT64 and MSVC Debug/Release each pass 101 ordinary tests;
Headland and AT DMA remain two explicit skips. GCC Debug adds the unchanged
optional SST selection, which does not validate string I/O. Thirty Python
tests, catalogue and provenance (36 components / 197 files) pass. No ABI,
scheduler, GUI or other guest CPU changes. Timing remains UNKNOWN; strict
clocked execution still rejects before fetch. No BIOS/POST or full-286 claim.

Next CPU block: memory XCHG and LOCK, with explicit bus ownership/atomicity.

## Previous tranche: interruptible real-mode memory repetition

F3 REP/REPE and F2 REPNE now repeat MOVS/STOS/LODS/CMPS/SCAS byte/word
forms, with at most one element per diagnostic step. CX=0 advances past the
instruction without checking data segments or changing indices/FLAGS. Otherwise
CX decreases after each completed element. CMPS/SCAS evaluate the resulting ZF,
not incoming ZF: F3 continues while equal, F2 while unequal, and both stop at
zero CX. F2 on non-comparison strings is accepted as count-only compatibility
policy; redundant repeat/segment prefixes select the last within the existing
ten-byte bound. No ignored-REP policy for unrelated opcodes is introduced.

Incomplete repetitions report `BM_286_BOUNDARY_REP_ITERATION` and retain the
first prefix IP; final and zero-count boundaries report INSTRUCTION. Private
per-instance decode survives uninterrupted iterations and HOLD, avoiding opcode
refetch on each element. Accepted INTR/NMI/#1, reset or successful architectural
state import discard it. IRET re-decodes current instruction bytes and continues
with remaining CX/SI/DI, without replaying completed elements. This is a bounded
functional continuation, not a physical prefetch queue or cycle-exact snapshot.

TF can trap after each completed iteration. Incoming STI/SS-load shadows are
consumed by the first completed element in this functional model; combined
shadow/repetition behavior still needs dedicated hardware-capture validation.
Host failures leave the current element's architectural state unchanged, retain
completed external writes and latch a stop without retry. Earlier elements
remain committed. Guest segment faults, their stepping-specific restart quirks
and fault escalation are not simulated by successful repetition.

Evidence: Intel 210498-005 section 3.7.2, [pages 3-23](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=79)
and [3-24](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=80),
[Intel's 286 REP restart errata](https://www.pcjs.org/documents/manuals/intel/80286/rep_restart/),
and [IBM's hardware interface reference, repeated MOVS single-stepping](https://www.ardent-tool.com/docs/pdf/ibm_hitrc13.pdf).
Pinned inherited `src/cpu/x86_ops_rep_286_2386.h` was consulted at
`87c3fb4876eaad086921bc3444569da026286c36`; authors remain credited under
derived-rewrite provenance. No inherited globals, cycle constants or abort
workarounds are embedded in the portable core.

Authored `cpu286_repeat.c` covers 5,000 opcode/prefix/DF/count/termination
combinations, all 65,536 initial CX values, a full 65,535-element run, overlap,
independent decode state in two instances, INTR/NMI/TF entry/IRET, code changes
across interruption/import, HOLD during an element, shadow boundaries, length
and segment limits, and every endpoint failure in first/later iterations.
The initial descending-byte matrix accidentally initialized two bytes per
element; fixed the fixture width without weakening expected termination.
Existing REP rejection tests now reject still-unimplemented string I/O or
non-string combinations rather than asserting supported memory strings fail.

GCC UCRT64 and MSVC Debug/Release pass 100 ordinary tests, with two explicit
Headland/AT DMA skips. GCC Debug also passes the unchanged optional SST
regression, which still excludes strings/REP. Thirty Python checks, catalogue
and provenance (36 components / 196 files) pass. No public ABI, scheduler,
timing policy or other CPU changed; strict clocked execution still rejects
UNKNOWN timing before fetch. No physical REP capture or BIOS/POST claim.

Next: INS/OUTS, including repeated I/O and irreversible endpoint side effects;
then memory XCHG/LOCK and the other explicit CPU/motherboard gates below.

## Previous tranche: unprefixed real-mode string elements

MOVS/STOS/LODS/CMPS/SCAS byte and word forms now execute exactly one element,
including when CX is zero. CX is unchanged. Source is DS:SI or the selected
segment override; destination is always ES:DI. STOS/SCAS ignore source segment
overrides and LODS does not access ES. DF controls signed index movement by
one or two with 16-bit wrap after the element; an individual word crossing
a segment limit is still rejected pending guest segment-fault delivery.

MOVS captures the complete source before writes, including overlapping words.
CMPS uses source minus destination, SCAS uses accumulator minus destination;
both reuse the defined subtraction flags without writing memory. MOVS/STOS/
LODS preserve FLAGS. LODSB preserves AH. Architectural AX/index/FLAGS changes
commit only after successful accesses. Host endpoint errors preserve registers,
stop execution and retain completed external writes without retry or rollback.
These host failures are not guest #GP/#SS, and do not emulate the inherited
286 post-abort index quirks. CMPS retains the inherited ES-before-source read
order as a functional policy, not certified electrical/fault precedence.

Sources: pinned `src/cpu/x86_ops_string.h` at
`87c3fb4876eaad086921bc3444569da026286c36`, added to derived-rewrite
provenance with authors retained; Intel instruction descriptions for
[MOVS](https://pdos.csail.mit.edu/6.828/2018/readings/i386/MOVS.htm),
[CMPS](https://pdos.csail.mit.edu/6.828/2018/readings/i386/CMPS.htm),
[SCAS](https://pdos.csail.mit.edu/6.828/2018/readings/i386/SCAS.htm),
[LODS](https://pdos.csail.mit.edu/6.828/2018/readings/i386/LODS.htm) and
[STOS](https://pdos.csail.mit.edu/6.828/2018/readings/i386/STOS.htm)
were consulted for shared 16-bit semantics only. No 386 addressing, timings,
page-fault model, globals or REP implementation is transplanted.

Authored `cpu286_strings.c` covers 3,600 opcode/segment/DF/alignment/CX/data
combinations, 3,145,728 independent comparison checks (all byte pairs and
all word values against eleven boundaries, CMPS/SCAS and two flag backgrounds),
150 transfer failures across 40 forms, offset wrap/limits, 24-bit physical
address wrap, overlapping MOVSW, last override selection, unused invalid
segments, PE/LOCK/REP refusal, TF delivery after the element and HOLD raised
mid-read without splitting the element. The first HOLD test expected OK;
it was corrected to the existing documented IDLE result without changing
production behavior. Completed-byte persistence/no replay is asserted.

GCC UCRT64 and MSVC Debug/Release pass 99 ordinary tests with two explicit
Headland/AT DMA skips. GCC Debug also passes the unchanged optional SST
regression; that selection does not include string instructions. Thirty
Python checks, catalogue and provenance (36 components / 195 files) pass.
No ABI, clocks, GUI or other CPU change; timing remains UNKNOWN and strict
clocked execution rejects before fetch. No physical 286 string capture,
REP, string I/O, guest segment fault, protected execution or BIOS/POST claim.

## Previous tranche: real-mode decimal adjustments

DAA/DAS/AAA/AAS/AAM/AAD now update AX and their defined flags. AAA/AAS
apply the 286 full-AX correction (including carry/borrow into AH), not the
8086 AL-only variant. DAA/DAS inspect the original AL and CF for the high
correction; DAS retains a borrow from its low correction. AAM/AAD consume
the immediate radix. AAD zero is valid; AAM zero delivers the same real-mode
#DE as DIV/IDIV, before changing AX or flags. A guest test patches the radix,
returns through IRET and retries from the initial segment prefix.

Undefined flags are preserved as explicit policy, not hardware evidence:
OF for DAA/DAS, OF/SF/ZF/PF for AAA/AAS, OF/AF/CF for AAM/AAD. Valid results
set SF/ZF/PF from AL where defined. Failed immediate fetch or exception entry
preserves architectural state and stops, retaining completed external writes
without replay. No clock, public contract, GUI or other guest CPU changed.

Evidence: pinned inherited `src/cpu/x86_ops_bcd.h` at
`87c3fb4876eaad086921bc3444569da026286c36` was consulted and added to
derived-rewrite provenance. Its AAM-zero-to-ten fallback is deliberately not
ported. [Intel's later instruction reference, AAM/AAD and DAA/DAS entries](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-vol-2a-manual.pdf)
clarifies generalized radix and #DE behavior. This is functional evidence,
not proof that all invalid-BCD/nondecimal edge cases have been captured from
a 286. Earlier Intel 386 pseudocode uses a different adjustment presentation;
it is not used as an exhaustive invalid-input oracle. No new manual, firmware
or third-party test data is copied into the repository.

Authored `cpu286_decimal.c` checks 1,319,424 scalar cases with separate
arithmetic/flag calculations: all AL/flag combinations for DAA/DAS; all AX/AF
values for AAA/AAS; every AL/nonzero radix for AAM; every AX in nine AAD bases
and all radices against boundary pairs. Another 40,000 valid packed-decimal
ADC/SBB followed by DAA/DAS sequences use ordinary decimal mathematics as
the oracle. Tests include all segment overrides, odd/even stack and relocated
IVT, every failing transfer, 256 AAM-zero faults, guest self-repair/IRET/TF,
and explicit PE/LOCK/REP rejection. Existing division tests remain active.

GCC UCRT64 and MSVC Debug/Release pass 98 ordinary tests with two Headland/AT
DMA skips; GCC Debug adds the unchanged optional SST regression. The selected
SST files do not cover these decimal instructions. Thirty Python checks,
catalogue and provenance (36 components / 194 files) pass. Timing is UNKNOWN;
strict clocked execution still refuses before fetch. No BIOS/POST claim.

## Previous tranche: real-mode division and divide-error delivery

DIV/IDIV F6/F7 /6,/7 now read byte/word divisors and commit quotient/remainder
only after checking for zero and out-of-range quotients. Signed division uses
int64_t, including INT32_MIN/-1 without host overflow, and truncates toward
zero. Undefined arithmetic flags are preserved as emulator policy.

A guest divide error is delivered through vector 0, with a six-byte
FLAGS/CS/IP frame, no error code and no INTA. The saved IP identifies the
first prefix, not the following instruction. The boundary reports EXCEPTION
with vector 0, distinct from a completed INT 0. IF/TF are cleared on entry;
no single-step trap is generated for the faulting instruction. An authored
guest handler repairs BX, returns with IRET and successfully retries the DIV.
TF restored by IRET causes a trap after successful retry, not before it.

References: [Intel 210498-005, sections 3.3.4, 5.2, 9.7 and Appendix B
DIV/IDIV](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf).
Indexed primary text and already pinned `x86_ops_misc.h` were consulted;
authors and derived-rewrite provenance retained. No firmware or timing copied.

Functional limits are explicit: the shared frame helper preflights stack/IDTR
and rejects unsupported #SS/#GP/#DF/shutdown cases. Host errors preserve
architectural state and latch execution off; completed stack writes are not
rolled back or retried. This is not physical fault precedence certification.
Clearing a previously SS-deferred trap on #DE is an explicit functional policy
awaiting combined-boundary hardware coverage. NMI pending/block state is not
consumed by #DE. Protected mode and LOCK/REP remain unsupported.

`cpu286_divide.c` uses independent sign/magnitude binary long division for
2,889,216 scalar checks (all byte dividends against eleven divisors, all word
divisors against eleven dividend boundaries, and every byte divisor against
boundary dividends). It also covers 32 register aliases, 96 memory/form/
segment/alignment/fault combinations, each transfer failure including frame
writes and IVT reads, relocated/odd IVT, wrapping stack, trace callback,
STI/SS shadow policies, TF and guest recovery. These are authored functional
tests, not physical captures. Earlier unsupported-DIV expectations have become
exact successful DIV checks and distinct invalid-encoding guards.

GCC UCRT64 and MSVC Debug/Release pass 97 ordinary tests, with two explicit
Headland/AT DMA skips; GCC Debug adds the unchanged SST regression. The selected
corpus excludes DIV/IDIV and the adapter still defers exception vectors.
Thirty Python checks, catalogue and provenance (36 components / 193 files)
pass. Timing remains UNKNOWN and strict clocked execution rejects before fetch.
No new board component, BIOS, POST, protected execution or general fault model.

## Previous tranche: real-mode multiply and sign conversion

MUL/IMUL F6/F7 /4,/5 now multiply byte or word operands into AX or DX:AX.
IMUL 69/6B supports every destination register and register/memory source;
the 6B immediate is sign-extended, not zero-extended. CBW 98 and CWD 99
extend AL into AX and AX into DX:AX without changing FLAGS.
CF/OF indicate unsigned upper-half overflow or signed result truncation.
Undefined multiplication S/Z/A/P flags are preserved as a deterministic
emulator policy, not measured silicon behavior.

Reference: [Intel 210498-005, sections 3.3.3 and 3.4.3, Appendix B
MUL/IMUL/CBW/CWD](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf).
Indexed primary text and pinned inherited `x86_ops_misc.h`/`x86_ops_mul.h`
were consulted; no PDF acquired, timing constants copied or clean-room claim.
The latter source is now explicitly recorded in provenance; authors retained.

Sources are captured before destination changes, including AL/AH/AX/DX and
immediate destinations that alias an address register. All fetches and data
reads must succeed before committing results or flags. No memory writes occur.
Signed values are converted mathematically into int32_t; no implementation-
defined signed narrowing or signed right shifts are needed. The largest
signed 16x16 product fits int32_t; unsigned multiplication uses uint32_t.
Immediate bytes are fetched before the data read as a functional decoder
policy, not a claim of physical bus or fault ordering.

Authored `cpu286_multiply.c` uses a separate sign/magnitude uint64 oracle:
4,327,680 scalar checks cover every byte pair, all word inputs against eleven
edge factors, both initial flag backgrounds, all immediate-word values against
nine patterns, immediate-byte sign extension and all AX values for CBW/CWD.
Additional tests cover all register destinations and aliases, all segment
overrides, BP's SS default, aligned/odd memory, every fetch/read failure,
unsupported PE/LOCK/REP, segment bounds and pending DIV/IDIV. The old MUL
unsupported test now asserts its exact result/overflow; DIV remains a negative
case. No failing behavioral test was removed or masked.

GCC UCRT64 and MSVC Debug/Release pass 96 ordinary tests, with two explicit
Headland/AT DMA skips; GCC Debug adds the unchanged SST regression. That
selection does not cover these new groups. Thirty Python checks, catalogue
and provenance (36 components / 192 files) pass. Timing is still UNKNOWN;
strict clocked execution still rejects. No BIOS, POST or protected-mode claim.

## Previous tranche: real-mode shifts and rotates

Group 2 ROL/ROR/RCL/RCR/SHL (SAL)/SHR/SAR now supports byte and word
register/memory operands, count 1, CL and immediate byte (D0-D3/C0-C1).
Counts use the 286 five-bit mask, including immediate values. CL is captured
before changing an aliased destination. Full bit-ring rotations are not
mistaken for a masked-zero count; carry reflects the final bit shifted out.
Single-count overflow and shift sign/zero/parity follow the instruction rules;
rotations preserve sign/zero/parity/auxiliary carry. Masked zero preserves all
flags and does not write the destination.

Primary reference: [Intel 210498-005, section 3.4.2 and Appendix B
RCL/RCR/ROL/ROR/SAL/SAR/SHL/SHR](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf).
Indexed primary text was consulted, not a newly acquired or fully visually
reviewed PDF. The pinned inherited `x86_ops_shift.h` was also consulted and
added to provenance. This is a derived rewrite with existing author notices.

Explicit policies, not silicon claims: preserve undefined OF for masked counts
greater than one; clear undefined AF on nonzero shifts; masked-zero memory
operands still read/check the operand, without writing it. This access policy
follows the inherited handlers, not measured bus sequencing. Undocumented /6,
LOCK/REP and protected execution remain unsupported; no fabricated guest fault.
Registers/FLAGS/IP commit only after successful transfers; a completed low byte
of an odd memory write remains visible if the second byte fails, with no retry.

`cpu286_shifts.c` uses an independent closed-form oracle (bit rings and wide
arithmetic, versus production's bounded single-bit loop): 1,899,520 scalar
cases cover every byte value/count/carry, two status backgrounds, plus nine
word boundary patterns. It also covers 3,360 opcode/register/count combinations,
1,008 memory/segment/alignment/count combinations, all 70 transfer failures
across the six encodings and two alignments, masked-zero read failure, invalid
forms and segment/fetch limits. These are authored functional tests.

GCC UCRT64 and MSVC Debug/Release pass 95 ordinary tests, with two explicit
Headland/AT DMA skips; GCC Debug also passes the unchanged SST selection.
That selection does not include Group 2. Thirty Python checks, catalogue
and provenance (36 components / 191 files) pass. No timing constants, ABI,
other CPU, machine registration or board decoding changed. Timing remains
UNKNOWN and strict clocked execution rejects before fetch; no PCS286 POST claim.

## Previous tranche: address and pointer loads

Real-mode LEA 8D, LES C4, LDS C5 and XLAT D7 are implemented. LEA computes
only the wrapped 16-bit EA, ignoring the data segment's base, validity and
limit; instruction fetch still obeys CS. Register-source Mod=3 remains an
unsupported invalid encoding, not a fabricated guest #6.

LDS/LES read offset and selector completely before replacing the destination
register and DS/ES cache. This preserves the old source when it aliases the
destination register or segment. The real-mode reload sets base=selector<<4,
limit=FFFF, with no SS-load shadow. Both words use the existing independently
wrapped offset policy, also present in the consulted classic LDS handler;
no independent hardware capture for C4/C5 at FFFE is claimed. Preflight checks
are explicit unsupported-fault policy, not silicon fault precedence.
XLAT reads one byte at the selected segment plus (BX+unsigned AL) modulo
65536, replacing only AL. FLAGS remain unchanged in all four instructions.

Primary reference: [Intel 210498-005, sections 3.7.1/3.8 and Appendix B
LEA/LDS/LES/XLAT](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf).
Indexed primary text and already pinned inherited `x86_ops_mov.h` /
`x86_ops_mov_seg.h` were consulted. This is a derived rewrite with notices
preserved, not a new clean-room implementation or new PDF acquisition.

Authored `cpu286_address_loads.c` tests 1,920 LEA EA/override/destination
cases, 384 LDS/LES destination/source/alignment cases, alias and cache reload,
1,280 XLAT index/override cases, unsigned wrap and A20, invalid Mod=3 forms,
segment/instruction limits, unsupported PE/LOCK/REP, and every fetch/read
failure in all four families. Errors leave defined CPU state unchanged and
latch execution off without retry. No writes or host-platform calls added.

GCC UCRT64 and MSVC Debug/Release pass 94 ordinary tests, with two explicit
Headland/AT DMA skips; GCC Debug also passes the unchanged selected SST
regression. The selected corpus does not include these new groups.
Thirty Python tests, catalogue and provenance (36 components / 190 files)
pass. Timings remain UNKNOWN and clocked scheduling refuses execution.
No protected segment loading, hardware timing or PCS286 boot claim follows.

## Previous tranche: real-mode far procedures

Far CALL 9A ptr16:16 and memory FF /3 save CS followed by the decoded next IP.
Both pointer words are read before stack writes, including when the source
overlaps the stack. RETF CB and CA imm16 pop IP then CS and discard the requested
number of additional bytes, without reading those bytes. FLAGS and NMI blocking
are unchanged; these are not IRET. Reloading CS discards the high reset cache,
and the CPU leaves A20 to the board. Registers commit only after all accesses
succeed; completed writes remain visible on host failure and cannot be retried.

Source references already pinned in provenance: `x86_ops_call.h`,
`x86_ops_ret_2386.h` and the existing segment reload helper. This remains a
derived rewrite preserving inherited notices. Primary instruction reference:
[Intel 210498-005, section 3.6.1 and Appendix B CALL/RET](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
including RET B-94. Indexed text was consulted, not a new PDF acquisition.

The shared FF /3,/5 pointer reader wraps between words at offset FFFE.
The prior hardware captures establish that case for FF /5, not for FF /3;
reusing that rule for CALL is an explicit functional policy awaiting independent
FF /3 capture coverage. Whole-frame preflight rejects unsupported stack faults
before writes; it is not measured fault precedence or shutdown emulation.
Protected calls/returns, task and privilege transitions remain unsupported.
No timing constant was adopted; all boundaries remain UNKNOWN and strict
clocked scheduling stays disabled.

`cpu286_far_procedures.c` covers immediate/indirect calls, all segment overrides,
BP's SS default, independently odd/even pointers and stacks, overlapping operands,
wrapping frames and return IP, high-reset CS and A20, unchanged FLAGS/NMI,
sampled TF, all 65,536 RETF immediate values, invalid forms and every transfer
failure in four forms at both alignments. Existing far-JMP/near-control tests
remain active. GCC UCRT64 and MSVC Debug/Release: 93 ordinary passes, two explicit
Headland/AT DMA skips. GCC Debug adds the unchanged pinned SST regression;
it does not contain these new groups. Thirty Python checks, catalogue and
provenance (36 components / 189 files) pass. No firmware or board boot claim.

### Remaining CPU work (not motherboard work)

- Complete broader 286 LOCK scopes, including MOV, shifts/rotates, REP and
  automatic interrupt/descriptor windows; XCHG and bounded RMW LOCK now work.
- Decimal/multiply/divide edge cases still need expanded hardware-capture
  comparison; functional instruction implementations are present.
- Memory strings and INS/OUTS now support interruptible REP. Combined
  shadow/repetition and silicon fault-restart behavior need hardware comparison.
- Guest faults beyond real-mode #DE, BOUND/invalid-opcode handling, protected segmentation, descriptors,
  privilege, tasks/gates and system instructions; correct double fault/shutdown.
- Unpopulated 80287 interface behavior (ESC/WAIT and MSW interaction), explicit
  undocumented-opcode policy, and calibrated timing/prefetch/bus behavior.

The next bounded block extends the 286-specific LOCK scopes beyond RMW.
The ordered CPU roadmap then covers remaining real-mode faults and 80287 interface,
protected segmentation/system instructions, gates/tasks, and calibrated timing.
Memory XCHG now uses the tested implicit LOCK/bus contract. Completing real-mode
instructions does not certify a full 286 or eliminate the PCS286 chipset gates.

## Previous tranche: software interrupts and FLAGS

Real-mode INT imm8, INT3 and taken INTO save the decoded next IP, CS and FLAGS,
clear IF/TF and enter the IVT handler without issuing INTA. Untaken INTO
does not touch the data bus. Software INT 2 does not acquire the hardware NMI
block. An instruction boundary carries the software vector; the hardware
interrupt/exception boundary categories are unchanged.

PUSHF/POPF use the 286 masks (not 8086 high bits or 386 real-mode privilege
rules). POPF preserves IOPL/NT and creates no STI shadow; enabling TF takes
effect for sampling the following instruction. LAHF/SAHF move only their
defined low FLAGS bits. CLC/STC/CMC and CLD/STD preserve unrelated flags.

This is a derived rewrite, consulting the additional pinned inherited paths
`src/cpu/x86_ops_flag_2386.h` and `src/cpu/x86_ops_int.h`. Original notices
remain in the implementation and both paths are recorded in provenance.
Intel 210498-005's instruction dictionary and the Intel
[B-2/B-3 information sheet, 21 November 1984](https://www.pcjs.org/documents/manuals/intel/80286/b2_b3_info/)
support the architectural behavior. The latter corrects INT's saved FLAGS
and single-step description for B-2 and later: taken software interrupts do
not newly sample a trap before their first handler instruction. This does not
claim early-stepping emulation or measured silicon behavior. Indexed primary
text was consulted, not a new visually reviewed or preserved PDF.

Authored tests cover all 256 INT vectors with IRET, INT3, both INTO paths,
65,536 POPF images, all AH values for SAHF/LAHF, odd/even and wrapping stacks,
prefixes, SS inhibition, IF/TF transitions and every transfer failure in six
instruction forms. Invalid stack/IVT and protected paths remain explicit
unsupported stops, not delivered guest faults. Registers commit only on
success; earlier external writes remain and a host failure prevents retries.

All timings remain UNKNOWN; strict clocked execution is still disabled.
GCC UCRT64 and MSVC Debug/Release pass 92 ordinary tests with two explicit
Headland/AT DMA skips. GCC Debug also passes the unchanged pinned SST subset;
that selected subset does not contain these new instruction groups and is
regression evidence only. Thirty Python tests, catalogue and provenance pass.
No BIOS, boot, complete CPU or physical timing claim follows.

## Previous tranche: real-mode interrupt roundtrip

Real-mode INTR now invokes both INTA callbacks, obtains the vector from phase
1, writes FLAGS/CS/IP at SS:SP and reads the four-byte pointer using IDTR base
and limit. A successful entry clears IF/TF and HALT, reloads CS/IP and publishes
an interrupt boundary without fetching an instruction. Sampled #1 and edge-
latched NMI use fixed vectors without INTA. Priority is sampled #1, NMI, INTR;
SS-load inhibition blocks all three, STI inhibition only INTR. NMI blocks
further NMI acceptance until IRET; a new edge during entry remains pending.

CLI, STI, HLT and real-mode IRET are implemented. IRET reads IP/CS/FLAGS from
the stack, preserves the 286 real-mode IOPL/NT restriction, clears reserved
bits and unblocks NMI. TF restored by IRET starts sampling on the following
instruction, not retroactively. The inherited return/flag algorithms were
consulted at the pinned commit below; `x86_ops_flag.h` is added to provenance.
Intel 210498-005 sections 5.2.2 and 9.7 and its instruction dictionary, plus
210760-002 table 3-2, are the primary architectural references. Online indexed
text was consulted; full PDF access failed, so no visual inspection or new
preserved-document acquisition is claimed.

All paths remain TIMING_UNKNOWN. Waits from both INTA phases and completed
bus transfers are counted once, as a lower bound only. The strict clocked
entry point still refuses execution before fetch. This is not a runnable
timed PCS286, physical interrupt-latency evidence or a complete exception model.
Guest faults, protected gates and shutdown recovery remain gaps; software
interrupts are covered by the latest tranche above.

Authored tests cover aligned/odd frames, stack wrap, relocated/odd IVT with
24-bit physical wrap, FLAGS, NMI edge/block/reentry, sampled trap priority,
STI/SS inhibition, HLT wakeup, every transfer failure in entry and IRET, both
INTA failure positions, and unsupported limits/protected entry. Register state
commits only on success. Completed external writes/acknowledgements cannot be
undone; host failures latch execution off until reset. This policy does not
claim silicon bus-abort recovery or guest fault precedence.

The actual cascaded AT PIC and 286 execute a synthetic IRQ9 roundtrip through
the slave vector, separate slave/master EOI instructions and IRET. The test
bus is not a production PCS286 map; no firmware or media is executed. Existing
SS and acceptance tests now check actual delivery or missing bus endpoints,
instead of expecting all events to be unsupported.

Validation: 90 ordinary CTest passes and two explicit absent-device skips
(Headland and AT DMA) on GCC UCRT64 and MSVC, Debug/Release. GCC Debug also
passes the pinned optional 71,000-case SST subset with unchanged classification.
Thirty Python tests, catalogue and provenance audit pass. This remains local
implementation, not a remote CI, boot or full-ISA certification.

Status: P1 partial interpreter through real-mode division/#DE, multiplication/conversion, shifts/rotates, address loads, far procedures, interrupts and FLAGS, 2026-09-23. Reviewed source base
`87c3fb4876eaad086921bc3444569da026286c36` on
`feature/pcs286-cpu286`, subsequently included unchanged in portable merge
`8e5cd917d95536fbdfe65ce2d5d658eda0633d20`. This is the Olivetti
**PCS 286**, not the TI/OLIMCU PCS 286/S. No new CPU body is present at this
base. The existing `cpu286_acceptance.c` was initially skipped because its
target did not exist. The current branch adds source and expanded tests;
coordinator-owned CMake/provenance wiring is integrated locally. No firmware or preserved media was
used in this review or the authored tests.

## Evidence and scope

The canonical `Z:/library/olivetti/pcs286` README, manifest and 2026-09-23
worklogs establish the 12 MHz Intel 80286 identity; at the initial P0 source
review no portable CPU implementation existed. Current partial implementation
results are recorded separately; they do not certify full opcode or timing behaviour. The
two local Compaq files named `80286-reference-volume-{1,2}.pdf` identify
themselves as **Compaq Portable 286/Deskpro 286 Technical Reference Guide**
volumes. They are machine references, not the CPU programming authority.

Primary CPU references:

- Intel, [*80286 and 80287 Programmer's Reference Manual* (1987), order
  210498-005](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf): chapters 3 (base instruction set), 8-10 (protection, exceptions, system control), Appendix B (formal instruction dictionary and clock columns), Appendix C (8086/8088 compatibility). Chapter 3 says undefined opcode patterns invoke interrupt 6; §9.6 covers faults and shutdown; §9.7 covers single stepping and interrupt priority; §10.4 gives reset state.
- Intel, [*80286 Hardware Reference Manual* (1987), order
  210760-002](https://bitsavers.org/components/intel/80286/210760-002_80286_Hardware_Reference_Manual_1987.pdf): chapter 3 covers bus transfers, two INTA cycles, HALT/SHUTDOWN, HOLD/HLDA and LOCK. This is bus evidence; it does not identify the PCS 286 motherboard wait-state table.
- Intel, [*iAPX 286 Operating System Writer's Guide*](https://bitsavers.org/components/intel/80286/1983_iAPX_286_Operating_System_Writers_Guide.pdf): exception handling and task/descriptor cases, including the warning that a repeated string with a segment-limit violation may not be restartable.

The classic BluMach source at the exact base above is an **inherited 86Box
implementation**, not portable validation. `src/cpu/386_ops.h:2370` has a
286 dispatch table and `:619` the 0F system table; `src/cpu/x86_ops_pmode.h`,
`src/cpu/x86seg.c` and `src/cpu/x86_ops_rep_286_2386.h` contain relevant
system, segment/task and repeat semantics. `src/cpu/386.c` dispatches this
through global CPU state, memory, PIC, timer and host integration. Its
triple-fault path calls soft reset, whereas Intel documents a 286 SHUTDOWN
state that NMI or RESET may exit. `src/cpu/cpu.c:180` and `:940` hold global
wait and timing parameters; `src/cpu/cpu_table.c:1084` has generic speed
profiles, not PCS 286 measured waits. `components/cpu/808x/src/cpu_808x.c`
is a separate per-instance derived rewrite of 808x/V30 behaviour; its ALU
techniques may inform a derived rewrite, but its segment/model architecture
cannot become a 286 by selecting a flag.

## Instruction and mode coverage matrix

`Required` means required for a reusable documented 80286 core, including
guest #6 for undefined encodings. `Later` names a separately scoped silicon
or populated-80287 feature. The current portable core implements the reset
state, NOP and bounded real-mode data-transfer, ALU, stack, control, I/O and
interrupt subsets. Each row distinguishes implemented forms from gaps. A
classic handler is a review/reuse candidate only, never a
portable pass. Every grouped opcode must classify each ModR/M subform; a
generic catch-all or NOP is not coverage.

| Family / encodings | Required semantics and representative tests | Classic candidate | Portable / timing |
|---|---|---|---|
| Reset, fetch, prefixes | CS=F000, hidden base=FF0000, IP=FFF0, MSW=FFF0, FLAGS=0002; fetch at FFFFF0; segment overrides 26/2E/36/3E, LOCK F0, REP F2/F3; prefix-inclusive fault IP and illegal combinations | `x86.c`, `386_ops.h`, prefix handlers | Reset/fetch, bounded segment/REP/F0 decode and #DE implemented; LOCK memory MOV/RMW/shifts subset only, other fault delivery missing; timing unknown |
| Data movement | MOV 88-8E/A0-A3/B0-BF/C6-C7, XCHG 86-87/90-97, LEA 8D, LDS/LES C4-C5, XLAT D7; register/memory, odd word, segment load permissions and cache | `x86_ops_mov.h`, `x86_ops_mov_seg.h`, `x86seg.c`, `x86_ops_xchg.h` | Listed real-mode MOV, LEA/LDS/LES/XLAT and register/memory XCHG implemented; memory XCHG needs bus_lock adapter; MOV SS has SS-load inhibition; protection missing, timing unknown |
| Integer ALU / flags | ADD/ADC/SUB/SBB/CMP/AND/OR/XOR, TEST, INC/DEC, NEG/NOT, Group 1 80-83 and F6/F7; byte/word carry, overflow, auxiliary carry, parity, defined/undefined flags, memory read-modify-write | `x86_ops_arith.h`, `x86_ops_inc_dec.h`, `x86_ops_misc.h`, `x86_flags.h` | Real-mode 00-3D, 80/81/83, 84/85, A8/A9, 40-4F, FE/FF /0,/1 and F6/F7 /0,/2,/3 implemented; 82 and all other groups deferred; timing unknown |
| Multiply/divide, BCD | MUL/IMUL/DIV/IDIV F6/F7, immediate IMUL 69/6B, DAA/DAS/AAA/AAS/AAM/AAD, CBW/CWD; divide #0 before destination mutation; result-dependent timing | `x86_ops_misc.h`, `x86_ops_mul.h`, `x86_ops_bcd.h` | Real-mode functional forms implemented, including #DE for division and AAM zero; general fault escalation and expanded hardware captures pending; timings unknown |
| Shifts/rotates | C0/C1/D0-D3 Groups 2; counts 0/1/>1, CF/OF, through-carry, memory alignment | `x86_ops_shift.h` | All seven documented real-mode operations implemented; undefined flag policies explicit, memory LOCK supported; /6 and register-only LOCK unsupported; timing unknown |
| Stack/procedures | PUSH/POP registers, segments, immediates and r/m; PUSHF/POPF, PUSHA/POPA, ENTER/LEAVE, near/far CALL/JMP/RET and IRET; 286 PUSH SP value, SP wrap, interlevel stacks | `x86_ops_stack.h`, `x86_ops_call.h`, `x86_ops_ret_2386.h`, `x86seg.c` | Listed real-mode stack/procedure forms implemented, including far CALL 9A/FF /3 and RET CA/CB; protected/interlevel execution and fault delivery missing; timing unknown |
| Branch and loop | Jcc 70-7F, LOOP/LOOPE/LOOPNE/JCXZ E0-E3, short/near/far jumps; taken/not-taken, prefetch flush, segment privilege/limit | `x86_ops_jump.h`, `x86seg.c` | Real-mode Jcc 70-7F, E0-E3 and JMP EB/E9/FF /4 and far JMP EA/FF /5 implemented; protected control and prefetch model missing; timing unknown |
| Strings and block I/O | MOVS/CMPS/STOS/LODS/SCAS A4-AF; INS/OUTS 6C-6F; REP/REPE/REPNE, zero count, DF, per-iteration interrupt/HOLD and restart state, segment-limit fault | `x86_ops_string.h`, `x86_ops_rep_286_2386.h`, `x86_ops_io.h` | Memory strings and INS/OUTS with interruptible REP implemented; guest segment faults and physical timing pending |
| Direct I/O and flag control | IN/OUT E4-E7/EC-EF, CLI/STI, CLD/STD, CLC/STC/CMC, LAHF/SAHF; 16-bit I/O port, CPL/IOPL checks and STI shadow | `x86_ops_io.h`, `x86_ops_flag_2386.h` | Listed real-mode forms implemented; protected privilege checks missing; timing unknown |
| Software interrupts / halt | INT3/INT/INTO/IRET CC-CF, HLT F4; real IVT and protected gates, IF/TF effects, HLT wake conditions | `x86_ops_int.h`, `x86seg.c`, `386.c` | Listed real-mode forms implemented; protected gates and fault delivery pending; timing unknown |
| 286 application extensions | BOUND 62 (#5), ARPL 63, 186-family PUSHA/POPA, immediate PUSH, IMUL, ENTER/LEAVE, INS/OUTS and count-immediate shifts | `386_ops.h`, `x86_ops_misc.h`, `x86_ops_pmode.h` | Real-mode forms implemented including BOUND #5/register #6/limit #13; ARPL pending; timing unknown |
| Protected system instructions | 0F 00 group SLDT/STR/LLDT/LTR/VERR/VERW; 0F 01 SGDT/SIDT/LGDT/LIDT/SMSW/LMSW; 0F 02/03 LAR/LSL; 0F 06 CLTS; privilege, type, present and selector tests | `x86_ops_pmode.h`, `386_ops.h` | Real-mode SGDT/SIDT/LGDT/LIDT/SMSW/LMSW/CLTS implemented; remaining system forms and all protected checks missing; timing unknown |
| Undefined / undocumented | Reserved primary, 0F and ModR/M forms must deliver #6 per Intel's documented map. Classic 0F 05 LOADALL, F1 alias and D6 SETALC are separate undocumented silicon candidates, not documented-required success | `386_ops.h`, `x86_ops_misc.h` | Missing; undocumented deferred pending silicon evidence |
| Unpopulated 80287 interface | ESC D8-DF and WAIT 9B with MSW EM/MP/TS: required #7/no-coprocessor behaviour; no fabricated 80287 arithmetic. Populated BUSY/ERROR/PEREQ/PEACK and #9/#16 are later | `x86_ops_fpu_2386.h` | WAIT completion with inactive BUSY/ERROR and documented #7 gates implemented; untrapped ESC and populated interface pending; timing unknown |

## Protection, interrupt and fault matrix

These are independent checks, not consequences of decoding the opcodes above.
Defined reset, real-mode caches, HOLD and bounded real-mode hardware/software
interrupt entry/return are implemented as described above. Protected execution
and general guest fault delivery remain missing; real-mode #DE and BOUND
#5/register-operand #6/limit #13 and extension #7 are implemented. The table lists required coverage,
not a claim that every case is implemented; it follows Intel manual
chapters 8-10; exact frame/error-code and restart IP must be asserted through
authored guest programs and bus traces.

| Area / vectors | Required cases and tests | Classic candidate / caution |
|---|---|---|
| Real mode / reset | 24-bit physical translation, A20 left to board, high reset CS cache, normal real-mode segment reload, IVT base/limit, word at segment end, cold and CPU-only reset | `x86.c`, `x86seg.c`; classic reset also manipulates board and peripherals |
| Descriptor cache / limits | GDT/LDT/IDT bounds, 286 8-byte descriptors, 24-bit bases/16-bit limits, code execute/read and data read/write/expand-down, null selectors, accessed/busy bits, hidden-cache survival after descriptor memory changes | `x86seg.c`, `x86_ops_pmode.h`; 386 width/granularity branches must be excluded |
| Privilege / control transfer | CPL/RPL/DPL/IOPL, conforming vs nonconforming code, stack-switch rules, call/interrupt/trap/task gates, parameter copy, far CALL/JMP/RET and IRET, nested task flag/backlink, TSS save/load | `x86seg.c`; need independently authored tables |
| Fault vectors 0, 5-8 | divide #0, BOUND #5, invalid opcode/operand #6, unavailable processor extension #7, double fault #8; no NOP replacement for undefined opcode | `386_common.c`, `x86seg.c`; classic #8 fallback may reset |
| Protection fault vectors 10-13 | invalid TSS #10, segment not present #11, stack #12, general protection #13; selector/IDT error-code fields, privilege and limit precedence, restart IP, side effects before fault | `x86seg.c`; do not copy 386-specific error precedence blindly |
| Interrupts 1-4 / hardware | TF single step #1, NMI #2 edge/latch and IRET unblock, breakpoint #3, overflow #4, INTR level plus two actual INTA callbacks, priority and STI/MOV SS/POP SS shadows | `386.c`, `x86_ops_int.h`; PIC remains external |
| Shutdown / halt / HOLD | An exception while invoking #8 enters SHUTDOWN; only NMI/RESET leaves it. HLT, SHUTDOWN and LOCK must still cooperate with HOLD/HLDA at the defined boundary; no host exit or automatic soft reset | Classic `386.c` reset is wrong for portable 286 |
| Optional extension vectors 9/16 | #9 processor-extension segment overrun and #16 ERROR signal require a populated coprocessor path; mark unsupported for initial unpopulated configuration | No fabricated FPU completion |

Distinguish outcomes at every decode/exception test: **implemented** guest
instruction, **guest #6** for an architecturally undefined encoding,
**unsupported emulator gap** (host status, halted pending reset per contract),
and **not yet measured timing** for an otherwise correct instruction. A
successful POST, DOS boot or compiled acceptance test cannot collapse these.

## Timing evidence and limits

Intel Appendix B clock counts assume prefetched/decoded instructions, no bus
waits, no HOLD and no exception. They describe a maximum execution rate for
their stated path, not a complete per-instruction elapsed-time oracle. The 80286
prefetch/execute overlap and memory/I/O word alignment affect real elapsed
clocks. Intel hardware manual chapter 3 describes two INTA cycles and the
intervening idle states; the board's wait response is additional, not a raw
legacy `cpu_waitstates` value. The classic `cpu.c` timing constants and
`cpu_table.c` 12 MHz generic profile have no PCS 286 wait-state provenance.

For each opcode form, record `documented`, `provisional` or `unknown` timing,
including taken/untaken branch, register/memory, aligned/odd word, repeat
count, descriptor hit/path, exception and bus waits. Accumulate native CPU
clocks with overflow checks and count endpoint waits once. `bm_286_step` may
execute a functional boundary with `BM_286_TIMING_UNKNOWN`; strict
`bm_286_step_clocked` must return unsupported and zero cycles in that case.
A functional boundary with UNKNOWN timing reports `cpu_cycles` only as a
known lower bound including bus waits, not elapsed clocks or a scheduler-valid
duration. Generic `ops.run` uses the existing diagnostic
one-instruction-per-tick convention; its ticks are not 286 clocks or
nanoseconds and must not schedule the PCS 286 machine.
A separately named, visibly provisional scheduling policy needs coordinator
review before a machine can run through unknown paths. No invented constant
or implicit zero-wait assumption closes the evidence gap.

## Reviewed contract decisions and remaining tests

1. State version 2 carries **pending TF single-step state** independently of
   current FLAGS, with atomic import validation. NOP tests a sampled pending
   trap. IRET/POPF transitions and full event priority remain open.
2. `BM_286_BOUNDARY_REP_ITERATION` is appended without renumbering previous
   kinds. The memory REP implementation now returns at most one iteration per
   step, retains prefix restart IP while incomplete, avoids data transactions
   at CX=0, and checks HOLD/interrupts at legal boundaries. Imported state
   clears private continuation and is not cycle-exact restore.
3. State explicitly that **NMI may recover SHUTDOWN without clearing PE**;
   RESET exits protected mode. The current signal and callback shapes can
   support this, so this is a semantic clarification and test, not an ABI
   redesign. Distinguish HLT/SHUTDOWN `IDLE` from an unsupported instruction.
4. Acceptance asserts Intel-defined MSW=FFF0, CS selector F000, FLAGS=0002,
   IDTR base 0/limit 03FF, and 24-bit fetch, without asserting unspecified
   registers. It also tests invalid config/output clearing, atomic import
   rejection, bus failure stop/no retry, HOLD reset, and imported HALT/SHUTDOWN
   event eligibility. Eligible pending NMI/INTR is explicitly unsupported
   before fetch; interrupt delivery is not claimed. State version 3 now gives
   `interrupt_shadow` explicit NONE, INTR_ONLY and SS_LOAD values. The last
   inhibits NMI and #1 as well; STI instruction execution is still pending.

## Reuse decision and next bounded block

Primary recommendation: **selective port / derived rewrite** of the classic
286 semantic algorithms into a private, instance-owned portable 286 core,
cross-checked against Intel's manual. The 286 opcode table and segment/task
logic are useful source references; directly compiling the classic files into
the portable engine would import global CPU state, board-owned PIC/A20/reset,
host APIs, 386 branches and unqualified timing. The current portable 808x core
provides arithmetic patterns and lifecycle precedent but cannot supply 286
protected-mode identity. Preserve original Andrew Jenner, Sarah Walker,
Miran Grca, Fred N. van Kempen, leilei, rtzor and other notices in any derived source; pin each consulted
path to base `87c3fb4876eaad086921bc3444569da026286c36` and classify it as
`selective-port` or `derived-rewrite`, never `new` or clean-room.

The initial CPU/AT integration executed 78 passing tests under UCRT64 GCC
and MSVC Debug, with three Headland/PIC/DMA gates explicitly skipped.
Its real CPU-to-AT wiring test covers HOLD/HLDA, LOCK, grant, reset and resume
against a synthetic endpoint, not a real DMA controller.

The first source block established per-instance lifecycle, Intel's defined
reset state, 24-bit fetch, NOP, state import validation, basic HOLD and strict
clocked refusal while timing is unknown. The second bounded block adds a
private real-mode prefix/ModR/M/16-bit-EA decoder, byte/word register and
memory MOV (88-8B, 8C, 8E ES/DS, A0-A3, B0-BF, C6/C7 /0) and register
XCHG (86/87 with Mod=3, 90-97). Real-mode ES/DS reload sets base to selector
shifted four and limit to FFFF; the reset CS hidden base remains FF0000 until
an actual future CS reload. Protected mode stops before fetch. Every bus or
unsupported failure latches stop without retry; no completed write is undone.
`cpu286_data_transfer.c` is an authored synthetic test for register aliases,
all 16-bit EA forms and displacements, DS/SS defaults, all overrides, odd and
aligned words, limit checks, failed second fragments, overlength prefixes,
protected-mode rejection and independent instances. The first acceptance
suite still passes. Coordinator integration executes 79 passing tests under
UCRT64 GCC and MSVC in Debug and Release; Headland/PIC/DMA remain three explicit
skips. Provenance audit covers 34 components and 173 files without errors.
No firmware was run.

Consulted classic source for this block at exact commit
`87c3fb4876eaad086921bc3444569da026286c36`:
`src/cpu/x86_ops_mov.h`, `src/cpu/x86_ops_mov_seg.h`, `src/cpu/x86seg.c`,
and the earlier `src/cpu/x86.c` / `src/cpu/386_ops.h`. The new private decoder
is a **derived rewrite**, not a direct embedded classic core; its source file
retains inherited author and GPL notices. Intel 210498-005, chapters 2, 3,
5 and Appendix C, supplies address/segment and MOV/XCHG semantics; Intel
210760-002 chapter 3 supplies the memory-XCHG LOCK requirement.

This remains partial: MOV/POP SS now have separate SS-load interruption/trap/NMI
inhibition, memory XCHG awaits bus LOCK semantics,
basic PUSH/POP is now available but protected SS and stack-fault delivery remain
missing, as do LOCK/REP, protected mode, guest faults/interrupts and strings.
Known valid unimplemented forms return host `BM_STATUS_UNSUPPORTED`; an
architecturally invalid encoding is likewise stopped for now, **not** falsely
reported as delivered guest #6. UNKNOWN timing remains unschedulable. This is
not a bootable PCS 286 or an approved full CPU.

## Bounded arithmetic/flags tranche

This tranche implements the documented real-mode 8- and 16-bit binary
arithmetic/logical families ADD, OR, ADC, SBB, AND, SUB, XOR and CMP in their
register/memory and accumulator-immediate forms (00-3D), Group 1 80/81/83,
TEST 84/85/A8/A9 and F6/F7 /0, register INC/DEC 40-4F, FE/FF /0 and /1,
and F6/F7 /2 NOT and /3 NEG. Opcode 82 remains an explicit unsupported gap:
no primary Intel evidence used here establishes its documented status or exact
behavior. F6/F7 /1 is not delivered as guest #6 because fault delivery is
missing; /4-7 MUL/DIV remain unsupported. FF /2,/4,/6 were added in the next
stack/control tranche; /3,/5 far forms and /7 remain unsupported.
No LOCK-prefixed arithmetic, protected-mode execution, shifts, BCD,
multiply/divide, stack or control flow is claimed.

The status bits CF, PF, AF, ZF, SF and OF use width-correct arithmetic.
INC/DEC preserve incoming CF; NOT preserves every flag; CMP/TEST perform no
destination write. ADC/SBB use incoming CF for both result and borrow/carry.
Intel leaves AF undefined after logical operations. The explicit
**logical-AF-clear emulator policy** sets it to zero for deterministic state;
conformance assertions mask AF and do not claim this is a physical 286 output.
Result and flags are computed locally and committed only after successful
writeback. On an odd-word read-modify-write whose second byte write fails,
the first completed byte remains changed, but IP, registers and FLAGS do not
advance; the CPU latches a stop and cannot silently retry. Successful
boundaries still report only known bus-wait lower bounds with UNKNOWN timing.

The authored `cpu286_arithmetic.c` tests all eight binary families, their
direction and immediate groups, sign extension of 83, TEST/CMP read-only
transactions, INC/DEC carry preservation, NEG/NOT, logical AF masking,
odd-word partial-write failure, no retry and deferred/invalid group stops.
It also tests failed immediate fetch, failed second read fragment, failed
INC/DEC/NEG writeback with unchanged IP/registers/FLAGS, and verifies that
memory CMP plus byte/word TEST never invoke a write callback.
No preserved software or timing constants are used. Additional selectively
consulted inherited sources at exact base
`87c3fb4876eaad086921bc3444569da026286c36` are
`src/cpu/x86_ops_arith.h`, `src/cpu/x86_ops_inc_dec.h`,
`src/cpu/x86_ops_misc.h` and `src/cpu/x86_flags.h`. This is a
**derived rewrite** with original project author/license notices retained in
the CPU source, not an embedded global core. Intel 210498-005 chapters 3 and
Appendix B are the primary arithmetic/flag reference. The forms have no
certified elapsed native-clock timing.

Coordinator validation: 81 executed tests pass in UCRT64 GCC and MSVC, each
in Debug and Release; Headland/PIC/DMA remain three explicit skips. The added
independent mathematical oracle covers 1,054,848 binary/unary cases, exhaustive
for the selected byte operands/carry inputs and sampled at 16-bit boundaries.
It does not certify timings, undocumented silicon or complete ISA coverage.
Provenance audit: 34 components/175 files, zero errors. No firmware executed.

## Real-mode far-JMP tranche

EA ptr16:16 and memory FF /5 reload CS selector/base/limit and IP after all
reads succeed. The reset-cache transition, 24-bit addresses without CPU-owned
A20 masking, default/overridden segments, even/odd transfers, per-read failures
and unsupported guest-fault paths have authored tests. No public ABI, scheduler
or timing policy changed. Protected execution and invalid register FF /5 forms
still reject explicitly; this is not guest #6/#13 delivery.

Documented semantics: Intel 80286/80287 Programmer's Reference Manual (1987),
Appendix B, JMP (B-56), and real-mode segment addressing. Consulted inherited
`x86_ops_jump.h` and `x86seg.c` at the provenance-pinned source revision;
no global state or timing constants were ported. Separately observed evidence:
the pinned Harris SingleStepTests FF.5 captures require wrapping from FFFE
to 0000 between the pointer's two words. This corrected three discrepancies;
it does not certify every Intel stepping or physical bus ordering.

See `pcs286-sst-validation.md` for the 61,000-case selected corpus and explicit
pending/revoked counts. Far CALL/RET/IRET, interrupts and timing
remain separate work. This tranche provides a synthetic reset-exit diagnostic,
not a bootable PCS286.

## Bounded stack and near-control tranche

Basic real-mode PUSH supports 50-57, 06/0E/16/1E, 68/6A and FF /6; POP
supports 58-5F, 07/1F and 8F /0. SP is committed after successful transfers;
POP SP loads the popped value without incrementing that replacement. Stack
accesses always use SS; overrides affect only explicit memory operands. No
POP SS or PUSHF/POPF is claimed. PUSHA/POPA and ENTER/LEAVE were added in the
subsequent aggregate/frame tranche below. PUSH at SP=1 stops
as unsupported; Intel's shutdown/exception path is not implemented by wrapping
the operand or pretending success.

Near control covers E8, FF /2, C2/C3, EB/E9, FF /4, 70-7F and E0-E3.
Displacements are relative to the decoded instruction end; near targets retain
CS including its reset hidden base. Taken targets are checked before CALL
stack writes and before committing RET/LOOP state. Fault detection remains a
host unsupported stop, not guest #13 delivery or a certified fault precedence
model. A RET or indirect operand read may already have occurred on failure.
In that tranche, no far transfers, mode changes, interrupts, exception delivery or timings were
implied. Register/IP rollback after host endpoint failure is an emulator
contract, not a hardware bus-abort recovery claim.

New synthetic tests cover 1,024 short-branch flag/direction combinations,
LOOP zero/wrap/termination, stack operands, segments, SP aliases, limits and
every endpoint-failure position for 14 instruction forms. A multi-instruction
CALL/subroutine/RET sequence executes against synthetic RAM. All 82 executed
tests pass under GCC UCRT64 and MSVC, Debug and Release, with three absent
device gates skipped. Provenance: 34 components/176 files, zero errors.

Additional consulted inherited paths at the pinned source commit above:
`src/cpu/x86_ops_stack.h`, `src/cpu/x86_ops_jump.h`, `src/cpu/x86_ops_call.h`,
`src/cpu/x86_ops_ret_2386.h`. The implementation stays a derived rewrite with
the original notices, not an embedded global core. Intel 210498-005 chapter 3
and Appendix B remain the primary instruction reference. This session used
indexed extracts of [the Intel manual mirror](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf)
for PUSH/POP, including B-87's PUSH SP and SP=1 distinction; full PDF retrieval
was unavailable. The [1985 Intel edition](https://www.bitsavers.org/components/intel/80286/210498-003_iAPX_286_Programmers_Reference_1985.pdf)
section 3.6.2.3 corroborates the LOOP/JCXZ distinction through indexed text.
No document or firmware was added to Git.

## Aggregate/frame tranche

Real-mode 60/61 and C8/C9 now implement PUSHA/POPA and ENTER/LEAVE.
Indexed Intel 210498-005 Appendix B entries, including
[ENTER B-40](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=250),
were compared with the pinned inherited `x86_ops_stack.h`; no inherited
timing constants were adopted. ENTER uses only five nesting bits, preserves
read/write interleaving for overlapping frames and leaves FLAGS unchanged.
POPA discards saved SP. The implementation omits that slot's endpoint read;
this is not a claim about the physical 286 bus. Whole-register commit is
deferred until success, while completed bus writes cannot be undone.

Authored tests add all 256 nesting encodings at both alignments, original-SP
preservation, frame restoration, overlap, allocation arithmetic, SS overrides
and explicit unsupported limit/shutdown paths. Failure injection now covers
20 forms, including ENTER level 31 and every odd-word fragment. Preflight
rejection is not guest exception delivery or silicon fault-order validation.
Four local compiler/configuration combinations still pass 82 executed tests
with three explicit device skips. No firmware execution or boot claim follows.
