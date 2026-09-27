# 80286 instruction timing: nominal foundation, not elapsed timing

## Sources and scope

Intel, **80286 and 80287 Programmer's Reference Manual**, order 210498-005,
1987, Appendix B. [Manufacturer manual preserved by Bitsavers](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf).
Reviewed local scan, PDF pages 214,235-238,240,243,286,312-314; printed pages
B-6, B-27..30, B-32, B-35, B-78, B-104..106. Tables and qualifications were
visually checked, not merely copied from OCR. Scan is outside Git.

New code, not copied from PCem, 86Box, MartyPC or another implementation.

## First implemented block

`bm_286_nominal_instruction_clocks` is a pure lookup of the documented nominal
cost of a successfully completed instruction. Exact instruction bytes and
length must come from the decoder, never an extra read of guest memory.

| Unprefixed instruction | Nominal native clocks | Intel printed page |
|---|---:|---|
| CBW | 2 | B-27 |
| CLC | 2 | B-28 |
| CLD | 2 | B-29 |
| CLI | 3 | B-30 |
| CMC | 2 | B-32 |
| CWD | 2 | B-35 |
| NOP | 3 | B-78 |
| STC | 2 | B-104 |
| STD | 2 | B-105 |
| STI | 2 | B-106 |

These entries describe successful execution, not a privilege fault (CLI/STI),
trap or interrupt delivery. Prefixes and other forms remain UNSUPPORTED.
Invalid or unsupported queries preserve output. No global state, allocation,
host dependency, default cost or CPU-state mutation.

## Deliberate separation from elapsed cycles

### Register/immediate block, 2026-09-27

Implemented additional unprefixed forms below. For ModRM encodings only
mod=11 (register operand) is accepted; all other modes remain unsupported.
Exact length is checked before success, including the immediate width.

| Forms | Nominal clocks | Intel printed pages |
|---|---:|---|
| ADD/OR/ADC/SBB/AND/SUB/XOR/CMP register-register | 2 | B-19,20,33,80,99,109,116 |
| Same families, accumulator-immediate and 80/81 register-immediate | 3 | same |
| 83 register-immediate ADD/ADC/SBB/SUB/CMP | 3 | B-19,33,99,109 |
| MOV general registers, immediate to general register (B0..BF,C6/C7 /0) | 2 | B-73 |
| TEST register-register / register-immediate | 2 / 3 | B-110 |
| INC/DEC register (short and FE/FF forms) | 2 | B-38,46 |
| NEG/NOT register (F6/F7 /3,/2) | 2 | B-77,79 |
| XCHG registers (86/87 and 90..97) | 3 | B-114 |

PDF pages227,228,241,246,254,281,285,287,288,307,317,318,322,324 visually
reviewed in the same immutable manual. The 83 /1,/4,/6 timing rows are absent
from the selected logical-instruction tables, so these queries remain
UNSUPPORTED pending corroboration. This is a timing-coverage limitation, not
an ISA-validity claim. No 82 alias, segment-register transfers, prefixes,
multiply/divide, adjust, shifts/rotates or memory forms are qualified here.
Subsequent register arithmetic additions are recorded below.

Tests exercise every ModRM byte for the supported families, all general
register encodings, byte/word immediate widths, all byte immediate values,
truncated/extra-byte encodings, seven prefix classes, memory rejection and
invalid group selectors. Inputs and outputs on failure remain unchanged.
This is validation of nominal tables, not SST cycle-trace certification.

### Register shifts and rotates, 2026-09-27

Added ROL/ROR/RCL/RCR/SHL(SAL)/SHR/SAR for mod=11, byte and word,
using Intel B-90/91 and B-97/98 (PDF298/299/305/306, visually reviewed).
D0/D1 count-one forms use 2 nominal clocks. C0/C1 immediate and D2/D3 CL
forms use 5 + (count & 31); this includes five baseline clocks for masked
zero and six for a variable encoding with count one. Do not reduce rotation
counts modulo operand width (or width+carry) when calculating these costs.

