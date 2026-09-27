# PCS 286: initial CPU and AT integration

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Current activation: [public functional step/run](pcs286-protected-public.md)
now use the reviewed profile. Public PE gate statements below describe the
earlier checkpoint; the strict clock gate and stated semantic limits remain.


Local integration branch: `feature/pcs286-integration-p1`, based on portable
merge `8e5cd917d95536fbdfe65ce2d5d658eda0633d20` (PR #208).
This is a component milestone, not a bootable PCS 286 or completed P1/P3.

## Implemented boundary

- Independent AT interconnect: CPU/one external requester ownership,
  LOCK/HOLD/HLDA, cancellation and explicit refusal of competing requests.
- Partial 80286: instance-owned reset state, high reset fetch, NOP, state
  import validation, HOLD acknowledgement and explicit unsupported-event stops.
  Real-mode data transfer now includes basic byte/word MOV, register XCHG,
  16-bit effective addresses, segment overrides and ES/DS/SS reload.
  Memory XCHG now uses the bus-lock adapter; protected execution remains unsupported.
  Binary arithmetic/logical operations, CMP/TEST, INC/DEC and NEG/NOT now
  operate on byte/word operands with defined flag semantics. Logical AF is
  undefined by Intel; deterministic clearing is an emulator policy only.
  Basic real-mode PUSH/POP (including POP SS, excluding flags), near CALL/JMP/RET,
  short Jcc, LOOP/LOOPE/LOOPNE and JCXZ are implemented. PUSHA/POPA and
  ENTER/LEAVE now cover real-mode aggregate saves and procedure frames;
  real-mode far JMP EA and memory FF /5 reload CS and leave the high reset
  mapping. Far CALL/RET, interrupts and protected control remain missing.
- A synthetic integration test connects these real components and checks
  suspension, DMA access after grant, return to CPU ownership and reset. It
  also raises a DMA request during a fetch: the current instruction completes
  before HLDA grants access. Endpoint errors propagate unchanged; the CPU
  stops without advancing IP or retrying the failed access until reset.
- Endpoint waits remain requester clocks and are counted once. CPU elapsed
  timing is UNKNOWN; strict clocked execution refuses before fetching.
- Existing generic engine contracts, scheduler and PCS86/M15 implementations
  are unchanged. PCS286 is not registered as an available runtime machine.

## Validation, 2026-09-23

Engine-only Debug and Release builds with UCRT64 GCC and MSVC each execute
82 passing tests, including CPU data-transfer, arithmetic, independent
ALU-oracle and stack/control suites (85 registered, three skipped).
Three more tests explicitly skip: Headland, AT PIC and AT DMA implementations
are absent. Assertions remain enabled; new component code uses warnings as
errors. The MSVC check exposed and corrected a size_t narrowing in a test.
New component test assertions remain enabled with `-UNDEBUG` / `/UNDEBUG`,
even in Release. Data-transfer tests exercise aliases and flag preservation,
all 16-bit effective-address forms, overrides, aligned/odd words, failed
second fragments, operand/immediate fetch errors and prefix-length limits.
Review caught an uninitialized immediate read on fetch failure; it was fixed
and covered before integration. Invalid CS and protected execution reject
before bus access rather than pretending to provide missing fault semantics.

The independent oracle exhausts all byte operand pairs and both carry inputs
for eight binary families, adds 16-bit boundary pairs and unary cases: 1,054,848
instruction executions per run. It derives overflow from signed-range bounds
and auxiliary carry from nibble arithmetic, independently of the core's bit
formulas. This is exhaustive for those byte inputs, not for the complete ISA
or all 16-bit inputs. Separate tests cover immediate groups, read-only CMP/TEST,
sign extension, failed reads/fetches/writes and partial odd-word writes without
retry or flag/IP commit. MSVC's possible-uninitialized-source warning was
resolved with explicit initialization; successful unary paths still supply
their own operand before calculation. All four configurations passed afterward.
Provenance covers 34 components/176 files; 21 Python tests and the catalogue
check pass. No timing or performance certification follows from these counts.

The stack/control suite tests register and memory operands, sign extension,
286 PUSH SP and POP SP behaviour, fixed SS stack addressing versus overridden
explicit operands, near CALL/RET sequencing, and all 16 short conditions over
32 relevant flag combinations in both displacement directions. LOOP variants
cover CX=0/1/2/FFFF and both ZF values. An endpoint failure is injected at every
transfer of 20 representative instruction forms, including odd-word fragments:
CPU state remains unchanged, completed writes persist and cannot be retried.
Stack/code limits reject explicitly; PUSH at SP=1 is an unsupported shutdown
path, not a claimed guest exception or successful stack wrap. Tests include
SP=0 push/pop, relative IP wrap, RET cleanup wrap, non-taken out-of-limit
targets and deferred SS/far/flags instructions. No new public
ABI or changes to the common scheduler were required.

Aggregate/frame tests cover PUSHA's original SP, POPA's discarded SP slot,
all 256 ENTER nesting encodings at both stack alignments (level modulo 32),
overlapping frame reads/writes, LEAVE restoration, allocation arithmetic and
segment overrides that do not redirect SS. Multiword operations commit CPU
registers only after success; completed external writes persist on host errors.
Preflight rejects unsupported segment-crossing/shutdown cases, including
PUSHA with odd SP from 1 through 15, before operand accesses. This is not guest
fault delivery, silicon fault precedence or cycle-accurate bus emulation.
POPA omits the discarded slot's endpoint read as an implementation policy,
not a measured physical-bus claim. See Intel Appendix B, ENTER/LEAVE/PUSHA/POPA;
the indexed primary-manual extracts were checked alongside the pinned inherited
`x86_ops_stack.h`. No timing constants were imported from that implementation.

The portable CI path filters now include `tests/components/**`, so a change
limited to these tests also triggers validation. These local results are not
a claim that remote Linux/macOS CI has already run.

No firmware, disk images or manufacturer scans are included or executed.
Authorship and exact inherited source references remain in the CPU source and
the versioned provenance manifest. The AT interconnect is authored new code.

## Next work

Independent hardware comparison is now available as a development-only tool:
see `pcs286-sst-validation.md`. The first 51,000-case pinned selection gives
49,076 functional matches, zero discrepancies, 1,923 explicit pending cases
and one upstream revocation. It is not whole-ISA, timing or board validation.
The core and its clock policy were not changed for this adapter.

1. SS reload and direct I/O are now available for board diagnostics. Build
   the RAM/ROM lifecycle next, then remaining instruction families and exception
   handling with authored tests. Complete STI execution and LOCK before relying
   on those paths; the SS versus INTR-only shadow distinction is implemented.
   Keep valid-but-unimplemented, invalid guest encoding and unknown timing
   distinct. Do not substitute a success/NOP or invent elapsed cycles.
2. Implement PIC cascade and DMA byte/word engines separately; the current
   interconnect is not either controller. Preserve their inherited provenance.
3. Review Headland register/decode evidence before implementation. Reference
   schematic wiring must not be presented as observed PCS286 wiring.
4. Compose board devices only after their contracts and ownership pass. No
   BIOS patch, permanently enabled memory alias or forced POST result.

## Earliest assembly milestones

The current executable integration is a synthetic CPU + AT bus fixture, not
the PCS286 board: the system, Headland and IOC02 targets are still interfaces.
Do not wait for the entire protected-mode ISA before writing board diagnostics,
but do not label a synthetic fixture as a firmware boot.

1. **Reset diagnostic:** implement a board-owned RAM/ROM map and lifecycle,
   documented reset aliases, far reset transfer and SS reload. Assert fetch
   and mapping traces with authored code first. Unknown decode/register paths
   stop explicitly. This can run instruction-by-instruction without a claimed
   real-time clock or GUI.
2. **Firmware/POST diagnostic:** connect evidenced Headland/IOC02 behaviour,
   A20 and CPU reset, PIC cascade, PIT/refresh, RTC/CMOS and keyboard controller;
   supply CPU I/O, interrupts and the instruction paths actually required.
   Select and document a scheduling policy before timed devices run together;
   diagnostic instruction counts are not CPU clocks. Preserve unresolved board
   wiring as an evidence gap, not a reference-schematic assumption.
3. **Visible, usable machine:** integrate/validate the existing portable PVGA1A
   and the PCS286 DAC/video path, then floppy controller/DMA/media and input.
   Existing PIC8259, DMA8237, PIT8253 and FDC765 components are reuse candidates,
   not proof that AT cascade, PIT8254, WD37C65 or PCS286 wiring is complete.
   Register the machine as available only once its declared profile works.

See `pcs286-cpu286-coverage.md`, `pcs286-at-fabric-notes.md` and `pcs286-p0.md`
for the larger pending coverage and acceptance gates.

## Far-JMP validation, 2026-09-23

Both real-mode forms commit CS/IP only after all pointer reads succeed. An
authored reset program jumps from FFFFF0 to F000:0100 and fetches its next
instruction at F0100. A separate target above 1 MiB proves that A20 masking
remains board-owned. Tests cover segment overrides, even/odd pointers,
per-transfer failures, invalid encodings and explicit unsupported limits.

The expanded hardware comparison exposed three FF /5 discrepancies: a pointer
at offset FFFE reads its selector word at offset 0000 on the captured Harris
286. The core now wraps between words while still checking each word's limit;
an independently authored regression protects this case. No exclusion or
metadata mask was introduced to hide these failures.

UCRT64 GCC and MSVC, Debug and Release, each pass 84 ordinary tests, with three
absent-chip gates skipped (87 registered). The optional external-corpus CTest
also passes in GCC Debug. All four comparator runs agree: 61,000 cases,
57,498 matches, 3,501 pending, one upstream revoked, zero discrepancies.
These are functional register/RAM results, not timing or PCS286 boot evidence.

## SS reload and direct I/O, 2026-09-23

Real-mode MOV/POP SS reload the segment cache and acquire an explicit SS_LOAD
shadow. Architectural state version 3 distinguishes it from INTR_ONLY: only
SS_LOAD also defers NMI and single-step delivery. Old v2 imports are rejected,
not silently reinterpreted. There is still no STI instruction implementation.
Shadows expire on a completed instruction, not on HOLD, idle or a host error.
Each successful SS reload rearms its shadow (functional policy, not a measured
consecutive-instruction silicon guarantee). SS-load's own TF trap is suppressed;
the following completed instruction samples TF normally, and any already
deferred trap/NMI is retained. Actual event delivery remains unsupported.

IN/OUT E4-E7/EC-EF use the existing bus callback, BM_ADDRESS_IO and 16-bit port
addresses. Immediate ports are zero-extended. Aligned words use one logical
word access; odd words split into low/high byte accesses, wrapping the port
address at FFFF. This follows the logical 16-bit bus contract, not a physical
pin capture. The board owns 8-bit endpoint splitting and unmapped-port policy.
No guessed port values, chip IDs, fixed waits or native elapsed clocks are added.
IN commits AL/AX only after successful reads; completed endpoint side effects
on a later failure remain, and the stopped CPU cannot retry them.

Authored tests exercise old/new SS addressing (POP SS; POP SP), MOV SS; MOV SP,
prefixes, null real-mode SS, odd reads, failures, explicit unsupported limits,
HOLD, pending NMI/INTR/#1, reset and state-import validation. I/O tests cover
160 opcode/prefix/port combinations, every transfer failure in all eight forms,
unmapped errors, unsupported LOCK/REP/protected execution and HOLD during I/O.
Other segment loads do not acquire SS inhibition. Engine/scheduler ABI and
PCS86/M15 code are unchanged; only the 286 inspection-state version changed.

Validation: GCC UCRT64 and MSVC, Debug/Release, 86 ordinary tests pass and three
absent-chip gates skip (89 registered); optional hardware CTest also passes in
GCC Debug. Thirty Python tests pass. Provenance: 34 components/180 files.
The expanded 15-file hardware subset gives 65,843 matches out of 71,000,
5,156 pending, one upstream revoked, zero discrepancies. MOV-segment/POP-SS
register/RAM captures do not test multi-instruction inhibition or I/O signals;
those are authored contract tests, not additional hardware-certified cases.

Sources: Intel 210498-005 (1987), Appendix B MOV/POP/IN/OUT and Chapter 2 flags;
consulted inherited MOV-segment/stack/I/O code at the pinned provenance commit.
Indexed primary manual: https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf
No firmware was run. The next milestone is a board-owned RAM/ROM diagnostic,
not a claim that a full PCS286 firmware boot is ready.

## Backing memory and synthetic board path, 2026-09-23

`blumach_pcs286_memory` now supplies instance-owned RAM and a private 128-KiB
firmware snapshot, including explicit low/even and high/odd lane interleaving.
It validates capacity/layout before allocation, publishes only a complete
object and unwinds all three possible allocation failures. New RAM is zero by
emulator initialization policy, not a measured power-on pattern. CPU reset
does not clear it. No whole-machine reset semantics are introduced.

Resolved-offset access validates the complete range before any write. Byte
assembly is host-endian/alignment independent. ROM writes and DEBUG writes
are rejected; success changes only read value or selected RAM bytes, never
timing. Decoding, contiguity across mappings, write-enable, CPU A20 and
requester-specific waits belong to the future board/chipset adapter.

The new authored composition test uses the actual partial 286, AT interconnect
and backing storage. It fetches a reset trampoline at FFFFF0h, far-jumps into
test RAM, initializes SS/SP, pushes/pops 1234h and roundtrips it through a test
I/O latch. A hole returns UNMAPPED, and CPU reset preserves stack bytes and
returns to the high reset vector. The two linear windows and latch exist only
in test code. They are not a production PCS286 profile, BIOS, POST or hardware
timing evidence. All instruction boundaries remain TIMING_UNKNOWN.

The reviewed Headland research note from `feature/pcs286-headland` distinguishes
documented G-2 reference ROM chip-select ranges from unresolved PCS286 backing
offsets, straps, remap control and register behavior. Consequently no permanent
60000h/80000h alias, GC103 register model or invented reset latch is added.
The note is evidence guidance only; this change copies no implementation or
restricted document and does not modify that agent's worktree.

Validation: GCC UCRT64 and MSVC, Debug/Release, 87 ordinary tests pass, three
absent-chip gates skip (90 registered). GCC Debug also passes the optional
pinned SST functional subset (91 registered). Thirty Python tests, catalogue
check and provenance audit pass (35 components, 183 files). Storage tests check
both firmware layouts byte-for-byte, 1..8-byte accesses in both byte orders,
instance isolation, all allocation failures, invalid inputs, ROM protection,
DEBUG restrictions, unchanged transactions on errors and no partial out-of-range
writes. An initial test used one-based failure indices; corrected to the
existing allocator's zero-based convention before the full validation run.

AT PIC cascade and real-mode CPU interrupt entry/return now have separate
component tests and an authored IRQ9/EOI/IRET composition test. See
`pcs286-at-pic.md` and `pcs286-cpu286-coverage.md`. This is not yet a complete
board composition or executable firmware profile. Exact Headland/IOC02 board decoding
still needs evidence review; it is not unlocked by this synthetic test. Timed
PIT/RTC/KBC integration additionally needs an explicit CPU timing policy. The
machine factory and frontend registration remain absent, and no BIOS or media
were executed. No engine/scheduler, PCS86 or M15 behavior changed.

## Combined diagnostic: reset, RAM/ROM, AT and interrupts

`pcs286-component.board-interrupts` now composes the actual partial 80286,
`blumach_pcs286_memory`, `blumach_at_bus` and `blumach_at_pic`. Only wiring and
test code are new; no production CPU/chip behavior or public ABI changes.
This supersedes the earlier next-step gap for synthetic composition, not the
missing physical PCS286 decoding, factory or device models.

An authored five-byte trampoline is fetched from the high reset ROM. It jumps
to a RAM program that initializes SS/SP, writes the IVT and programs both PICs
via guest instructions through the AT interconnect. No CPU state import or
host-preset PIC configuration supplies success. The CPU halts after STI, then
handles slave IRQ9 and master IRQ1, writes distinct RAM signatures, sends the
appropriate EOIs, restores AX and returns to the saved CS:IP/SP with IRET.

The test also verifies:

- Pending IRQ cannot be acknowledged while HOLD/HLDA grants the bus elsewhere.
  An authored external ISA requester writes a scratch word while the CPU is
  held; this validates arbitration, not an implemented DMA controller or card.
- CPU accesses without ownership are side-effect-free IDLE. DEBUG observes
  RAM/PIC during HOLD without consuming interrupts, generating waits, changing
  ownership or permitting writes.
- Synthetic memory/I/O/INTA waits reach each boundary exactly once. They are
  deliberately chosen fixture values in requester clocks, not hardware timing;
  boundary timing remains UNKNOWN and strict scheduling is still disabled.
- A host failure on second INTA or first frame write latches CPU execution off
  and does not repeat acknowledgement. PIC state already changed by first INTA
  is not rolled back. CPU-only reset leaves that state; resetting the complete
  test composition clears it and permits guest reinitialization and recovery.
- RAM survives this explicit test reset policy. A second instance is isolated;
  every one of six construction allocation failures cleans up; destroying a
  held instance with pending IRQ disconnects live callbacks before consumers.

The reset policy, 12/8 MHz requester metadata and two linear memory windows
exist only in this test. They make no claim about PCS286 straps, A20, Headland,
IOC02, RAM aliases, wait-state programming or physical reset nets. No original
ROM, BIOS, disk or external document is copied or executed. A full historical
implementation narrative remains deferred until there is a production machine.

Validation: GCC UCRT64 and MSVC Debug/Release each pass 91 ordinary tests, with
Headland and AT DMA still explicitly skipped (93 registered). GCC Debug adds
the unchanged pinned SST regression (94 registered). Thirty Python checks,
catalogue and provenance audit pass (36 components / 187 files). The first
GCC Release run exposed a test comparing struct padding with memcmp; changed
to checks of every defined CPU/PIC field, without relaxing architectural state
preservation or changing production code. No remote CI or POST claim.

The subsequent CPU tranche implements real-mode INT/INT3/INTO and remaining
FLAGS/control operations (PUSHF/POPF, LAHF/SAHF, CLC/STC/CMC, CLD/STD).
See `pcs286-cpu286-coverage.md` for scope, inherited provenance, IF/TF rules
and failure tests. Validation now passes 92 ordinary tests on GCC UCRT64 and
MSVC Debug/Release, with the same two absent-device skips; the GCC Debug SST
subset is unchanged regression coverage, not validation of these new groups.
Thirty Python checks, catalogue and provenance (36 components / 188 files)
pass. No CPU timing, machine factory, ROM execution or board decode added.

The next completed CPU tranche adds real-mode far CALL 9A/FF /3 and RETF CA/CB,
with stack/CS-cache, A20, alignment, overlap, parameter discard and per-transfer
failure tests. All 93 ordinary tests pass in GCC/MSVC Debug/Release, with the
same two skips; provenance covers 36 components / 189 files. The selected SST
regression does not yet include far CALL/RET groups. Shared FF pointer wrap and
whole-frame preflight are explicitly bounded policies, not new silicon evidence.

LEA/LDS/LES/XLAT are now implemented and covered by independent EA, source,
destination, cache-reload, alignment, wrap and failure tests. All 94 ordinary
tests pass in GCC/MSVC Debug/Release, with the same two skips; provenance
covers 36 components / 190 files. The unchanged optional SST selection does
not cover the new groups. Real-mode functional scope only, no clock/ABI change.

Group 2 shifts/rotates now cover all seven documented operations, byte/word
registers and memory, count 1/CL/imm8 and five-bit masking. An independent
closed-form oracle checks 1,899,520 scalar cases, plus opcode/alias/segment,
alignment and every-transfer failure cases. Undefined flags and zero-count
memory reads are explicit policies, not chip/bus certification. GCC UCRT64
and MSVC Debug/Release pass 95 ordinary tests with the same two skips;
30 Python checks and provenance (36 components / 191 files) pass.
The unchanged SST selection does not include Group 2. Timings remain UNKNOWN.

MUL/IMUL byte/word and immediate IMUL 69/6B, plus CBW/CWD, now pass an
independent sign/magnitude oracle (4,327,680 scalar checks), register aliases,
all segment selections, alignment and every fetch/read failure. Undefined
multiplication flags are explicitly preserved as emulator policy. All 96
ordinary tests pass in GCC UCRT64/MSVC Debug/Release with two device skips;
30 Python tests and provenance (36 components / 192 files) pass. The unchanged
SST selection excludes these groups. No CPU timing or board boot claim.

DIV/IDIV now stage results and deliver real-mode #DE on divisor zero or
quotient overflow, with prefix-inclusive IP and EXCEPTION vector 0. Guest
repair/IRET/retry, TF, aliases, stack/IVT alignment and every-transfer failure
are tested, plus 2,889,216 scalar cases against an independent long-division
oracle. All 97 ordinary tests pass on GCC UCRT64/MSVC Debug/Release with two
device skips; 30 Python tests and provenance (36 components / 193 files) pass.
The unchanged SST selection excludes these groups and still defers exceptions.
Undefined FLAGS and clearing a previously SS-deferred trap on #DE are explicit
functional policies; no timing, general fault escalation or board POST claim.

DAA/DAS/AAA/AAS/AAM/AAD are now implemented with explicit undefined-flag
preservation. AAA/AAS adjust full AX; AAM zero enters #DE without partial
results, instead of inheriting the old base-ten substitution. Authored tests
cover 1,319,424 scalar cases, 40,000 valid ADC/SBB plus decimal-adjust pairs,
all segment overrides, aligned/odd exception entry, transfer failures and
guest patch/IRET/retry with TF. All 98 ordinary tests pass in GCC UCRT64/MSVC
Debug/Release, two component skips, 30 Python checks and provenance
(36 components / 194 files). The unchanged SST selection excludes BCD;
nondecimal/invalid-BCD edges still need 286 hardware-capture comparison.

Unprefixed MOVS/STOS/LODS/CMPS/SCAS byte/word now execute a single element
independently of CX, with source overrides, fixed ES destination, DF and
16-bit index wrap. Registers/flags commit after accesses succeed; external
writes already completed on a host failure persist without replay. CMPS uses
inherited destination-first reads as a functional policy, not silicon ordering.
The authored suite covers 3,600 matrix cases, 3,145,728 comparisons, 150 endpoint
failures, overlap, segment/physical wrap, unused segments, TF and mid-read HOLD.
All 99 ordinary tests pass on GCC UCRT64/MSVC Debug/Release, two device skips,
30 Python checks and provenance 36 components / 195 files. The unchanged SST
selection does not cover strings; no physical timing or BIOS/POST claim.

F2/F3 memory repetition now executes at most one element per step, preserving
prefix IP while incomplete and reusing private per-instance decode until an
interrupt, reset or successful state import. CX/ZF stopping, zero data accesses
at CX=0, INTR/NMI/TF/IRET, HOLD, overlapping copies, two-instance isolation,
code changes and transfer failures have authored tests: 5,000 matrix cases,
every initial CX and a full 65,535-element run. Incoming STI/SS shadow consumption
after the first completed element is explicit functional policy pending silicon
comparison. Original authors and derived-rewrite provenance remain intact.
All 100 ordinary tests pass on GCC UCRT64/MSVC Debug/Release, two device skips,
30 Python checks and provenance 36 components / 196 files. Existing SST coverage
still excludes strings/REP. Timing stays UNKNOWN; no machine boot claim.

INSB/INSW/OUTSB/OUTSW now share the scalar port-transfer helper and existing
REP continuation. Fixed ES destination, overridable source, unchanged DX/FLAGS,
zero-count no access, DF/index wrap and ordered memory/port transactions are
covered by 2,880 combinations, interruption/IRET/HOLD, limits and every-transfer
failures before/after effects. Consumed input and partial writes are never
rolled back or replayed. Segment preflight is a host-gap policy, not delivered
guest exceptions or certified fault ordering. All 101 ordinary tests pass in
GCC UCRT64/MSVC Debug/Release with the same two skips; 30 Python checks and
provenance 36 components / 197 files pass. The unchanged SST selection does not
cover these instructions; timing remains UNKNOWN, no BIOS/POST claim.

Memory XCHG now asserts implicit bus exclusion; F0 supports memory-destination
arithmetic/logical RMW, INC/DEC/NOT/NEG and XCHG. The pin callback brackets all
operand fragments and architectural commit, or releases after a stopped host
failure. LOCKED attributes propagate through the actual AT bus. A synthetic
competing master is denied until release/HOLD/HLDA across 320 XCHG cases;
430 locked/unlocked RMW comparisons, pin/attribute checks, IRQ and every-transfer
failures also pass. No DMA chip or timing is inferred from this integration.
GCC UCRT64/MSVC Debug/Release pass 102 ordinary tests, two explicit device skips;
30 Python checks and provenance 36 components / 198 files pass. No new SST claim.

Memory MOV (register, immediate, moffs and segment-register forms) and memory
Group 2 shifts/rotates now accept F0 through the validated data-access helper.
No artificial read is introduced for write-only MOV. The test fixture adds
450 MOV and 21,504 shift locked/unlocked comparisons, competing requester
exclusion, IRQ deferral and failures at each transfer before/after effects.
Register-only LOCK, undocumented shift /6 and LOCK REP remain unsupported.
The same 102 ordinary tests pass in all four local configurations, two device
skips, 30 Python checks and provenance 36 components / 198 files. Timing remains
UNKNOWN and existing masked-zero read/no-write policy is not a bus capture.

Real-mode INTR now uses the bus-lock adapter across both INTA callbacks and
the first stack word (B-2/later policy); remaining frame and IVT are outside
that window. CPU/PIC/AT integration connects the real arbiter. Tests cover
contention, both alignments, failure cleanup and missing adapter refusal.
No new timing or complete-machine claim follows.

LOCK MOVS/INS/OUTS now retain bus exclusion across REP iterations and mark
memory/I/O fragments. Completion, accepted events, failures and lifecycle
cancellation release the window. Tests cover 4,320 transfer combinations,
interrupt/restart, failure effects, cleanup and 65,535 elements. Combined
LOCK/event pin ordering remains explicit functional policy, not measured
silicon timing; other strings/register forms and protected windows are pending.

BOUND now compares signed inclusive memory bounds, delivers restartable vector 5,
and delivers vector 6 for register second operands. DIV/AAM share the private
fault helper. Tests cover all 16-bit indices in a crossing-zero range,
register/address matrices, invalid encodings, retry/TF and every-transfer errors.
Four local configurations pass 103 ordinary tests with two device skips.
BOUND segment-limit vector 13 remains a visible unsupported gap, as do general
fault escalation and protected delivery. No measured timing or boot claim.

The following tranche delivers BOUND segment-limit #13 and processor-extension
#7 (ESC EM/TS; WAIT MP+TS). WAIT can complete on the explicitly unpopulated
interface, with BUSY/ERROR inactive. Untrapped ESC remains unsupported and
does not fabricate stores or FPU results. Tests cover 144 control combinations,
prefix restart, IRET/TF, fault-frame failures and BOUND EA repair/retry.

Real-mode SMSW/LMSW/CLTS now let the guest control MSW and recover from #7
without test-side state edits. Register/memory forms, #13 overruns and
per-transfer failures are tested. Setting PE stops at the next boundary;
protected execution is not implemented. Real-mode SGDT/SIDT/LGDT/LIDT now
transfer six-byte operands with 24-bit bases, #6 register rejection, #13
whole-operand preflight and failure-safe register commits. An executable
LIDT/INT/IRET sequence validates relocation of the IVT. Access ordering and
undefined-byte FF readback are explicit functional policies, not timing
evidence. General real-mode exceptions/escalation and untrapped ESC
handshake/absence detection also remain pending.
Protected lock windows remain coupled to future protected execution.
Current unsupported combinations are missing
implementation, not the 386 legal-prefix rule or fabricated guest #UD.
Headland/IOC02 evidence
and implementation, AT DMA, timed PIT/RTC/KBC, CPU timing and protected execution
remain separate gates before a real PCS286 boot profile can be validated.

Real-mode out-of-limit interrupt vectors now attempt #8 with first-byte
restart IP and no error word. An inaccessible #8 enters guest shutdown,
not a host error or automatic reset. The diagnostic boundary reports shutdown,
ignores INTR and allows HOLD; real-mode NMI can recover with a usable frame/IVT,
otherwise an IVT failure leaves NMI blocked until reset. Pin/order choices
are explicit functional policy, not measured timing. Tests exhaust 262,144
vector/limit pairs and execute a guest-only IDTR repair/retry. Stack-fault
escalation and protected double-fault delivery remain separate unfinished work.

Scalar ModR/M and moffs/XLAT operand overruns now unwind through a private
decode marker and deliver real-mode #13. Registers are not committed and the
instruction's LOCK is not inherited by its exception frame. Authored tests
cover 168 forms, per-transfer failures and guest-only repair/IRET/retry.
This does not yet generalize to implicit stack, fetch, strings or multiword
pointer preflight faults. Earlier reads (such as POP's stack source) are not
claimed absent. Generic transport still cannot manufacture a guest exception
from a host error. Timing remains UNKNOWN.

The next tranche extends this unwind path to supported implicit stack
operations, including aggregate frames and IRET. Valid SS overruns deliver
#13; inability to form the exception frame enters guest shutdown. Invalid
imported caches and host errors still refuse. Tests include Intel's PUSHA
odd-SP distinctions, 19 prefixed forms, per-transfer failures, guest-only
LEAVE repair/retry, NMI recovery failure and INTA lock release. Architectural
register retention and preflight order remain explicit functional policies.
Fetch/target, strings, compound-pointer faults, protection and timing are
still open; this is not BIOS/POST validation.

Compound-pointer preflights now deliver #13 for valid-cache word overruns
in LDS/LES and indirect far CALL/JMP, retaining independent word-offset
wrap. Tests cover 96 fault combinations, 32 valid boundary pairs, failure
injection and four guest repair/retry programs. No pointer/return frame
is partially committed on preflight failure.

String faults need a separate evidence gate: Intel's exception notes describe
partial index/count updates and its 1984 restart erratum explicitly concerns
protected mode and specific steppings. These are not safely replaced by a
generic atomic retry. See the latest coverage section for sources and the
required mode/stepping/fault-stage matrix. Strings themselves remain usable;
their segment-fault paths still refuse explicitly.

The original scans have now been checked; the concrete fault-stage evidence
and implementation plan is in `pcs286-string-fault-restart.md`. This research
does not certify a PCS286 CPU stepping or change CPU behavior. Fetch/control
target limit faults can proceed independently while string evidence remains open.

That bounded block now delivers real-mode #13 for instruction-byte limit,
ten-byte-length and taken near-target violations. Prefixed restart, no premature
CALL/RET/LOOP commit, endpoint failures and guest repair/retry are tested.
Physical prefetch and sequential boundary behavior are not certified; see the
latest CPU coverage section. Invalid encodings and protection remain unfinished.
