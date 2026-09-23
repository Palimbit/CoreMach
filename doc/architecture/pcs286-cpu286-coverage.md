# PCS 286 portable Intel 80286: coverage and implementation decision

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Latest tranche: real-mode division and divide-error delivery

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

- Data operations: memory XCHG/implicit LOCK (LEA/LDS/LES/XLAT now implemented).
- Decimal adjustment instructions (multiply, DIV/IDIV and CBW/CWD implemented).
- Strings, REP restart/interruption and string I/O.
- Guest faults beyond real-mode #DE, BOUND/invalid-opcode handling, protected segmentation, descriptors,
  privilege, tasks/gates and system instructions; correct double fault/shutdown.
- Unpopulated 80287 interface behavior (ESC/WAIT and MSW interaction), explicit
  undocumented-opcode policy, and calibrated timing/prefetch/bus behavior.

The next bounded block is decimal adjustment (DAA/DAS/AAA/AAS/AAM/AAD),
including AAM base zero reusing the real-mode divide-error path.
Memory XCHG stays separate until
the implicit LOCK/bus contract is implemented and tested. Completing real-mode
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
| Reset, fetch, prefixes | CS=F000, hidden base=FF0000, IP=FFF0, MSW=FFF0, FLAGS=0002; fetch at FFFFF0; segment overrides 26/2E/36/3E, LOCK F0, REP F2/F3; prefix-inclusive fault IP and illegal combinations | `x86.c`, `386_ops.h`, prefix handlers | Reset/fetch and 10-byte-bounded override decode implemented; LOCK/REP and fault delivery missing; timing unknown |
| Data movement | MOV 88-8E/A0-A3/B0-BF/C6-C7, XCHG 86-87/90-97, LEA 8D, LDS/LES C4-C5, XLAT D7; register/memory, odd word, segment load permissions and cache | `x86_ops_mov.h`, `x86_ops_mov_seg.h`, `x86seg.c` | Listed real-mode MOV, LEA/LDS/LES/XLAT and register XCHG implemented; MOV SS has SS-load inhibition; memory XCHG and protection missing; timing unknown |
| Integer ALU / flags | ADD/ADC/SUB/SBB/CMP/AND/OR/XOR, TEST, INC/DEC, NEG/NOT, Group 1 80-83 and F6/F7; byte/word carry, overflow, auxiliary carry, parity, defined/undefined flags, memory read-modify-write | `x86_ops_arith.h`, `x86_ops_inc_dec.h`, `x86_ops_misc.h`, `x86_flags.h` | Real-mode 00-3D, 80/81/83, 84/85, A8/A9, 40-4F, FE/FF /0,/1 and F6/F7 /0,/2,/3 implemented; 82 and all other groups deferred; timing unknown |
| Multiply/divide, BCD | MUL/IMUL/DIV/IDIV F6/F7, immediate IMUL 69/6B, DAA/DAS/AAA/AAS/AAM/AAD, CBW/CWD; divide #0 before destination mutation; result-dependent timing | `x86_ops_misc.h`, `x86_ops_mul.h`, `x86_ops_bcd.h` | Real-mode MUL/IMUL/DIV/IDIV including #DE, IMUL 69/6B and CBW/CWD implemented; BCD and general fault escalation pending; timings unknown |
| Shifts/rotates | C0/C1/D0-D3 Groups 2; counts 0/1/>1, CF/OF, through-carry, memory alignment | `x86_ops_shift.h` | All seven documented real-mode operations implemented; undefined flag policies explicit, /6 and LOCK unsupported; timing unknown |
| Stack/procedures | PUSH/POP registers, segments, immediates and r/m; PUSHF/POPF, PUSHA/POPA, ENTER/LEAVE, near/far CALL/JMP/RET and IRET; 286 PUSH SP value, SP wrap, interlevel stacks | `x86_ops_stack.h`, `x86_ops_call.h`, `x86_ops_ret_2386.h`, `x86seg.c` | Listed real-mode stack/procedure forms implemented, including far CALL 9A/FF /3 and RET CA/CB; protected/interlevel execution and fault delivery missing; timing unknown |
| Branch and loop | Jcc 70-7F, LOOP/LOOPE/LOOPNE/JCXZ E0-E3, short/near/far jumps; taken/not-taken, prefetch flush, segment privilege/limit | `x86_ops_jump.h`, `x86seg.c` | Real-mode Jcc 70-7F, E0-E3 and JMP EB/E9/FF /4 and far JMP EA/FF /5 implemented; protected control and prefetch model missing; timing unknown |
| Strings and block I/O | MOVS/CMPS/STOS/LODS/SCAS A4-AF; INS/OUTS 6C-6F; REP/REPE/REPNE, zero count, DF, per-iteration interrupt/HOLD and restart state, segment-limit fault | `x86_ops_string.h`, `x86_ops_rep_286_2386.h` | Missing; timing iteration/prefetch-dependent |
| Direct I/O and flag control | IN/OUT E4-E7/EC-EF, CLI/STI, CLD/STD, CLC/STC/CMC, LAHF/SAHF; 16-bit I/O port, CPL/IOPL checks and STI shadow | `x86_ops_io.h`, `x86_ops_flag_2386.h` | Listed real-mode forms implemented; protected privilege checks missing; timing unknown |
| Software interrupts / halt | INT3/INT/INTO/IRET CC-CF, HLT F4; real IVT and protected gates, IF/TF effects, HLT wake conditions | `x86_ops_int.h`, `x86seg.c`, `386.c` | Listed real-mode forms implemented; protected gates and fault delivery pending; timing unknown |
| 286 application extensions | BOUND 62 (#5), ARPL 63, 186-family PUSHA/POPA, immediate PUSH, IMUL, ENTER/LEAVE, INS/OUTS and count-immediate shifts | `386_ops.h`, `x86_ops_misc.h`, `x86_ops_pmode.h` | Real-mode PUSHA/POPA, immediate PUSH/IMUL, ENTER/LEAVE and immediate shifts implemented; other forms and guest faults missing; timing unknown |
| Protected system instructions | 0F 00 group SLDT/STR/LLDT/LTR/VERR/VERW; 0F 01 SGDT/SIDT/LGDT/LIDT/SMSW/LMSW; 0F 02/03 LAR/LSL; 0F 06 CLTS; privilege, type, present and selector tests | `x86_ops_pmode.h`, `386_ops.h` | Missing; timing descriptor/path-dependent |
| Undefined / undocumented | Reserved primary, 0F and ModR/M forms must deliver #6 per Intel's documented map. Classic 0F 05 LOADALL, F1 alias and D6 SETALC are separate undocumented silicon candidates, not documented-required success | `386_ops.h`, `x86_ops_misc.h` | Missing; undocumented deferred pending silicon evidence |
| Unpopulated 80287 interface | ESC D8-DF and WAIT 9B with MSW EM/MP/TS: required #7/no-coprocessor behaviour; no fabricated 80287 arithmetic. Populated BUSY/ERROR/PEREQ/PEACK and #9/#16 are later | `x86_ops_fpu_2386.h` | Missing; no populated FPU contract; timing unknown |

## Protection, interrupt and fault matrix

These are independent checks, not consequences of decoding the opcodes above.
Defined reset, real-mode caches, HOLD and bounded real-mode hardware/software
interrupt entry/return are implemented as described above. Protected execution
and guest fault delivery remain missing. The table lists required coverage,
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
   kinds. A future REP implementation must return at most one iteration per
   step, retain prefix restart IP while incomplete, avoid data transactions
   at CX=0, and check HOLD/interrupts at legal boundaries. Imported state
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
