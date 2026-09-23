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
  memory XCHG, stack and protected execution remain explicitly unsupported.
  Binary arithmetic/logical operations, CMP/TEST, INC/DEC and NEG/NOT now
  operate on byte/word operands with defined flag semantics. Logical AF is
  undefined by Intel; deterministic clearing is an emulator policy only.
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
81 passing tests, including CPU data-transfer, arithmetic and independent
ALU-oracle suites (84 registered, three skipped).
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
Provenance covers 34 components/175 files; 21 Python tests and the catalogue
check pass. No timing or performance certification follows from these counts.

The portable CI path filters now include `tests/components/**`, so a change
limited to these tests also triggers validation. These local results are not
a claim that remote Linux/macOS CI has already run.

No firmware, disk images or manufacturer scans are included or executed.
Authorship and exact inherited source references remain in the CPU source and
the versioned provenance manifest. The AT interconnect is authored new code.

## Next work

1. Extend the CPU through coherent instruction families: stack and control
   flow, then remaining ALU families, with authored boundary/flag/bus tests. Resolve distinct
   STI versus SS-load inhibition and LOCK before completing deferred transfers.
   Keep valid-but-unimplemented, invalid guest encoding and unknown timing
   distinct. Do not substitute a success/NOP or invent elapsed cycles.
2. Implement PIC cascade and DMA byte/word engines separately; the current
   interconnect is not either controller. Preserve their inherited provenance.
3. Review Headland register/decode evidence before implementation. Reference
   schematic wiring must not be presented as observed PCS286 wiring.
4. Compose board devices only after their contracts and ownership pass. No
   BIOS patch, permanently enabled memory alias or forced POST result.

See `pcs286-cpu286-coverage.md`, `pcs286-at-fabric-notes.md` and `pcs286-p0.md`
for the larger pending coverage and acceptance gates.
