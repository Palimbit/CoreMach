# BluMach portable engine

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: foundation for `0.2.0-dev`.

The proposed multi-clock execution model and its current evidence boundary are
tracked in [Virtual time across CPU architectures](virtual-time.md).

The portable engine is developed beside the inherited product and does not call
into its global state, configuration, device registry or user interfaces. The
two implementations can be enabled independently with
`BLUMACH_BUILD_LEGACY` and `BLUMACH_BUILD_ENGINE`.

## Dependency direction

```text
optional frontends
        |
        v
opaque runtime sessions
        |
        v
engine scheduler and CPU contract <--- reusable bus components
        |
        v
capability-based host services <--- null, Windows, POSIX, Circle, MorphOS
```

The engine and runtime are C11. They do not depend on Qt, SDL, native windowing,
host threads, C++ exceptions or a particular byte order. A frontend may use
those facilities without exposing them to an emulated component.

## Foundation invariants

- Every mutable value belongs to an engine, session, machine or component.
- A session has an explicit lifecycle and supports safe partial cleanup.
- Time uses integer ticks and same-time events use insertion order.
- Every machine definition declares the rate of its current scheduler tick
  domain so a frontend never substitutes CPU crystal frequency for engine
  time. The rate is pacing metadata, not a claim of cycle accuracy.
- CPU implementations receive a bounded budget and report consumed time.
- Buses model memory, I/O, program and data spaces independently.
- Debug and initiator-held lock state are explicit transaction attributes, not
  second hidden buses or host-specific callbacks.
- Host allocation, time and logging arrive through explicit capabilities.
- Storage components consume caller-owned block media; host paths and file
  handles stay outside the engine.
- Frontends submit stable physical-key events; guest machines own scan-code and
  controller-protocol translation.
- The null platform makes tests and GUI-free targets first-class builds.
- Every production file is covered exactly once by the provenance manifest.

The synthetic test machine has four instructions, two registers, memory, I/O,
halt, interrupt, a timer, event capture and register introspection. It is test
equipment and must never be exposed in the historical catalogue.

## Olivetti PCS 86 boundary

The canonical research identifies the PCS 86 processor as an NEC V30 at
10 MHz. The first real CPU target will therefore be an 808x-family interpreter
with the required V30 behaviour, not a target named or constrained as a pure
Intel 8088. No inherited CPU implementation is moved until its exact source
paths, commit, notices and adaptation method can be recorded.

The PCS 86 vertical slice starts only after this foundation is green. Its first
stage supplies firmware as a caller-owned blob, establishes reset execution and
memory transactions, and records instruction checkpoints without adding ROMs
or machine media to Git.

### PCS86-1 bring-up status

The first real-machine cut now models the documented NEC V30 at 10 MHz, 640 KiB
of conventional RAM and two caller-supplied 32 KiB firmware halves interleaved
at `F0000h-FFFFFh`. The machine validates the known firmware hashes when a
frontend supplies them, but test firmware may omit a hash so repository tests
can use newly authored synthetic bytes.

The explicit-state interpreter implements the documented V30 native and 8080
instruction maps at instruction-boundary functional granularity. Every primary
opcode and grouped encoding is classified by an executable synthetic matrix;
unknown or reserved forms return `BM_STATUS_UNSUPPORTED` and are never silently
treated as no-ops. A versioned optional observer now reports the timing facts
known for each completed architectural boundary, while one scheduler tick still
represents one completed instruction until that contract is sufficiently
complete to drive machine time.

The component also exposes a versioned architectural snapshot and a
single-boundary step operation. These contracts contain registers, segments,
IP, FLAGS, halt state and the maskable-interrupt inhibit that remains after
`EI` or a segment-register transfer, but deliberately exclude bus pins, trace
bookkeeping and host callbacks. They allow external conformance tools to
establish an arbitrary documented state without adding test-only globals or
host services to the interpreter.

`tools/run_v20_conformance.py` can stream an explicitly supplied
SingleStepTests/V20 corpus through the manual vector runner. The corpus remains
outside BluMach and its cycle and queue traces are ignored: native V20 results
are evidence for the architectural ISA shared with V30, not for the V30's
16-bit bus timing. The initial D4h/D5h campaign passes all 20,000 hardware
vectors and records two NEC distinctions: `AAM 00h` produces `AH=FFh` without
interrupt zero, while `AAD` always uses decimal base ten regardless of its
encoded second byte.

The next native-ISA campaign raises the verified total to 250,000 hardware
vectors. It covers the previously absent byte and accumulator forms of ADC and
SBB, DAA/DAS/AAA/AAS, CWD, INT3, INTO and all eight operations of the NEC 82h
byte-immediate alias. In particular, decimal and ASCII adjustment follow the
measured NEC flag and carry thresholds rather than assuming the behaviour of a
different x86 generation. Undefined flags are excluded with the corpus masks;
the interpreter still gives them a deterministic internal value.

The complete F6h/F7h campaign raises that total to 410,000 hardware vectors.
Both TEST encodings, NOT, NEG, unsigned and signed multiply, and unsigned and
signed divide now cover their byte and word forms. Divide errors enter vector
zero only after consuming the full instruction and leave the dividend intact;
the external adapter masks architecturally undefined saved-FLAGS bits without
weakening comparisons of the saved return address or other memory. The measured
NEC byte-IDIV boundary also raises divide error for a quotient of exactly -128.
Non-string REP prefixes are accepted as observed by the hardware corpus instead
of turning an otherwise implemented instruction into an unsupported opcode.

The first 80186-compatible V30 cut adds PUSHA/POPA, sign-extending and word
immediate PUSH, and the two three-operand immediate IMUL forms. It also closes
the conformance matrix for all 48 primary ADD/OR/ADC/SBB/AND/SUB/XOR/CMP
encodings. This stage exercises 540,000 hardware vectors without discrepancies,
460,000 of them newly covered, for an accumulated 870,000-vector campaign.