The existing query handles implicit/immediate counts. A companion
`bm_286_nominal_instruction_clocks_with_cl` accepts pre-execution CL, including
when CL/CX is itself the destination. Without that input, D2/D3 remain
UNSUPPORTED; CL is never invented as zero. Other forms ignore supplied CL.
These APIs return nominal table costs, not validated bus-trace durations.

Exhaustive tests cover six encodings x 256 ModRM values x 256 counters
(393216 combinations), including rejected memory and /6 forms, masked zero,
full rotations, byte/word widths and all register choices. Exact lengths,
prefix rejection, null inputs, output atomicity and immutable input tested.
CPU execution, flags and the diagnostic PCS286 clock are unchanged.

### Multiply, divide and decimal adjustments, 2026-09-27

Visually checked Intel B-15..18, B-36/37/39/43/44/76 (PDF
223..226,244,245,247,251,252,284). These tables give fixed nominal values,
not operand-dependent ranges. Do not import an 8086 timing algorithm.

| Successful unprefixed form | Nominal clocks |
|---|---:|
| MUL/IMUL register byte / word (F6/F7 /4,/5) | 13 / 21 |
| DIV register byte / word (F6/F7 /6) | 14 / 22 |
| IDIV register byte / word (F6/F7 /7) | 17 / 25 |
| IMUL register word with immediate byte or word (6B/69) | 21 |
| AAA/AAS/DAA/DAS | 3 |
| AAM decimal (D4 0A) / AAD decimal (D5 0A) | 16 / 14 |

Only register ModRM forms and the documented decimal base are qualified.
Nondecimal AAM/AAD, memory, prefixes and divide-error delivery remain outside
this coverage. The query has no operand values and cannot detect divide zero
or quotient overflow: the caller must only use it after successful execution.
The scan's IMUL opcode is **6B**, not the OCR's erroneous **68**.

Tests enumerate every ModRM for F6/F7, all ModRM/byte-immediate combinations
for 69/6B (including signed extrema and varying both word bytes), and all 256
AAM/AAD bases. Prefixes, incomplete/overlong encodings and complete memory
encodings are rejected; input and failure output remain unchanged.
No CPU semantics, elapsed clocks or board scheduling changed.

### Memory foundation: MOV, addressing and alignment, 2026-09-27

`bm_286_nominal_memory_clocks` adds a separate zero-wait memory query.
Intel B-6 and B-73 (PDF214/281) were visually rechecked. Supported successful
unprefixed forms: 88..8B, C6/C7 /0 and A0..A3, memory operands only.
Register-only queries keep rejecting memory, rather than assuming alignment.

The result separates instruction, addressing and alignment costs and their sum:

- MOV memory to general register: 5 nominal clocks.
- MOV general register or immediate to memory: 3 nominal clocks.
- Add 1 for base + index + encoded displacement, including displacement zero.
  ModRM mod=01/10 with r/m=000..011 qualifies. Direct disp16, a single
  register plus displacement and base+index without displacement do not.
- Add 2 for a word reference at an odd **physical** address. A byte reference
  has no alignment penalty. The caller supplies the already resolved 24-bit
  address; the offset's parity alone is insufficient with an odd segment base.

Exact ModRM/displacement/immediate lengths are checked, including the special
mod=00,r/m=110 direct disp16 and moffs16 accumulator forms. No translation,
memory read, access check or decode-time guest refetch occurs. Successful
completion remains a precondition; a query does not certify an access as legal.

Tests cover all 256 ModRM values for six encodings, three byte-fill patterns,
six physical addresses (27648 combinations), direct accumulator forms with
offset and physical parity deliberately different, seven prefix classes,
invalid groups, register rejection, truncation/extra bytes, null/empty inputs
and out-of-range addresses. Failure preserves the entire output structure.

This is the first part of the memory block, not complete memory timing.
Read-modify-write arithmetic, other memory instructions and segment transfers
remain unqualified. MOV deliberately has only one read or write, so this does
not settle the number of alignment penalties in a read-modify-write operation.
Wait states, fetch/write overlap and prefetch accounting remain separate work.

