# Portable Intel 8088 core

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: functional model with a narrowly verified Intel clocked baseline and
an explicitly provisional clocked bring-up path; general Intel cycle timing
remains unsupported.

## Scope and component boundary

`BM_808X_INTEL_8088` selects the original Intel 8088 ISA and bus traits in the
reusable 808x component. The model owns all mutable register, interrupt,
prefetch and bus-interface state per instance. It has an 8-bit external bus, a
four-byte instruction queue, 20-bit physical addressing and no NEC 8080 mode.
It is not an IBM PC 5150 definition and contains no machine, firmware, CGA or
board-specific condition.

The executor is shared only for 8086-family operations whose equivalence is
established. Each instance points at an immutable model profile; the Intel
profile positively selects its bus width, queue depth, FLAGS image, native-only
mode, divide boundary and opcode decisions. NEC extensions, 8080 mode and V30
timing are selected only by the V30 profile. Intel behavior is therefore not
defined as a V30 interpreter with a negative list of disabled instructions.

`bm_8088_classify_opcode()` is the executable 256-entry primary map. It also
classifies every grouped ModR/M operation field as documented, silicon alias,
silicon-undocumented or undefined. The Intel-published ISA is the documented
baseline. Hardware/microcode evidence promotes the original-silicon aliases
`0Fh`, `60h`-`6Fh`, `82h`, `C0h`, `C1h`, `C8h`, `C9h` and `F1h`; `D6h` is
silicon-undocumented. Group promotions are `D0h`-`D3h /6` (`SETMO`),
`F6h`/`F7h /1` (`TEST` aliases) and `FFh /7` (`PUSH` alias). The observed
undefined grouped holes `8Fh /1`-`/7`, `C6h`/`C7h /1`-`/7` and `FEh /2`-`/7`
remain unsupported. Thus `82h` is a complete alias of `80h`; the manual's
selective instruction listings are not misread as evidence that the logic
subfields fault on physical silicon.

The BIU configuration is instance-owned. Intel word memory and I/O operations
are emitted as ordered low-byte then high-byte transactions even at even
addresses. Instruction fetches are one byte wide, the queue capacity is four,
and control transfers reset the queue to the selected target. Segment-plus-
offset addresses wrap at 20 bits.

The existing execution-clock tables, operand placement and complete-boundary
durations are NEC V30 evidence and are not reused for the Intel model.
`bm_808x_step()` remains the general functional instruction-boundary path.
`bm_808x_step_clocked()` is the strict path: only the specific full-queue,
unprefixed, verified instruction forms are admitted. Other Intel boundaries
are rejected as unsupported **before** execution, with zero cycles; they do
not fall back to NEC timing. The strict path is not a general Intel EU/BIU
scheduler.

`bm_808x_step_clocked_provisional()` is a separate bring-up callback for a
clocked engine. It uses the functional executor from an empty reset queue and
can advance through its supported memory, I/O, branch and interrupt paths.
For each completed boundary it chooses a deterministic virtual duration:
observed prefetch bus-phase clocks plus observed operand bus-phase clocks,
including waits, plus a fixed four-clock internal quantum. The timing
observation labels the boundary `PROVISIONAL` (version 39), not `EXACT`; the
instruction execution-clock classification remains `UNKNOWN`. Equal minimum
and maximum here describe the selected scheduling increment, **not** measured
or documented 8088 timing. This formula serializes bus occupancy and does not
model EU/BIU overlap, instruction-specific internal work or interrupt-acknowledge
waveforms. It exists to integrate a functional 8088 with timers and other
clocked components while accurate timing is developed; it is not evidence of
cycle fidelity or of successful boot on a historical machine.

## Evidence classification

### Documented by Intel

The primary source is Intel Corporation, *The 8086 Family User's Manual*,
October 1979, order number 9800722-03:

- sections 2.2-2.3, printed pages 2-5 through 2-11: common EU, four-byte 8088 queue,
  byte prefetch, queue reset on control transfer, and 20-bit physical address
  generation;
- section 2.4, printed page 2-16: one byte per 8088 I/O bus cycle and ordered two-cycle
  word transfers;
- section 2.6, printed pages 2-24 through 2-29: interrupt acceptance, priority, vector
  layout, saved FLAGS/CS/IP, cleared IF/TF, trap behavior and the inhibition
  after STI, IRET, MOV to a segment register and POP of a segment register;
- section 2.6, printed page 2-29 and table 2-4: reset to `CS=FFFFh`, `IP=0000h`, other
  segments zero, flags clear and an empty queue; pages 2-29 through 2-30 cover
  HALT and queue reinitialization;
- section 2.7, printed page 2-48: HLT wake sources, WAIT/TEST behavior and ESC register
  no-op versus discarded memory read;
