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
  16-bit effective addresses, segment overrides and ES/DS reload. MOV SS,
  memory XCHG and protected execution remain explicitly unsupported.
  Binary arithmetic/logical operations, CMP/TEST, INC/DEC and NEG/NOT now
  operate on byte/word operands with defined flag semantics. Logical AF is
  undefined by Intel; deterministic clearing is an emulator policy only.
  Basic real-mode PUSH/POP (excluding POP SS and flags), near CALL/JMP/RET,
  short Jcc, LOOP/LOOPE/LOOPNE and JCXZ are implemented. PUSHA/POPA and
  ENTER/LEAVE now cover real-mode aggregate saves and procedure frames;
  far control transfers are still missing.
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

1. Prioritize real-mode far control, SS reload and direct I/O to enable board
   bring-up, then remaining ALU families and exception handling with authored tests. Resolve distinct
   STI versus SS-load inhibition and LOCK before completing deferred transfers.
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