### Memory arithmetic and comparisons, 2026-09-27

Extended the memory query with ADD/OR/ADC/SBB/AND/SUB/XOR/CMP ModRM
forms, 80/81/83 immediate groups and TEST84/85,F6/F7 /0. Visually checked
Intel B-19/20/33/80/99/109/110/116 (PDF227/228/241/288/307/317/318/324).

Base memory costs: ALU7; CMP38/39=7, CMP3A/3B=6, CMP immediate=6;
TEST=6. Addressing terms are unchanged. Read-only odd-word references add2.
Logical83 /1,/4,/6 remain unqualified because the reviewed tables omit them.

Byte and aligned-word read-modify-write forms are now qualified. Odd-word
read-modify-write queries return UNSUPPORTED, preserving output: the general
B-6 wording alone is not taken as proof of whether to count one or both
operand references for alignment. No guessed +2/+4 rule is installed.
This is a timing-query limitation, not an unsupported CPU instruction.

Added19968 opcode/ModRM/parity combinations (39 forms x256 x2), including
CMP direction asymmetry, TEST without writes, immediate widths, register
rejection, missing logical83 rows and odd-word RMW rejection. Common checks
also exercise malformed lengths, prefixes and output atomicity.
Other memory families (including unary, exchange, multiply/divide and shifts),
segment moves, prefixes, waits and elapsed timing remain pending.

### Memory unary, multiply/divide and exchange, 2026-09-27

Visually checked B-38/39/43/44/46/76/77/79/114 (PDF
246/247/251/252/254/284/285/287/322). Added memory forms:

| Form | Nominal base clocks |
|---|---:|
| MUL/IMUL byte / word | 16 / 24 |
| DIV byte / word | 17 / 25 |
| IDIV byte / word | 20 / 28 |
| IMUL69/6B word memory source, immediate word/byte | 24 |
| INC/DEC/NEG/NOT | 7 |
| XCHG | 5 |

Existing effective-address and alignment terms apply. MUL/IMUL/DIV/IDIV
read memory but write registers, so odd-word sources use the single-read
alignment term. Immediate IMUL6B still has a word memory operand.
Divide-error delivery is not covered. Unary and XCHG are read-modify-write:
byte and aligned-word forms supported; odd-word forms stay unqualified.
XCHG's implicit bus lock is not modeled as an extra invented nominal cost;
bus arbitration belongs to the future elapsed model. Explicit prefixes remain
unsupported by the lookup, not by functional CPU execution.

12288 new encoding/ModRM/parity/fill cases (8 x256 x2 x3) cover groups,
widths, immediate lengths, signed boundary byte patterns, memory modes and
refused forms. Existing tests exercise malformed lengths, prefixes and
atomic failure output. Strict clocked execution and machine scheduling unchanged.

### Memory shifts and rotates, 2026-09-27

Visually rechecked Intel B-90/91/97/98 (PDF298/299/305/306). Memory
ROL/ROR/RCL/RCR/SHL(SAL)/SHR/SAR use7 nominal clocks for D0/D1 and
8+(count&31) for C0/C1/D2/D3. The variable form with masked zero still
has baseline8; variable count1 costs9, not7. No operand-width or carry-width
modulo is applied to the timing count. Addressing surcharge remains separate.

The memory query handles implicit/immediate counts. Its new companion
`bm_286_nominal_memory_clocks_with_cl` accepts pre-execution CL. Without CL,
D2/D3 remain unsupported. Other instruction forms ignore CL. Immediate count
is read after the complete displacement, never from a fixed ModRM-relative
offset. Only already captured bytes are used; guest memory is not fetched.

Tests enumerate6encodings x256ModRM x256counts x2physical parities =786432
cases, including rejected register,/6 and odd-word forms. Displacement layouts,
masked zero, full rotations, widths, missing CL, irrelevant CL, bad lengths,
prefixes, invalid inputs and unchanged error output are checked.
Odd-word RMW remains unqualified even for zero counts: no bus behavior is
inferred from unchanged flags or result. No functional CPU or clock change.

