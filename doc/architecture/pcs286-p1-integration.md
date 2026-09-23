# PCS 286: initial CPU and AT integration

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

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
  Memory XCHG and protected execution remain explicitly unsupported.
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
