# PCS 286 portable Intel 80286: coverage and implementation decision

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: P1 source review and first lifecycle tranche, 2026-09-23. Reviewed implementation base
`87c3fb4876eaad086921bc3444569da026286c36` on
`feature/pcs286-cpu286`, subsequently included unchanged in portable merge
`8e5cd917d95536fbdfe65ce2d5d658eda0633d20`. This is the Olivetti
**PCS 286**, not the TI/OLIMCU PCS 286/S. No new CPU body is present at this
base. The existing `cpu286_acceptance.c` was initially skipped because its
target did not exist. The current branch adds source and expanded tests;
coordinator-owned CMake/provenance wiring is under integration. No firmware or preserved media was
used in this review or the authored tests.

## Evidence and scope

The canonical `Z:/library/olivetti/pcs286` README, manifest and 2026-09-23
worklogs establish the 12 MHz Intel 80286 identity and the *absence* of a
portable implementation. They do not certify opcode or timing behaviour. The
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
state and functional NOP only; all other rows are **not implemented**. A
classic handler is a review/reuse candidate only, never a
portable pass. Every grouped opcode must classify each ModR/M subform; a
generic catch-all or NOP is not coverage.

| Family / encodings | Required semantics and representative tests | Classic candidate | Portable / timing |
|---|---|---|---|
| Reset, fetch, prefixes | CS=F000, hidden base=FF0000, IP=FFF0, MSW=FFF0, FLAGS=0002; fetch at FFFFF0; segment overrides 26/2E/36/3E, LOCK F0, REP F2/F3; prefix-inclusive fault IP and illegal combinations | `x86.c`, `386_ops.h`, prefix handlers | Reset/fetch/NOP implemented; prefixes and fault delivery missing; timing unknown |
| Data movement | MOV 88-8E/A0-A3/B0-BF/C6-C7, XCHG 86-87/90-97, LEA 8D, LDS/LES C4-C5, XLAT D7; register/memory, odd word, segment load permissions and cache | `x86_ops_mov.h`, `x86_ops_mov_seg.h`, `x86seg.c` | Missing; timing unknown |
| Integer ALU / flags | ADD/ADC/SUB/SBB/CMP/AND/OR/XOR, TEST, INC/DEC, NEG/NOT, Group 1 80-83 and F6/F7; byte/word carry, overflow, auxiliary carry, parity, defined/undefined flags, memory read-modify-write | `x86_ops_arith.h`, `x86_ops_flag_2386.h`, `x86_ops_mul.h` | Missing; timing unknown |
| Multiply/divide, BCD | MUL/IMUL/DIV/IDIV F6/F7, immediate IMUL 69/6B, DAA/DAS/AAA/AAS/AAM/AAD, CBW/CWD; divide #0 before destination mutation; result-dependent timing | `x86_ops_mul.h`, `x86_ops_bcd.h` | Missing; timing ranges unresolved |
| Shifts/rotates | C0/C1/D0-D3 Groups 2; counts 0/1/>1, CF/OF, through-carry, memory alignment and LOCK where legal | `x86_ops_shift.h` | Missing; timing count-dependent |
| Stack/procedures | PUSH/POP registers, segments, immediates and r/m; PUSHF/POPF, PUSHA/POPA, ENTER/LEAVE, near/far CALL/JMP/RET and IRET; 286 PUSH SP value, SP wrap, interlevel stacks | `x86_ops_stack.h`, `x86_ops_call.h`, `x86_ops_ret_2386.h`, `x86seg.c` | Missing; timing path-dependent |
| Branch and loop | Jcc 70-7F, LOOP/LOOPE/LOOPNE/JCXZ E0-E3, short/near/far jumps; taken/not-taken, prefetch flush, segment privilege/limit | `x86_ops_jump.h`, `x86seg.c` | Missing; timing path-dependent |
| Strings and block I/O | MOVS/CMPS/STOS/LODS/SCAS A4-AF; INS/OUTS 6C-6F; REP/REPE/REPNE, zero count, DF, per-iteration interrupt/HOLD and restart state, segment-limit fault | `x86_ops_string.h`, `x86_ops_rep_286_2386.h` | Missing; timing iteration/prefetch-dependent |
| Direct I/O and flag control | IN/OUT E4-E7/EC-EF, CLI/STI, CLD/STD, CLC/STC/CMC, LAHF/SAHF; 16-bit I/O port, CPL/IOPL checks and STI shadow | `x86_ops_io.h`, `x86_ops_flag_2386.h` | Missing; timing unknown |
| Software interrupts / halt | INT3/INT/INTO/IRET CC-CF, HLT F4; real IVT and protected gates, IF/TF effects, HLT wake conditions | `x86_ops_int.h`, `x86seg.c`, `386.c` | Missing; timing gate/path-dependent |
| 286 application extensions | BOUND 62 (#5), ARPL 63, 186-family PUSHA/POPA, immediate PUSH, IMUL, ENTER/LEAVE, INS/OUTS and count-immediate shifts | `386_ops.h`, `x86_ops_misc.h`, `x86_ops_pmode.h` | Missing; timing unknown |
| Protected system instructions | 0F 00 group SLDT/STR/LLDT/LTR/VERR/VERW; 0F 01 SGDT/SIDT/LGDT/LIDT/SMSW/LMSW; 0F 02/03 LAR/LSL; 0F 06 CLTS; privilege, type, present and selector tests | `x86_ops_pmode.h`, `386_ops.h` | Missing; timing descriptor/path-dependent |
| Undefined / undocumented | Reserved primary, 0F and ModR/M forms must deliver #6 per Intel's documented map. Classic 0F 05 LOADALL, F1 alias and D6 SETALC are separate undocumented silicon candidates, not documented-required success | `386_ops.h`, `x86_ops_misc.h` | Missing; undocumented deferred pending silicon evidence |
| Unpopulated 80287 interface | ESC D8-DF and WAIT 9B with MSW EM/MP/TS: required #7/no-coprocessor behaviour; no fabricated 80287 arithmetic. Populated BUSY/ERROR/PEREQ/PEACK and #9/#16 are later | `x86_ops_fpu_2386.h` | Missing; no populated FPU contract; timing unknown |

## Protection, interrupt and fault matrix

These are independent checks, not consequences of decoding the opcodes above.
Except for defined reset state and HOLD cancellation, all are unimplemented
in the portable core. The table follows Intel manual
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
   before fetch; interrupt delivery is not claimed. The draft single
   `interrupt_shadow` cannot distinguish STI's INTR delay from SS-load
   trap/NMI inhibition; resolving those separate rules is a later tranche.

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

The first authored source block now contains per-instance lifecycle, Intel's
defined reset state, 24-bit fetch, NOP, state import validation, basic HOLD
acknowledge, and a strict clocked refusal before fetch while timing is
unknown. It does not deliver interrupts, exceptions, prefixes, strings or
protected mode. A direct UCRT64 C11 warnings-as-errors build and execution of
the strengthened `cpu286_acceptance.c` pass; CMake/CTest integration is
coordinator-owned. The next bounded block, after baseline review,
is documented primary-opcode classification and a small authored real-mode
instruction set sufficient to exercise decode, flags, stack and odd-word bus
transfers. Valid but unimplemented opcodes must stop with an explicit emulator
status; invalid encodings cannot claim guest #6 until fault delivery exists.
Then proceed to the complete families and protected-mode matrix above before
declaring P1 complete. This first block is not a bootable PCS 286 or an
approved full CPU.