### Segment MOV table bases, 2026-09-27

Intel B-73 (PDF281, visually rechecked) distinguishes register and memory
forms and real/protected mode. New `bm_286_nominal_segment_base_clocks`
requires an explicit mode and returns **only the table base**, unlike the
memory query's adjusted result:

| Successful MOV | Register | Memory |
|---|---:|---:|
| 8C: ES/CS/SS/DS to r/m16, either mode | 2 | 3 |
| 8E: r/m16 to ES/SS/DS, real mode | 2 | 5 |
| 8E: r/m16 to ES/SS/DS, protected mode | 17 | 19 |

No EA or alignment terms (explicit operand or implicit descriptor), waits,
fault delivery, null-selector shortcuts, descriptor accessed-bit variations
or interrupt-shadow timing are qualified by this table query. Protected loads
require successful non-null selection as a caller precondition. It does not
read selectors or CPU state and must not feed elapsed clocks directly.
Reading CS is valid here; loading CS with MOV remains rejected, as do /4..7.
Tests cover1024 opcode/ModRM/mode combinations, exact displacement lengths,
truncation, extra bytes, seven prefix classes, invalid mode/arguments and
unchanged failure output. Existing generic APIs still refuse segment moves.

Prefix review of Appendix B did not establish a general segment-override
cost. LOCK and REP have their own rules; no universal prefix surcharge or
blanket prefix stripping was added. Prefix timing remains pending.

Intel B-6 distinguishes nominal execution cost from instruction supply and
memory access delays. Fetch and write waits can overlap execution, so adding
every recorded bus wait to an opcode-table cost is not a justified elapsed
timing model. Alignment and effective-address terms also need explicit handling.
The manual's aggregate fetch-overhead discussion is not a universal multiplier.

This first block therefore does NOT change `bm_286_boundary_t`, mark execution
DOCUMENTED, activate `bm_286_step_clocked`, or replace the board's diagnostic
1us step. The public elapsed-time contract stays strict. No performance or
physical-frequency improvement is claimed from the lookup itself.

### Bus evidence review, 2026-09-27