The next 80186-compatible cut implements the NEC-native CHKIND/BOUND,
full-byte-count immediate rotate/shift groups, PREPARE/ENTER and
DISPOSE/LEAVE. These semantics follow NEC's published instruction manuals:
bounds are signed, an out-of-range check enters BRK 5 after consuming the
instruction, and neither the immediate shift count nor the PREPARE lexical
level is truncated to five bits. Opcode 63h and immediate-shift group `/6` are
explicitly rejected because NEC marks those encodings undefined; they are not
accepted as aliases or no-ops. Synthetic tests cover the documented forms and
the rejected encodings. The pinned physical V20 corpus currently has no vectors
for 62h, 63h, C0h, C1h, C8h or C9h, so this cut records manufacturer-manual
evidence rather than claiming hardware conformance for those opcodes. The
primary references are the [October 1986 NEC V20/V30 User's
Manual](https://www.dosdays.co.uk/media/nec/NEC-V20-V30-Users-Manual.pdf) and
NEC's later [16-Bit V Series Instruction User's
Manual](https://datasheets.chipdb.org/NEC/V20-V30/U11301EJ5V0UMJ1.PDF).

The following 80186-compatible cut adds the NEC INM/OUTM instructions exposed
by the usual INSB/INSW/OUTSB/OUTSW opcodes. Input always targets ES:DI and
ignores segment override prefixes as documented; output reads DS:SI by default
and accepts the selected source-segment override. DX remains fixed across REP,
DF selects index increment or decrement, a zero repeat count performs no bus
access, and word I/O uses ordered byte transfers at DX and wrapping DX+1. A bus
failure preserves the progress of completed iterations but does not advance
the failing iteration. This remains functional instruction-domain behavior:
the current string executor does not yet expose V30 bus-cycle timing.

The prefix/interrupt cut implements the maskable interrupt-disable windows
documented for the V30. A pending maskable interrupt is accepted only after the
instruction following `EI`, `MOV` to or from a segment register, `POP` to a
segment register, or the segment half of `LES`/`LDS`. Prefix bytes and their
effective instruction remain one acceptance unit. Repeated memory and I/O
operations can accept an interrupt only after a completed iteration; the saved
return address points to the retained prefix sequence, `CX` and the indexes
preserve completed progress, and `IRET` resumes the remaining iterations. The
V30 retains at most three prefixes in that return address, which is covered
synthetically.

The later interrupt-priority cut adds a distinct rising-edge NMI input and the
BRK/single-step trap controlled by PSW bit 8. NMI is latched while acceptance is
blocked, ignores IE, wakes HALT and has priority over maskable INT, which in turn
has priority over a pending single-step trap. Interrupt entry clears IE and BRK;
an instruction which enters a synchronous software interrupt does not also
schedule a single-step trap. The version-3 architectural snapshot preserves the
NMI and trap latches plus two explicit shadows: `EI` delays maskable INT only,
while segment-register transfers also defer NMI and single-step recognition
through the following instruction. Non-locked repeated blocks may accept NMI
after a completed iteration and restart at their retained prefixes; BUSLOCK
keeps the edge latched until the complete repeated block ends. Prefetch state
and physical cycle timing remain explicit later work.

The PSW cut canonicalizes the V30 status-word image at every public state
boundary: bits 14-12 and 1 read as one, and reserved bits 5 and 3 read as zero.
Reset starts with MD one and its write gate disabled, so `POPF` and `IRET`
preserve native mode. `BRKEM` clears MD and enables that gate; an interrupt or
`CALLN` can then enter native mode and `IRET` may restore MD zero. `RETEM`
restores the native frame and disables MD writes again. Architectural snapshot
version 4 preserves both MD and its write gate, and rejects the impossible
combination of emulation mode with the gate disabled.

The emulation-mode cut executes the complete documented NEC V30 8080 opcode
map through the same portable state and buses. A maps to AL; BC, DE and HL map
to CW, DW and BW; the emulated stack pointer maps to BP. Instruction fetches
use PS:PC, while data and the emulated stack use DS0; the native SP/SS pair
remains available for mode transitions and interrupts. The low PSW flags map
directly to the 8080 flags, while native-only registers and high control flags
remain inaccessible to 8080 instructions. Memory, stack and immediate I/O
operations use the existing bus contracts, with no global interpreter state.

`BRKEM`, `CALLN`, native interrupt entry, `IRET` and `RETEM` are covered as
round trips. An asserted INT releases emulation-mode HLT even with IE clear;
with IE set it enters the native interrupt path. Undefined holes in the NEC
8080 map, undefined Group 0 extensions and nested `BRKEM` return
`BM_STATUS_UNSUPPORTED`; undocumented Intel 8080 opcode aliases are not
silently adopted. This is an instruction-boundary functional implementation:
8080/V30 cycle counts, prefetch, bus-status pins and electrical timing remain
outside this cut.

The native-map closure test executes all 256 primary bytes with a valid form
where the byte introduces a group or prefix, then exhausts the NEC Group 3 map
and every operation field of the immediate, shift, Group 1, Group 2, segment
transfer and reserved-field families. NEC-defined primary holes `63h`, `D6h`
and `F1h`, V33A/V53A-only `BRKXA`/`RETXA`, memory-only register encodings and
reserved group fields remain explicit unsupported results. Two deliberately
documented exceptions are retained from the pinned physical V20 corpus rather
than inferred from the manual: the second F6h/F7h TEST encoding and the
register-count shift `/6` behavior. This closes opcode classification; it does
not claim exhaustive physical conformance for every operand value or replace
the remaining cycle, prefetch and bus work.

The timing contract keeps three quantities separate. Documented execution-unit
clocks are classified for native register, immediate, memory, branch,
control-transfer, flag and I/O forms whenever Table 2-8 provides a value that
the current instruction boundary can determine exactly. Memory forms use the
decoded effective-address parity for the V30's documented odd-word penalty;
stack forms retain the entry SP so caller cleanup cannot corrupt that decision,
and counted shifts use the actual count. Version 3 classifies each result as
`UNKNOWN`, `EXACT` or `RANGE` and reports inclusive minimum and maximum clocks.
The documented V30 intervals for signed and unsigned multiply, immediate
signed multiply, signed divide, `CWD`, interrupting `CHKIND`, and `INS`/`EXT`
bit fields are therefore preserved without inventing a representative value;
a range is evidence, not a scheduler-ready exact time. Exact formulas now
cover non-interrupting `CHKIND`, `PREPARE`, packed-BCD strings, NEC bit
operations, nibble rotations and `BRKEM`. For an odd BCD digit count, the clock
formula uses the actual even-sized packed-byte operation documented by NEC and
implemented by the core. Native memory and I/O strings use the actual completed
iteration count plus their entry SI, DI and I/O-port alignment. NEC's primitive
formula already includes one repeat prefix, so only additional prefixes add the
documented two-clock prefix cost. Zero-count repeats and conditionally shortened
`CMPS`/`SCAS` runs are therefore exact, while a fragment that accepts an
interrupt remains explicitly unclassified rather than folding interrupt entry
into the instruction formula. `POLL` and all 8080-mode timings remain
unclassified.
Successful transactions through the portable memory and I/O bus are counted
separately, including wait states reported by mapped devices, without calling
their sum the elapsed instruction time. Timing-observation version 20 reports
bus occupancy, the subset spent on demand prefetch, one queue-read clock per
consumed instruction byte, successful prefetch transactions, BCU phase clocks
and the next prefetch phase. It also exposes a separately classified complete
boundary duration. That duration is available when every bus transaction in a
native instruction boundary belongs to prefetch and, from version 9, for the
direct and DX-addressed `IN`/`OUT` forms. Version 10 also places every native
prefix before the following instruction byte is decoded, so those I/O forms
remain complete when prefixed. Version 11 adds the four direct
accumulator-memory `MOV` forms, including even and odd word transfers, and
`XLAT` after its inherited three internal clocks. Version 12 adds memory forms
of the four byte/word, load/store ModR/M `MOV` opcodes. Version 13 adds all
read-only ModR/M ALU memory forms, including `CMP` in both encoding directions.
Version 14 adds the read-modify-write ALU forms.
Version 15 adds ModR/M `TEST` and `XCHG` memory forms.
Version 16 adds the immediate ALU groups `80h`-`83h`.
Version 17 adds segment-register `MOV` memory forms and the immediate-to-r/m
`MOV` groups `C6h`-`C7h`.
Version 18 places Group 3 `F6h`-`F7h` memory operands. Exact `TEST`, `NOT`,
`NEG` and successful `DIV` forms can complete their timelines; multiply and
signed-divide intervals remain explicitly ranged where the documentation is
ranged.
Version 19 places byte and word `INC`/`DEC` memory forms in groups `FEh` and
`FFh`; the other `FFh` indirect control and stack forms remain unresolved.
Version 20 places the single-word stack transfers used by register, segment,
flags and immediate `PUSH`/`POP` forms, including odd-stack splits.
Version 21 places byte and word `STOS` and `SCAS` transfers. Repeated forms
retain the inherited setup and per-iteration ordering, zero-count repeats have
no operand transaction, and odd words retain both physical byte transfers.
An interrupted repeat fragment remains unclassified.
Version 22 places relative near `CALL` and both near `RET` forms. It models the
inherited prefetch suspension separately from ordinary internal clocks, flushes
at the actual control-transfer point and resumes target prefetch before or
after the stack transfer in the inherited order.
Version 23 places byte and word `MOVS` and `LODS` source transfers. It covers
normal, repeated, zero-count and segment-overridden forms while preserving both
physical transfers of an odd word.
Demand-fetch stall clocks
(including their waits) and queue-read clocks are added to the documented EXU
interval, while speculative prefetch phases remain overlapped. A placed I/O or
memory operand contributes its four base clocks per physical bus transaction
inside that EXU interval; device waits and prefetch handoff stalls extend the
complete boundary outside it.
Exact execution
times therefore produce exact boundary times and documented execution ranges
remain ranges. Other operand-memory and I/O traffic leaves the boundary
duration `UNKNOWN` until its position relative to an already-running prefetch
is represented. The observer explicitly reports whether that placement is
complete, how many documented EXU clocks were placed and the operand-only wait
extension. The underlying counters remain resource and timeline observations,
not a serialized sum: BCU and EXU work overlap. The queue, independent PFP and
resource accounting
now live in one private, instance-owned BCU component rather than as scattered
interpreter fields. Its direct tests cover queue wrap, flush and boundary
accounting. Demand fills now execute explicit T1/T2/T3/Tw/T4 phases, perform
the portable bus access in T3 and publish fetched bytes to the queue in T4.
An in-flight speculative fetch whose queue is empty when the next instruction
needs a byte is reclassified as demand for its remaining phases, including
when its T3 transaction completed in the preceding boundary.
Every successful instruction-queue read now advances this same BCU by its one
documented predecode clock. The byte is removed first, so the newly freed queue
space may start a fetch; an in-flight fetch instead advances one phase. This is
real overlap, not an additional serialized bus delay. Instruction fetches also
remain ordinary bus cycles under `BUSLOCK`; only the instruction's operand
transfers carry the locked transaction attribute.
For native instructions whose NEC execution time is exact, which do not flush
the queue and which perform no operand or I/O transfer, those phases progress
concurrently for the documented EXU clocks and persist across instruction
boundaries. Aligned V30 word memory
operands, stack/vector transfers and word I/O at an even port use one
little-endian 16-bit bus transaction; an odd memory word or I/O port still uses
the two byte cycles required by the hardware. Every memory and I/O transaction
now acquires that same BCU instead of calling the portable bus directly. If a
prefetch is already in T1-T4, it completes before the operand begins its own
T1/T2/T3/Tw/T4 transfer. Version 7 reports operand transaction clocks and the
prefetch clocks spent handing over the bus separately. Version 9 ports the
inherited execution ordering for the eight native `IN`/`OUT` opcodes: internal
EXU intervals advance prefetch, each operand transaction occupies its four base
clocks, and any remaining documented clocks resume prefetch. Version 10 places
each prefix's documented execution interval immediately after its queue read;
this may finish a prefetch before a later operand requests the bus. Version 11
places direct accumulator-memory `MOV` after its two-byte address, preserves
the inherited internal clock before a store, and places `XLAT` after its three
internal clocks. Version 12 places ModR/M `MOV` memory loads after three
internal clocks and stores after five, while leaving register forms on the
bus-free path. Version 13 places byte ALU reads after two internal clocks,
ordinary word ALU reads after one, and both byte/word `CMP` reads after two,
as inherited. Version 14 places the read-modify-write forms at those same
read offsets and preserves the four inherited internal clocks between the read
and write. Version 15 places `TEST` after two internal clocks and advances two
more after its read; it places `XCHG` after two internal clocks and preserves
the five-clock interval between its read and write. Version 16 restores the
inherited ordering of an immediate ALU instruction: read the ModR/M operand,
consume the immediate, advance one internal clock for the operation and two
more before a possible write. Version 17 places a segment-register load after
two inherited internal clocks and a store after three. Immediate-to-memory
`MOV` advances two clocks before consuming its immediate, then two clocks for
the byte form or one for the word form before writing. Register forms remain
on the ordinary bus-free path. Version 18 places the Group 3 read after one
inherited internal clock, the `TEST` operation after its immediate, and the
two-clock interval before a `NOT` or `NEG` write. A ranged multiplication still
reports the placed read without claiming a complete elapsed boundary.
Version 19 places the `INC`/`DEC` read after one internal clock and preserves
the inherited two-clock computation interval before the write.
Version 20 places register, segment and flags pushes after three internal
clocks, immediate pushes after their encoded operand and their documented
internal interval, and single-word pops at their stack read. Version 21 places
`STOS` writes and the comparison setup, read and post-read intervals of `SCAS`;
the documented fixed tail completes only after the last repeated transfer.
Version 22 adds a BCU prefetch-suspend operation that preserves queued bytes
until the subsequent control-transfer flush. Relative near `CALL` and both near
`RET` forms can therefore place their internal clocks, stack transfer, flush
and target prefetch without allowing a speculative fetch during a suspended
interval.
Version 23 additionally places `MOVS` source/destination transfer ordering and
the post-read intervals of `LODS`; the repeated `MOVS` formula has no bus-free
per-iteration interval beyond its two transfers, so its documented fixed tail
is advanced only after the final iteration. The executor still lacks the
offsets for other operand instructions, documented timing ranges and unknown
timings; those paths suspend this overlap model until their individual accesses
can be placed. The observer therefore still labels these as resource
measurements rather than claiming a complete external-bus trace or elapsed
duration. Instruction demand fetch uses a real six-byte queue and per-instance
PFP. An even PFP fetches one little-endian word in a single bus transaction; an
odd PFP fetches one byte before the pointer returns to an even boundary.
Consumed bytes remain queued across instruction boundaries, so later memory
writes do not rewrite already-prefetched instructions. Taken control transfers,
accepted interrupts and architectural state replacement discard the queue and
restart PFP at the new IP. The observer reports PFP and occupancy at every
boundary, rather than inferring queue state only when a flush occurs.

This establishes the first scheduler-ready boundary measurements without
claiming a complete timing model or exposing a clocked CPU callback prematurely.
The shared BCU now establishes real prefetch-versus-operand ownership, but
placing the remaining requests on the EXU timeline, exact realised clocks
inside data-dependent ranges, DMA arbitration and scheduler consumption of the
observations remain explicit subsequent work. The limited overlap is
cycle-phased, but the processor as a whole is not yet described as cycle
accurate.
`POLL` also requires a semantic
correction before timing: the current interpreter emits one boundary per pin
sample instead of keeping the documented polling loop inside one instruction.
That provisional retry discards the queue because it restores architectural IP;
it does not claim the queue or five-clock sampling behavior of real hardware.
NEC's tables also state that execution clocks exclude prefetch, pre-decode and
bus waits. Version 23 combines those quantities only for uncontended cases and
the explicitly placed `IN`/`OUT`, direct accumulator-memory `MOV` and `XLAT`
forms, memory forms of ModR/M `MOV`, all ModR/M ALU forms, immediate ALU groups,
segment-register and immediate-to-r/m `MOV`, Group 3 memory operands, ModR/M
`TEST` and `XCHG`, `FEh`/`FFh` memory `INC`/`DEC`, and single-word stack
`PUSH`/`POP`, `MOVS`, `LODS`, `STOS`, `SCAS`, relative near `CALL` and near `RET`; every other operand boundary remains
explicitly unknown rather than receiving a misleading sum.

The native-extension cut implements the documented V30 Group 3 map used by
`ADD4S`, `SUB4S`, `CMP4S`, `ROL4`, `ROR4`, `INS` and `EXT`. Packed-BCD strings
use DS:SI (or the selected source override) and the fixed ES:DI destination;
only CY and Z are changed, while the manually undefined flags are preserved.
Bit fields use the encoded byte registers, a one-to-sixteen-bit length and an
explicit portable-memory window, then apply the documented offset-register and
SI/DI update. `INS` honors the documented destination override instead of
assuming that every operation is fixed to ES. The earlier
`TEST1`/`CLR1`/`SET1`/`NOT1` path now validates its reserved ModR/M field and
decodes memory displacement before immediate bit data. Blank Group 3 entries,
non-register `INS`/`EXT` forms, out-of-range register operands, malformed
immediate forms and BCD lengths outside 1-254 return
`BM_STATUS_UNSUPPORTED`; they are never accepted as successful no-ops. The
high half of AL after the nibble rotations is architecturally unspecified by
the manual; the core uses explicit deterministic update ordering without
claiming a hardware value for that undefined part. This cut remains host-,
file- and Qt-independent and adds no cycle or prefetch claims.

The coprocessor-contract cut adds the CPU side of NEC `FPO1`/`ESC`
(`D8h`-`DFh`), `FPO2` (`66h`/`67h`) and `POLL`/`WAIT` (`9Bh`). Register FPO
forms perform no CPU data operation. Memory forms decode the effective address,
start the documented word read and discard it from CPU state; an optional
host-neutral callback receives the raw opcode/ModR/M, FPO family, resolved
address and word observed on that bus cycle. With no callback the instruction
still performs those documented CPU-side actions and completes, which models
an absent floating-point component without pretending to execute arithmetic.
Callback failures and memory-bus failures propagate without being converted to
success.

`POLL` samples a separate callback representing the V30's external active-low
POLL input. Ready completes the instruction, while busy restores the
instruction address so another deterministic portable step samples again.
Because an unconnected software callback does not establish the electrical pin
level, absence returns `BM_STATUS_UNSUPPORTED` rather than inventing ready or
an 8087-style exception. The vector-7 behavior documented for V33A/V53A is not
applied to V30. `BUSLOCK POLL` is also rejected, following NEC's explicit
caution. The current retry is an instruction-boundary functional model; the
documented five-clock sampling interval, queue behavior, bus ownership and an
actual floating-point execution component remain outside this cut.

The prefix-control cut adds the V30-native `REPC` (`65h`) and `REPNC` (`64h`)
conditions for `CMPS`/`SCAS`: the completed comparison controls continuation
through CF, while a zero initial `CX` performs no data access. Using either
carry-repeat prefix as the effective repeat prefix for any other instruction is
an explicit unsupported result, and the NEC-undefined `F1h` encoding remains
rejected. When several repeat prefixes are present, the last one selects the
condition, matching the existing last-prefix selection rule.

`BUSLOCK` (`F0h`) is no longer accepted as an inert prefix. The generic bus
contract exposes validated debug and locked attributes, and every fetch,
memory or I/O transaction after the prefix and through its effective
instruction carries the locked attribute. A repeated block keeps it for every
iteration and defers maskable interrupt acceptance until the block completes.
This remains an instruction-domain contract: it does not model electrical pin
levels, bus arbitration or the V30's documented low BUSLOCK output while a
prefixed `HALT` is in standby and no transaction exists.

The test ROM jumps from physical `FFFF0h` to `F0100h`, writes a byte through
the memory bus and halts. No Olivetti firmware or guest media is compiled,
copied or executed by this test.

### PCS86-2 platform-contract status

The next cut adds explicit, independently testable instances of the single
8259A interrupt controller and the 8253 timer. The PCS 86 owns its board glue:
known registers at `60h-6Fh` and the jumper byte at `100h` are not hidden in a
generic PC global. PIT channel 0 raises the machine's PIC IRQ0 input, while a
bus observer can capture successful I/O transactions without coupling devices
to a debugger or frontend.

The V30 subset now performs byte-oriented `IN` and `OUT` operations, including
word forms as two consecutive 8-bit bus transfers, and supports CLI, STI and
CLD. A synthetic ROM uses those paths to configure the PIC and PIT and exercise
board registers. That cut validated composition and traceability only; DMA,
RTC, keyboard queues, interrupt entry and much of the instruction set were
still absent at that boundary.

### PCS86-2A earlier BIOS execution milestone

The original BIOS is a local-only diagnostic input to a manual probe; it is
never part of CTest or a build artifact. Before the later timekeeping and video
work, the two recorded revision 1.09 EPROM hashes verified that the engine could execute
6,341,893 instructions and 2,851 successful I/O transactions before stopping
strictly on the first unmapped RTC counter register, port `E8h`, read at
`F000:F4B0`. This covers the
reset jump, flag/register self-test, the firmware's complete 64 KiB checksum
loop, its first conventional-memory alias check, a 64 KiB upper-memory
clear-and-scan pass, the following segment-overridden memory-alias check and
programming self-tests for the 8237 and its external page latches, the
MM58167 interrupt-status/control access, the following long conventional-
memory test, the complete empty option-ROM scan, the observed PCS 86 video-
selection sequence at `46E8h` and `102h`, the first Paradise initialization and
memory-copy paths, and enough resident diagnostics to publish a real text-mode
frame. The option-ROM region explicitly
models an unpopulated bus returning ones: the PCS 86 firmware already contains
its Paradise initialization and no separate ROM is invented at `C0000h`.

The interpreter additions are still a tested subset: arithmetic and logical
flags, conditional and relative branches, register ModR/M forms, 8086 memory
effective-address decoding, immediate arithmetic including sign-extended CMP,
byte and word immediate memory moves and loads, byte comparison, TEST, AND and
NOT, byte and word multiplication, unsigned word division with real-mode
interrupt-zero faults, NEC V30 bit operations, memory forms of general and
segment moves, direct and indirect near calls, software interrupts and IRET,
register plus all segment and FLAGS stack operations,
byte and word shifts, flag-control operations, segment-overridden loads, all four
segment overrides and byte/word MOVS, STOS, LODS and SCAS operations with
REP/REPE/REPNE. Its inspection contract exposes
all general and segment
registers. Unit tests use new synthetic bytes
reproducing the relevant instruction paths, not Olivetti firmware.

Port `70h` has no verified PCS 86 bit semantics in the evidence currently
available. BIOS context places its `40h` write in the upper-memory setup path,
so the machine records it as an opaque write-only board latch rather than
silently discarding it or borrowing the unrelated PC/AT CMOS convention.
Writes to the known EMS page-selector range `8400h-8403h` are also retained,
but the aperture and backing SIMMs remain deliberately absent.

BIOS writes to `46E8h` and `102h` are retained as write-only video-arbitration
latches. Their observed ordering and values are testable, but the engine does
not yet assign undocumented selection side effects to them. A new isolated
PVGA1A component owns the VGA and Paradise register files, DAC state and 256 KiB
of planar VRAM behind the generic buses. Its input-status phase is deliberately
deterministic rather than timed. The engine now defines a caller-owned XRGB8888
framebuffer value type, the runtime exposes optional geometry and rendering
operations, and the PCS 86 publishes PVGA1A output without exposing the device
to a frontend. The first rasterizer covers text modes only and reads the
programmed character, attribute, font, palette and CRTC state; it does not use a
built-in font or a synthetic diagnostic screen.

The earlier timekeeping cut produced a deterministic 720x400 frame (CRC32
`680D0FA8`) containing the PCS-86 Resident Diagnostics 1.09 screen. CPU, ROM,
DMA, interrupt-controller, Timer 0 and Clock/Calendar checks were visible as
passing, and 640 kB of base memory was reported. The current cut then adds a
caller-owned SPP register component at `378h-37Ah`, an NS16450 register core at
`3F8h-3FFh`, their port-65h gates and IRQ7/IRQ4 routes, and the PCS 86 dual
keyboard/mouse command queues. Runtime keyboard input uses stable physical-key
identifiers and the machine translates supported keys to IBM Set 1 bytes on
IRQ1; Qt, Win32 and host scan codes remain outside the engine.

With those components the preceding local BIOS probe executed 6,272,717
instructions and 3,251 successful I/O transactions, completed its parallel and
serial probes, and stopped strictly at its first access to the then-absent
floppy controller, `OUT 3F2h,AL` at `F000:352C`. The no-printer SPP status and
disconnected UART remain honest device states, not forced diagnostic-success
values. The floppy cut below advances beyond that recorded boundary; mouse
input, complete keyboard command coverage, cursor/blink, graphics modes and
scan timing remain outside the current implementation.

The PCS 86 now owns a portable 8237 core with explicit address,
count, command, mode, request, mask, status and master-clear state. A separate
XT page-register component maps the firmware-observed `87h`, `83h`, `81h` and
`82h` channel order, retains reserved ports as independent latches and exposes
the resulting 20-bit DMA address. PCS 86 writes are constrained to the
documented four-bit page value; an 8237 master clear cannot erase these
external latches. Devices can transfer bytes synchronously through an explicit
channel API; asynchronous arbitration, cycle stealing and bus-ownership timing
remain outside this cut.

The MM58167 maps the PCS 86 control window at `B0h-B7h` and its counter/alarm
RAM at `E0h-EFh`. It advances a deterministic BCD millisecond calendar, matches
alarms, raises enabled interrupt sources and saves or restores its caller-owned
32-byte state. Counter and RAM reset commands, GO, standby and the PCS 86
checksum repair path are covered by focused tests. This is a selective port of
the inherited `src/device/isartc.c`, retaining Fred N. van Kempen's notice. A
yearless calendar and instruction-domain scheduling are explicit fidelity
limits; this is not a crystal- or battery-level simulation.

The PCS 86 configuration may provide that complete 32-byte state explicitly.
The engine never reads the host clock or a host file. A generic named-state
runtime contract lets a frontend copy opaque machine state while a session is
running or paused; it does not know how or whether those bytes are stored. The
PCS 86 publishes its RTC as the `rtc` state. Frontend adapters declare the
state's size, deterministic fallback and battery-backed policy independently
of Qt or a path.

Qt stores retained bytes in its host settings and exposes **Retain
battery-backed state**. Disabling it removes the saved image, supplies zeroed
bytes on the next cold start and discards the new state at power-off, modelling
a depleted battery without changing reset semantics. Headless offers the same
boundary explicitly with `--persistent-state rtc=<path>` and
`--depleted-state rtc`. The deterministic first-use fallback includes both
nonzero calendar counters and the BIOS weekday/checksum encoding in alarm RAM;
counters alone are insufficient because BIOS 1.08 rewrites those fields after
its first successful `INT 1Ah` read. None of these paths synchronizes the RTC
to host wall time.

The 8253 is a selective port of the measured edge-state core, retaining Daniel
Balsom and Clara's attribution. It covers modes 0-5, binary and BCD counts,
gates, stable latches and output edges. An optional, separately linked adapter
can now drive that unchanged chip model from an exact rational engine clock;
its contract test uses the PC `14.31818 MHz / 12` source and checks fractional
mode-2 output-edge timestamps. The adapter currently advances one input edge
per scheduler activation as a correctness baseline, not the final performance
path. The PCS 86 still uses rational accumulators over its measured functional
instruction rate because its V30 timing observer is not yet consumed by the
scheduler, so that machine does not claim cycle accuracy.

Maskable interrupts now have an explicit handshake. The PIC publishes its
pending output, the machine routes that signal through the engine CPU contract,
and the V30 asks the PIC for a vector before pushing FLAGS/CS/IP and reading the
real-mode vector table. A withdrawn edge request no longer survives as its
original IRQ before the first interrupt acknowledgement. Non-specific and
specific EOI commands release their corresponding in-service state, and fixed
priority prevents the same or a lower-priority request from recursively
entering an active handler. This matters for the PCS 86 BIOS, which uses
specific `61h` and `66h` EOI commands for keyboard and floppy interrupts.
Neither component owns the other. The added SPP, NS16450 and keyboard paths
follow the same ownership rule and communicate only through explicit callbacks
and runtime events. Mouse delivery and complete V30 coverage remain subsequent
cuts.

### PCS86-4 floppy/bootstrap status

Storage remains a composition of small contracts. The engine defines a
caller-owned block medium with no host path or `FILE *`; a floppy drive owns
geometry and mechanical state; the uPD765-compatible component owns the AT
register front, command/result phases, active-low change indication and its
explicit links to IRQ6 and DMA2. The PCS 86 machine chooses the documented
jumper encoding from the configured drive geometry. No layer reaches into a
global drive table.

The public tests use newly authored in-memory sectors. They verify complete
512-byte DMA read and write paths, terminal count, including completion before
an EOT beyond the mounted track, result bytes, reset/sense
interrupts and write protection. The controller contract limits a sector
payload to 4096 bytes; read and write commands for an otherwise valid 8192-byte
generic drive are rejected before DMA or media callbacks. Original firmware
and media remain local-only manual inputs. With BIOS 1.09 and the preserved
720 KiB system diskette, all visible resident diagnostics report `Pass`, and
the BIOS detects one floppy and enters primary bootstrap. At that historical
cut, the boot sector and system files displayed the Microsoft MS-DOS 3.30a
banner before an unclaimed `OUT 02F2h,AL` stopped the engine. The later bus-
response contract described below supersedes that diagnostic boundary; it does
not turn the absent device into an implemented one.

### Explicit passive-bus and memory-write responses

The generic bus can now map a stateless response over a range and can define a
default response independently for each address space. Read, write and fetch
each have an explicit status, while successful reads and fetches repeat a
configured fill byte across the transaction. A mapped device may return
`BM_STATUS_UNMAPPED` for an operation it does not decode; the bus then applies
the address-space default. Observers receive the one final successful
transaction, so diagnostics do not invent a second hidden access.

The PCS 86 composition uses that contract for its board-level passive I/O
response: unclaimed ports read `FFh` and ignore writes. This one rule covers
absent expansion cards, conventional but unpopulated XTA bases, and reads from
write-only DMA, PIT and board registers. Device callbacks still own documented
register behavior, and unsupported command semantics still fail explicitly;
there are no BIOS- or Customer-specific port exceptions. The unpopulated
`C0000h-EFFFFh` option-ROM range is a static memory response returning ones and
discarding writes.

Linear memory likewise distinguishes writable storage, protected storage whose
writes return `BM_STATUS_READ_ONLY`, and immutable storage whose physical write
cycles are acknowledged but ignored. This lets strict tools request a failure
without making real EPROM behavior fatal to an emulated CPU. Synthetic tests
cover all policies, callback precedence and delegation, observer behavior,
unclaimed PCS 86 ports, read-only/write-only registers and absent XTA slots.

A CPU architecture cut adds tested, general 808x semantics for
sign extension, string comparison and repeat conditions, direct and indirect
far control flow, `POP r/m16`, byte/word exchange and negation, accumulator
`TEST`, `XLAT`, immediate carry arithmetic and unsigned byte division. The
CPU trace now distinguishes the first byte from the effective opcode after
prefixes, while inspection exposes up to the first eight consumed instruction
bytes and their full length. Each behavior is covered with synthetic memory and
machine-independent tests; opcode handling does not depend on firmware
addresses, image contents or host services.

The following CPU cut completes both F6h/F7h TEST encodings and their byte/word
NOT, NEG, MUL, IMUL, DIV and IDIV operations, including interrupt-zero error
entry. Its external V20 corpus evidence remains architectural only; it does not
claim V30 cycle, prefetch-queue or 16-bit-bus fidelity.

The next cut begins the V30's 80186-compatible instruction set with PUSHA,
POPA, immediate PUSH and immediate IMUL. Stack state and multiplication flags
remain explicit core behavior and use only the portable memory bus.

The following cut adds CHKIND/BOUND, the immediate rotate/shift groups,
PREPARE/ENTER and DISPOSE/LEAVE. All operands still pass through the portable
register and memory-bus contracts. Undefined NEC encodings return
`BM_STATUS_UNSUPPORTED`; they are not treated as undocumented aliases or
successful no-ops.

The next cut adds INSB/INSW/OUTSB/OUTSW, including REP count-zero behavior,
DF-directed indexing, the fixed DX port, the mandatory ES input destination,
source overrides for output and ordered word transfers. It uses only the same
portable memory and I/O bus.

The following cut makes interrupt acceptance an explicit instruction-boundary
contract. It preserves the V30 delay after `EI` and segment-register transfers,
prevents an asserted interrupt from being stranded by the common `EI; HLT`
sequence, and restarts interrupted REP memory or I/O operations at the retained
prefix address with completed progress intact. This changes no host, Qt, file
or firmware boundary. NMI, trap handling and V30 bus-cycle accounting remain
separate future cuts.

The FDC deliberately omits rotational and command latency, non-DMA transfer,
format-track, deleted-data distinction and flux/weak-sector formats. Dynamic
raw-media insertion and ejection are explicit session operations at an engine
instruction boundary; the drive validates the replacement before changing its
state and raises the existing disk-change indication. This does not claim the
omitted physical-media behavior.

## Portable Qt presentation boundary

### Host pacing and latency diagnostics

On Windows the Qt worker uses a high-resolution waitable timer for its running
idle interval, together with an auto-reset event for queued commands. Input can
wake the wait immediately. Paused/stopped workers wait for commands without
periodic polling. Handles are owned by the frontend worker and released after
its thread joins. Unsupported hosts use the standard condition-variable wait;
failure to obtain the Windows timer is reported. No timer API enters the engine.

The opt-in `BLUMACH_TRACE_LATENCY` environment flag emits monotonic timestamps
and opaque IDs for input queueing/dispatch/completion, frame generation, UI
delivery and paint submission. It records no key values or guest image data.
The portable PowerShell launcher accepts `-TraceLatency -LogPath <local-log>`;
run `python -B tools/portable_latency_report.py <local-log>` for sample counts,
p50/p95/max, frame intervals and ticks per wall second. These measurements
include logging overhead. Paint submission is neither physical monitor scanout
nor proof that a guest application has rendered the requested character.

Local Windows/UCRT64 Release measurements found that a nominal 1 ms condition
wait took about 15.5 ms; the high-resolution wait took about 2 ms. With the
canonical read-only PCS 86 inputs and OpenGL, observed guest throughput changed
from about 0.49 million to 1.98 million ticks per wall second (target 2 million),
and median frame interval from 30.8 to 18.2 ms. Eight input transitions reached
the worker in 0.8–3.4 ms; six reached paint submission in 13–18 ms. Two early
transitions encountered a 1.4-second post-UI-delivery paint gap during native
UI automation. Its cause is unconfirmed; it is not excluded from the report.
Other platforms and end-to-end physical keyboard/display latency are unmeasured.

### Bounded headless debug observation

The frontend adapter exposes an optional host-neutral observer for completed
CPU instructions, accepted interrupt vectors, memory accesses and I/O
transactions. It is disabled by default, owns no files and cannot alter
execution. Instruction records include the complete architectural register
set at the instruction boundary. The headless runner's `--trace-tail N`
option retains only the most recent `N` selected events (maximum 4096) and
prints them in original sequence order. This provides a bounded diagnostic
trail around failures without permanent multi-million-instruction logs.

When tracing is enabled, headless also retains the most recent 32 accepted
interrupt boundaries. Each entry records the last completed instruction, the
PIC vector and the first instruction at the target. The most recent boundary
is printed separately, and a failed run reports the complete architectural CPU
register set. These fixed-size records remain bounded even when a guest loops
for a long time after the event that caused a failure.

`--trace-memory` restricts retained memory events to one address or a bounded
range. `--trace-only memory|writes|io` reduces the retained event classes, and
the optional memory image reconstructs the bytes written in the selected range;
it is capped at 1 MiB and is not an unrestricted guest-memory dump. A trace can
freeze at one CS:IP, physical instruction address or event sequence while the
machine continues to its requested deterministic stop. Freeze records include
the CPU state at that boundary. These controls are diagnostics only: they do
not add a debugger command channel or allow the host to mutate guest memory.

Version 2 headless scenarios can schedule named physical key transitions as
well as text, and a run can schedule one read-only floppy replacement. This
makes modifier-sensitive input and multi-disk boot paths reproducible without
GUI automation. The scenario and media scheduler call the same public input
and storage contracts used by frontends; neither the machine nor its devices
receive host paths.

The observer carries architectural state and addresses, opcodes, accepted
interrupt vectors, memory values and byte-wide PCS 86 I/O values only. Output
policy remains in the headless frontend; the engine and machine receive no
paths, streams or host APIs. A disabled observer has no trace buffer and the
existing diagnostic counters remain unchanged.

The CPU still reports instruction-based ticks. FAST/SLOW port behavior and the
physical slow clock remain a separate pending implementation/measurement.

### Presentation preferences

The Qt6 frontend owns window geometry, menus, fullscreen state, presentation
scaling, interpolation and image export. These are user preferences rather
than emulated-machine state: changing them cannot affect engine time, video
memory or the framebuffer returned by the runtime. The first presentation cut
offers source-aspect fit, integer-pixel, corrected 4:3 and stretched layouts,
with independently selectable nearest-neighbour or smooth interpolation.

Frame copying and PNG export consume the frontend's last published image, not
the composited window. The application offers a Qt OpenGL presentation surface
and a software surface over that same immutable XRGB8888 image. Software is the
default; an OpenGL widget is created only when selected and destroyed when
returning to software. A
first optional CRT profile adds scanlines and an edge vignette after scaling;
it does not modify the source image, video memory or emulated video state.
Renderer, effect, scaling and interpolation choices are persistent host
preferences. File dialogs, clipboard access and settings remain on the Qt side
of the boundary; no path, native window, settings object or Qt type crosses
into the frontend adapter, runtime or engine. This is a new, deliberately small
presentation layer, not a port of the inherited renderer or shader globals.
Additional multipass or user-supplied shaders may be layered here later while
preserving the same clean framebuffer contract.

The interactive worker derives a five-millisecond catch-up slice from the
machine definition's scheduler rate and coalesces consecutive ordinary frames
before they cross to the Qt event loop. Lifecycle results and errors remain
ordered and cannot be discarded. This bounds the worker-side keyboard delay
and prevents a busy UI from presenting an accumulated queue of obsolete
images. The scheduler rate is the portable machine contract; the frontend must
not infer it from a CPU crystal or another device clock.

Qt key events are translated to the engine's stable physical-key identifiers
before they enter the session. On Windows the adapter consumes Qt's native
physical scan code, matching the established frontend's layout-independent
behavior, including the ISO `0x56` key and extended modifiers/navigation keys.
Other platforms use the declared Qt key until their physical adapters are
defined. Shifted punctuation remains a fallback when a physical code is not
available. Auto-repeat release events are discarded, as they are Qt artifacts;
repeat presses and the final release retain the guest keyboard's make-stream
semantics. No Qt value, host scan code or keyboard-layout object crosses into
the runtime.

The PCS 86 port `60h` is a data latch as well as the IRQ1 queue front. Reading
it acknowledges the current request and advances a queued byte, but an empty
queue continues to expose the last latched byte until another scan byte arrives
or the machine resets. This matters for software that reads the byte in a
hooked IRQ1 handler and then chains the previous handler; both readers observe
the same hardware datum without manufacturing a second input event.

Keyboard indicators are guest-owned state. The PCS 86 keyboard channel now
acknowledges the two-byte `EDh` command, retains only its defined Scroll, Num
and Caps bits, and clears them on reset. An optional runtime query publishes
that state without exposing board ports or guest commands. Qt renders the
three indicators from the query; it never substitutes the host operating
system's lock-key state.

Each accepted make or break transition is followed by two milliseconds of
emulated execution derived from that same declared scheduler rate. This
deterministic separation prevents a burst from the Qt event loop from
depositing an entire key sequence at one emulated instant. The worker charges
those ticks to its wall-clock pacer, so input processing cannot make the guest
run ahead. Headless and other frontends remain free to schedule the same engine
events with their own explicit emulated-time separation; there is no private
Qt timer or machine-specific keyboard exception in the engine.

Video geometry also carries an optional exact refresh rational. PVGA1A derives
it from the programmed CRTC totals and a selected documented clock; board-defined
external clocks remain zero/unknown instead of being assigned a guessed rate.
Headless reports the rational directly. Qt labels source resolution and guest
refresh separately from physical output size and measured presentation FPS.

Storage telemetry follows the same boundary. A machine may enumerate generic
device status containing media presence, write protection, motor state and
cumulative completed read/write operations. The PCS 86 composes those values
from its floppy drive and FDC, while Qt only presents snapshots and short
activity pulses. A separate runtime command identifies a device by generic kind
and unit and supplies caller-owned block callbacks for insertion or no medium
for ejection. The PCS 86 accepts its supported 720 KiB and 1.44 MiB geometries;
invalid replacements leave the current medium unchanged. The Qt worker owns a
successful replacement until another replacement, ejection or session teardown,
so no callback can outlive its file resource. Paths, file handles and Qt types
remain outside the runtime and engine, and the GUI exposes only read-only media.