- section 2.7, printed pages 2-49 through 2-51, especially 2-50 and 2-51:
  instruction timing assumptions, prefetch caveats and the additional four
  clocks for every 8088 16-bit memory reference;
- section 4.2, printed page 4-1: 8-bit data bus, four-byte queue, byte fetches and BIU
  arbitration; table 4-13, printed pages 4-28 through 4-35: the original opcode and
  grouped-encoding map, including `60h`-`6Fh`, `C0h`/`C1h`, `C8h`/`C9h` and
  reserved grouped fields marked `not used`.

For the stored high FLAGS nibble, Intel Corporation, *Intel 64 and IA-32
Architectures Software Developer's Manual, Volume 3B: System Programming Guide,
Part 2*, March 2023 revision, order number 253669-079US, section 23.17.2,
records that bits 12 through 15 are always set on the 8086. The 1979 manual's figure 2-9 defines only the nine
8086/8088 status and control flags; the implementation therefore exposes no
NEC MD flag on the Intel model and canonicalizes only that documented Intel
image. These official documents define supported Intel behavior; their `not
used` cells are not treated as proof that original silicon rejected a byte.

### Observed silicon and retained corroboration

The non-published opcode decisions are not attributed to Intel documentation:

- Daniel Balsom, *A Hardware-Generated CPU Test Suite for the Intel 8088*,
  SingleStepTests/8088 version 2.0.0, generated in 2024, repository commit
  `aea84484abc79d09639d855b7b0ab32bc9e4dbeb`, `v2/metadata.json`. The recorded
  device is an AMD D8088, marking `60h`-`6Fh`, `82h`, `C0h`, `C1h`, `C8h` and
  `C9h` as aliases, `D6h` as undocumented, `F1h` as a prefix, and providing the
  grouped decisions used above. This is a physical 8088-family corpus from a
  licensed second-source part, not an Intel manual and not silently described
  as an Intel-marked specimen.
- Ken Shirriff, *Undocumented 8086 instructions, explained by the microcode*,
  July 2023, sections “Holes in the opcode table” and “Holes in two-byte
  opcodes”. This direct Intel 8086 die/microcode analysis explains `POP CS`,
  conditional-jump and return aliases, `F1` LOCK, `SALC`, `SETMO`, Group 3
  `TEST`, Group 5 `PUSH`, and all `82h` ALU subfields. Applying those EU/decode
  results to the bus-narrowed 8088 is an explicit inference, corroborated by
  the hardware-generated D8088 corpus.
- The pre-existing `src/cpu/808x_marty_86box.c` validation core already records
  the same original-8086/8088 aliases. It was reviewed as retained BluMach
  evidence only. No instruction body or timing implementation was copied from
  it into this portable core.

### Retained derived rewrite

The common register executor and effective-address machinery remain the
existing BluMach derived rewrite of the inherited BluMach/86Box Vx0 sources
listed in `provenance/components.json`. This change adds explicit immutable
Intel and NEC profiles plus independently sourced Intel reset, FLAGS,
interrupt, bus, queue and opcode decisions. It does not copy or import a new
8088 implementation from PCem, 86Box, MartyPC or another emulator.

V30 behavior, including NEC extensions, 8080 emulation mode, six-byte queue and
the documented NEC timing observer, remains covered by the existing tests and
retains its recorded authorship and GPL-2.0-or-later provenance.

### Physical-vector execution

`tools/run_8088_conformance.py` runs the portable Intel model against the
external SingleStepTests/8088 V2 corpus without copying that corpus into this
repository. The adapter accepts only the pinned metadata described above and
starts `portable-engine-808x-vector-runner` explicitly in `intel-8088` mode.

The gate executes both empty-queue and preloaded-queue vectors and compares
defined registers, masked FLAGS and changed memory. A separate versioned
prefetch-state contract installs the physical corpus bytes without treating the
queue as architectural register state. Raw final queue contents are now
measured and reported separately. This is not yet a fidelity percentage: the
hardware corpus ends when the first byte of the following instruction is read
from the queue, whereas `bm_808x_step()` returns after the current instruction.
The raw comparison is diagnostic while those boundaries differ; the deliberately
named `--require-raw-final-queue` option exists only for focused local work.

The component also exposes a versioned, read-only observation for each active
external-bus phase: `T1`, `T2`, `T3`, zero or more `Tw`, and `T4`. It reports
the logical transaction and the point at which the device response becomes
available. This is evidence plumbing, not an assertion of complete CPU timing:
it observes fetch and operand transfers already performed by the BIU, but does
not synthesize EU-only idle (`Ti`) clocks, queue-status pins, READY sampling
edges or instruction-boundary durations. Physical cycle traces therefore
remain not yet compared.