The Intel 80286 Hardware Reference Manual (210760-002, 1987), printed
3-24, 3-65 and 4-4 (PDF74,115,136), was visually checked.
[Manufacturer manual](https://bitsavers.trailing-edge.com/components/intel/80286/210760-002_80286_Hardware_Reference_Manual_1987.pdf).

Documented distinctions:
- Table3-3 describes bus priorities and split odd-word transfers, not a
  universal instruction surcharge.
- Table3-5 gives HOLD latency in **system clocks**, not instruction execution
  time; two system clocks equal one processor clock. Its locked memory
  rotate and XCHG expressions distinguish four odd-address data transfers
  from two aligned transfers. Their zero-wait difference must not be installed
  as a universal instruction penalty.
- Section4's benchmark explanation says data-read waits stall execution,
  whereas buffered writes can overlap it. Later reads, writes and prefetch
  may nevertheless be delayed. This requires bus state, not blind addition
  of transfer durations to an opcode-table base.

Observed hardware trace check: pinned SST286 revision
37c73caf53dcd22d3dd369ff09305d13d117a4fe, Harris N80C286-12,
v1_real_mode/01.MOO.gz. Input, upstream decoder and revocation list hashes
matched tests/data/sst286-lock.json. Of5000 ADD tests,1024 prefixed,
965 register and40 exception cases were excluded; no revoked case was selected.
Counting MEMR/MEMW at Ts in the remaining2971 traces:
1465 even-address cases each had one read and one write;
1506 odd-address cases each had two reads and two writes.
This confirms bus transaction shape for this corpus, not Intel/Harris timing
equivalence or an instruction-duration delta. The terminal HLT and flushed
initial queue remain part of the capture.

Examples: test0 hash8a51328f572d633c6a0e804cb05cd6efce8b9899
reads/writes1029758; test2 hashfc3bb36488483532bd2074ee54929d28e40b1076
reads904349,904350 then writes904349,904350.
No elapsed model, odd-RMW cost, general prefix cost or runtime clock change
is justified by this check. Segment overrides, LOCK and REP must remain
separate cases. The next temporal harness must compare whole modeled traces
including setup/HLT and queue/bus state, not equate capture length to table cost.

### Reproducible capture inventory, 2026-09-27

`python tools/sst286_trace.py --corpus <local-cache> --file v1_real_mode/01.MOO.gz`
prints a JSON inventory of the complete pinned captures. Exit77 deliberately
means **no emulator temporal comparison was performed**, not a timing pass.
Evidence errors return2. The command verifies lock hashes and revocations;
it neither downloads nor executes guest code. Output contains capture sample
counts, T-state counts, first observed HALT and bus transaction start offsets.
These are not per-instruction clocks. Prefixed/exception captures are retained
and identified, not silently treated as successful unprefixed instructions.
No trimming at the first/last data access or subtraction of a guessed HLT cost.

The functional parser remains unchanged by default; raw cycle decoding is
explicitly opt-in. Authored tests cover bounded count validation, absent CYCL,
retained raw fields, transaction start counting, absent HALT, pinned inputs,
revocations and selection errors. The real ADD inventory reads5000 captures
and reproduces1465 aligned/two-transfer and1506 odd/four-transfer cases after
excluding1024 prefixed,965 register and40 exception cases. All5000 captures
contain a HALT status. This is capture validation, not CPU timing validation.

Next: produce modeled bus events from a separate elapsed model and compare
them against these captures with identical initial queue and capture boundaries.
The existing functional probe does not provide that stream; zero timing passes
must remain explicit until it does.

### CPU data-access shape comparison, 2026-09-27

The existing functional CPU already splits odd-word accesses. Rather than
duplicate that logic, the private SST probe now offers --data-bus, recording
actual non-fetch accesses during its single instruction. Its default functional
protocol is unchanged. This is not a physical prefetch or timestamp stream.

Run tools/sst286_trace.py --corpus <cache> --compare-add-data <pcs286-sst-probe>.
The separate mode selects only non-revoked, unprefixed, nonfaulting01h memory
cases and compares data address, read/write kind and ordering. It does NOT
compare values, widths, byte lanes, waits, prefetch or elapsed clocks. Exit0
means that limited comparison passed; the report explicitly says temporal
comparison not performed. CPU errors, including UNSUPPORTED, are failures.

Observed Debug result:2971/2971 matches against the pinned Harris corpus.
18 Python tests passed with the compiled probe, including synthetic aligned/
odd ADD, malformed transport, reversed access order and unsupported-probe
failure cases. The adapter CTest now includes these trace tests without
requiring external vectors. No CPU implementation, runtime or clock changed.
This replaces the earlier statement that the probe provides no data stream;
it still provides no timestamps or physical instruction-supply model.

### Experimental prefetch FIFO, 2026-09-27

Private prefetch_286.c/.h implements queue mechanics in isolation, not CPU
execution or elapsed timing. Intel210760-002 p3-24 was visually rechecked:
six-byte queue, requests with at least two free slots, odd physical branch
target fetched as a byte and subsequent requests as words. This is only the
byte FIFO, not the decoded-instruction queue or full BU/IU model.

Caller grants each request and supplies the completed bytes. Single-owner
state supports consumption during a pending fetch. Redirect discards queued
bytes and invalidates the pending ticket even at the same target; stopping
requests allows an already granted request to finish. These lifecycle choices
are explicit software policies, not claims about the exact HLT/snoop waveform.

Scope is a caller-validated contiguous physical window. A word crossing its
end is refused, not guessed into a byte or silently read beyond it. Protected
segment boundary behavior, real-mode beyond-limit prefetch, address wrapping,
descriptor validation, faults, LOCK/HOLD/PEREQ arbitration, two-clock EU
prefetch inhibition and elapsed phases remain unmodeled. No interpreter
fetch path, public ABI, strict clocked gate or machine clock changed.

Tests exercise32768 FIFO consumptions from even/odd starts, ring wrap,
capacity/backpressure, consumption while pending, duplicate/stale completion,
same-address redirect, stop, physical limit, unsupported terminal word,
serial exhaustion, isolation and unchanged state/output on rejection.

### Experimental ready-request priorities, 2026-09-27

Private bus_priority_286 selects ready classes in Intel210760-002 table3-3
order: locked transfer, split continuation, HOLD, processor extension,
EU data, prefetch. It does not interrupt a transfer already in progress or
grant a new transfer while an external master owns the bus. A locked sequence
reserves ownership even when its next transfer is not ready. Locked split
continuations must be classified as LOCKED by the future caller.

Prefetch inhibition is an explicit input, not guessed from the opcode. The
documented two-clock advance EU notice still needs a timed execution model.
The selector has no pin callbacks, durations, Ts/Tc phases, READY sampling,
HOLD-release handshake or production CPU integration. It is not a complete
arbiter.512 combinations and a synthetic grant/FIFO sequence are tested;
invalid masks leave output unchanged. Added missing stddef.h to make the
private FIFO header explicitly declare its size_t dependency.

Remaining order: physical phase/wait state machine; EU/prefetch integration
and segment boundaries; matched hardware capture timing; broader opcode,
control-flow, interrupt/protected-event coverage; only then scheduler activation.
Nominal lookup coverage alone never qualifies the whole CPU clock.

### Bus phases, experimental CPU integration and negative timing evidence

Intel210760-002 printed3-25/PDF75 and3-32/PDF82 visually checked2026-09-27.
The bus phase state machine now implements Ts followed by Tc; normalized
inactive READY extends Tc, active READY completes it. One step is a processor
clock, not a system CLK half-cycle. Transfers may be back-to-back. A fixed-wait
O(1) completion path is tested against stepwise execution for0..4096 waits.
Both paths reject overflow; neither models electrical setup/hold or the82284.

A separate single pending-write slot snapshots address/data and refuses a
second pending write. Its issue operation requires an explicit grant; active
transfer storage belongs to the caller. This models queue ownership, NOT the
exact time at which the EU can retire a write. Production CPU writes remain
synchronous; write overlap is still not integrated.

The manual resolves the earlier protected-tail uncertainty: an even-addressed
last segment byte is fetched as a word and the additional byte discarded.
The new explicit protected-window queue entry point implements this for a
contiguous physical window. It does not raise a segment exception on prefetch;
the executing CPU must still check its instruction limit. Real-mode overrun
and physical/segment wrapping remain outside this adapter's scope.

fetch_supply_286 is a PRIVATE EXPERIMENT joining real CPU access callbacks,
the FIFO and serialized bus phases. It supports explicit redirects and fixed
endpoint waits. Its counter measures BUS OCCUPANCY, not CPU elapsed time.
It has no EU/IU schedule, automatic branch/reset interception, asynchronous
write retirement or production runtime activation. Refills are demand-driven
unless a harness explicitly requests one. Source effects happen synchronously.
Never use its counter to replace cpu_cycles or feed bm_286_step_clocked.

Synthetic programs run through both ordinary and adapted access paths in
real and protected mode with identical registers and memory, including MOV,
ADD, store, JMP+0 (explicit flush even though target is sequential) and HLT.
Additional tests cover source failure/recovery, debug isolation, protected
tail discard and disallowed implicit redirects.

The private SST probe offers --supply (functional response) and
--supply-data-bus (data address/kind/bus-clock response). It executes one
instruction, not the complete capture's terminal HLT. Its trace storage is
heap allocated to avoid exceeding small host stacks.

Run tools/sst286_trace.py --corpus <cache> --compare-supply <probe>.
This is a falsification experiment, not a cycle-accuracy pass: it compares
functional state and data-event spacing relative to the first data event.
It deliberately does not compare capture origins or infer total instruction
time. Errors and spacing differences return exit1; even all relative matches
would return77 rather than certify CPU elapsed timing.

Observed pinned Harris ADD results:2971 functional matches,2971 address/order
matches,2971 relative-spacing mismatches. For1465 aligned cases the hardware
data start offsets relative to first read are[0,4], whereas serialized access
predicts[0,2]. For1506 odd cases hardware gives[0,2,6,8], predicted[0,2,4,6].
All8954 selected hardware data transfers have one Ts and one Tc (zero waits).
This falsifies serialized bus occupancy as instruction time. No universal
two-clock patch or frequency multiplier was installed to conceal the missing
EU delay. These observations describe this ADD corpus/CPU only.

As checked2026-09-27, upstream SingleStepTests/80286 still describes protected
and unreal-mode captures as future work. Current captures do not exercise
IF/TF or wait states; synthetic/documentation tests cannot be labeled hardware
timing validation for these cases. Source: https://github.com/SingleStepTests/80286

Current requested milestones: (1) digital phase/wait primitive implemented,
but no electrical/pin-system claim; (2) integration demonstrated in a harness,
not a complete scheduled EU/runtime; (3) real negative comparison available,
not timing qualification. Remaining work is substantial and must not be marked
complete because component tests pass: resumable EU/IU scheduling, prefetch
opportunity/inhibition and write overlap, control/event paths and explicit
hardware evidence or an explicitly approximate timing contract.

## Accepted provisional execution policy (2026-09-27)

Cycle-exact execution is not a release prerequisite. Prefer the applicable
manual tables and document approximations instead of blocking functional work.
`bm_286_step_provisional` executes the existing functional step exactly once,
observes its original fetch responses without extra bus reads, and returns a
separate estimate. Known forms use nominal manual clocks plus serialized waits;
this composite is PROVISIONAL, not a validated pipeline duration. Real-mode
segment forms are explicitly marked base-only. Protected segment forms remain
pending because the nominal query's descriptor preconditions are not proven.

For unqualified forms, a caller may explicitly supply a diagnostic fallback.
It is reported as DIAGNOSTIC_FALLBACK, never as a manual value. A zero fallback
leaves the estimate pending without preventing execution. Actual execution
errors remain errors; arithmetic overflow leaves the report pending without
inviting instruction replay. Prefixes, events and missing forms require further
coverage. Existing boundaries and the strict clocked API are unchanged.

Synthetic real/protected tests compare state and access counts against the
ordinary interpreter, including missing timing, overflow and callback failure.
The experimental PCS286 runtime now opts into these estimates at nominal
12 MHz. Peripherals/video consume the same elapsed estimate with a carried
fractional remainder. Missing CPU forms and DMA/refresh scheduling keep an
explicit diagnostic quantum of 12 clocks. Manual/fallback counters are exposed
through machine inspection. This integration claims neither better host speed
nor hardware timing equivalence; the strict clocked API remains unchanged.

## Validation and next blocks

The test covers all 256 one-byte values, supported forms and explicit rejection
of incomplete/unsupported encodings. Also tests null/empty input, extra bytes/prefixes, immutable
input and unchanged output on error. Existing CPU timing tests still require
UNKNOWN and refusal by the strict clocked entry point.

1. Extend nominal forms: register/immediate and ModRM arithmetic/moves, then
   address/alignment terms, branches and REP, with a source for each rule.
2. Introduce an explicit provisional elapsed model with fetch/bus accounting;
   do not silently redefine nominal costs as certified hardware durations.
3. Extend the existing functional SST286 runner with a separate temporal
   comparison. Pin corpus revision and revocations; include test setup and
   terminal HLT rather than treating trace length as one instruction cost.
4. Validate interrupts, protected transfers, tasks and waits separately before
   connecting a sufficiently covered elapsed model to the PCS286 clock.

[SingleStepTests/80286](https://github.com/SingleStepTests/80286) supplies hardware
traces but starts with a flushed queue and records through terminal HLT. Its
current real-mode corpus has no inserted wait states and does not replace
protected-mode timing coverage. The limited negative comparison above is not
complete temporal qualification.