The separate `bm_8088_queue_event_t` observer records logical queue reads:
`READ_FIRST` for each prefix and the effective opcode, `READ_SUBSEQUENT` for
ModR/M, displacement and immediate bytes, and `FLUSH` on a guest control-flow
queue invalidation. Its CS:IP and queue counts make event order testable with
both empty and preloaded queues. Host reset and explicit test-state import do
not emit guest queue events. These callbacks are deliberately not Intel QS pin
samples: in the physical corpus QS reports the preceding cycle's operation,
whereas this observer has no cycle position or `Ti` states. Nor is logical
`FLUSH` assumed to be identical to every physical `E` status.

For the pinned AMD D8088 corpus, `04`, `82.0` and `90` were each run through
10,000 vectors. Architectural registers and changed RAM matched in all
30,000, including 15,000 initially preloaded queues. The logical `F`/`S`
read-kind and byte sequences also matched all 30,000 with
`--require-queue-reads`. Raw final queues matched 14,324/30,000, but this
number is not a fidelity score because the compared boundaries differ.

The bus-phase observer now also feeds an operand-transfer comparison in the
external test runner. For the same 30,000 vectors, the ordered non-CODE
transfers matched 30,000/30,000 by operation, 20-bit physical address and
data byte with `--require-operand-bus`; 7,552 of those vectors actually
contain an operand transfer. The hardware adapter latches the
address at `T1` and takes data at `T3` (or the final `Tw`); a transfer already
visible at `T3` remains included when the capture stops before `T4`. This is
not a comparison of prefetch timing, `Ti`, READY edges, QS pin placement or
the complete CPU-cycle trace. The corpus still terminates at a different
instruction boundary from the portable step.

The next projection compares the observed `T1`/`T2`/`T3`/`Tw`/`T4` phase
order for non-CODE transfers. It matches all 30,000 sampled vectors (7,552
with operand traffic) using `--require-operand-phases`. The sole permitted
boundary normalization is an omitted final `T4` if the hardware capture ends
at `T3` after the data transfer; arbitrary phase mismatches are failures.
This checks active operand-bus sequencing, **not** full CPU-cycle placement:
the trace can begin partway through a CODE fetch and stops after the next
instruction queue read, while the functional step has no Intel `Ti` schedule.

The first clocked baseline was separately tested against the pinned physical
captures. Among the initial 30,000 selected vectors, 2,544 were full-queue
unprefixed NOP and 2,527 were full-queue unprefixed ADD AL,imm8. All 5,071
matched the Intel manual's 3/4-clock duration and the capture's absolute
CODE-phase positions with `--require-clocked-baseline`. Subsequent strict
work expanded the eligible full-queue cases to the 16 accumulator-immediate
ALU forms and `A0h`/`A1h` direct memory reads. The current pinned run of
190,000 selected vectors has 47,804/47,804 eligible strict clocked matches,
with no functional, logical queue-read, operand-bus or operand-phase failures.
The other vectors are **not** strict-clocked passes: their clocked boundary
remains unsupported. These results still do not establish cold-queue timing,
prefixed instructions, all operand contention, interrupts or continuous
multi-instruction timing.

Example, with the external corpus checked out at commit
`aea84484abc79d09639d855b7b0ab32bc9e4dbeb`:

```text
python tools/run_8088_conformance.py \
  --suite /path/to/8088/v2 \
  --runner /path/to/portable-engine-808x-vector-runner \
  --opcode 04 --opcode 82.0 --opcode 90 \
  --require-queue-reads --require-operand-bus --require-operand-phases \
  --require-clocked-baseline
```

### Inferred or unknown

- No complete Intel 8088 EU/BIU overlap schedule is claimed. Only the
  explicitly admitted full-queue forms above have exact clocked boundaries;
  the separate provisional mode has no cycle-fidelity claim.
- The active bus-phase observer is not electrical pin timing. READY sampling,
  queue-status pins, EU-only clocks and interrupt-acknowledge waveforms remain
  unmodelled.
- Grouped encodings classified `undefined` by the physical D8088 corpus remain
  unsupported. Their actual latch-dependent effects are `UNKNOWN` rather than
  being inherited from the NEC V30 or a later x86.
- `SETMO` result bytes are modeled from the die/corpus decision; its undefined
  carry, auxiliary-carry and overflow outcomes are preserved rather than
  claimed as verified flag values.
- Attached 8087 arithmetic is outside this CPU component. ESC exposes the
  documented opcode/operand observation contract; a null callback models no
  attached coprocessor, while WAIT without a TEST provider remains unsupported.

## Validation boundary

The independent synthetic suite covers reset and FLAGS images, the complete
primary classification map and grouped decisions, documented ISA, physical
aliases, absence of NEC 8080 execution, divide behavior, interrupts, HLT wake,
20-bit wrap, ordered byte memory and I/O transactions, ESC memory traffic,
four-byte queue capacity and flushes, and the explicit unknown clocked-step
result. No ROM, BIOS or machine media is required.
