# Private shared-decoder integration (block D1)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Current activation: [public functional step/run](pcs286-protected-public.md)
now use the reviewed profile. Public PE gate statements below describe the
earlier checkpoint; the strict clock gate and stated semantic limits remain.


This tranche connects a bounded instruction subset to protected accesses,
segment loads, fault unwind, event delivery and IRET. It is an incremental
integration probe, **not public protected-mode activation**. The imported
context already has consistent protected caches; LMSW/far-transfer entry,
system instructions and the full instruction consumer audit remain pending.
`bm_286_step`, `cpu.ops.run` and strict clocked execution expose no PE bypass.
There is no runtime/build switch or installed header enabling this subset.

## Shared execution and allowed scope

Private `execution_286.h` declares `bm_286_pm_step_subset`, operating on the
same instance as ordinary CPU lifecycle and signals. It reuses the existing
decoder and MOV/LEA/XLAT/register-XCHG/NOP handlers. The internal default PE
gate remains; only this private entry sets a per-decode flag, with an explicit
positive opcode list before any unreviewed handler can run. IRET is routed to
the protected same-CPL helper rather than the real-mode return path.

| Opcode | Private D1 support |
| --- | --- |
| 88–8B, A0–A3, B0–BF, C6/C7 /0 | Byte/word MOV, immediate/register/memory and moffs |
| 8C/8E | MOV segment selector, ordinary DS/ES/SS loads and accessed writeback |
| 8D, D7 | LEA offset only; XLAT with protected data access |
| 90–97 | NOP and register XCHG |
| CF | Ordinary same-CPL protected IRET, with existing FLAGS restrictions |
| 26/2E/36/3E | Segment overrides under the shared ten-byte limit |
| Everything else | Explicit unsupported stop; no invented guest #UD |

MOV CS and register-source LEA retain their documented #UD paths. Reserved
ModR/M aliases remain unsupported. LOCK/REP, arithmetic/RMW, aggregate stack,
strings/I/O, software INT, far transfers, system queries and privileged
instructions cannot enter unchecked real-mode handlers. Task/outer IRET and
inner/task event gates keep their existing unsupported results.

## Sources and fault/commit policy

The behavioral reference is the [Intel 80286/80287 PRM (1987)](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
MOV B-73/B-74 (PDF 281/282), LEA B-63, IRET B-51/B-52, protection section 7.4
and exception/priority sections already reviewed for blocks A–C. MOV's
segment-load actions, null-cache rejection, #NP/#SS/#GP selection, unchanged
FLAGS and SS interrupt inhibition were rechecked against the local manual.
The PDF remains outside Git. Existing derived-rewrite attribution and
inherited authors in `cpu_80286.c` remain intact; no additional external
emulator implementation was copied.

Fetch and scalar memory operands use block C's cache checks and bus transfers.
Full byte/word width is checked before touching the operand; an unusable cache,
execute-only read, read-only write or illegal range does not access that
operand. LEA does not dereference or check segment access rights. Descriptor
loads keep their independent selector/error semantics and staged A/cache
commit. No generic bus/router ABI acquires x86 checks.

A private vector/error marker unwinds the handler before instruction register
commit. It then calls the bounded protected delivery coordinator with the
architectural IP still at the first prefix. No real-mode frame is used. An
error code is pushed only for the cause that requires it; the handler removes
its own error word before CF. Successful IRET owns the restored IP, so the
decoder's sequential cursor cannot overwrite it. Unsupported gaps and every
non-OK host callback stop the instance; they never become guest exceptions.
In particular, host IDLE after event acceptance is a stop, not an empty event
poll. Completed endpoint writes/acknowledgements survive without replay.

Before fetch, block B arbitrates pending TF/NMI/INTR and escalation/shutdown.
HOLD produces an idle boundary without fetching or consuming the SS shadow.
Successful instructions consume the previous shadow, MOV SS establishes the
next shadow and suppresses its own sampled trap, and incoming TF is sampled
for the next boundary. Signals latched by callbacks survive register commit;
a fresh NMI edge during IRET remains pending after successful unblock.
INTR retains exactly two INTA phases and its existing lock window. The subset
adds no populated NPX or #9 signal source. Timing stays UNKNOWN; reported
cycles are only the established wait lower bound, not scheduled CPU clocks.

## Validation and limits

The authored test `pcs286-component.protected-execution` covers:

- 128 CPL/alignment/override/byte-word/read-write MOV cases, including
  write-to-CS faults with no operand write.
- Eight byte-stream fault/repair/IRET/retry sequences (four CPL, even/odd
  tables and stack): MOV loads null DS; a prefixed MOV faults; the handler
  executes MOV to load a valid DS, MOV SP to discard its known error word,
  then CF; the original instruction retries successfully. After initial
  setup, the fixture neither imports state nor calls direct event/IRET helpers.
- 88 integrated fault cases for #GP/#SS/#NP/#UD: operand permissions/limits,
  expand-down, absent DS/SS loads, invalid MOV CS/LEA, fetch cut and excessive
  prefix length. Error words, saved first-prefix IP, preserved registers and
  the first non-fetch transaction are asserted.
- 2,080 per-transfer failures before/after effects for MOV load/store,
  null-cache fault delivery, segment load/#NP, IRET and NMI entry. Five host
  statuses include IDLE. Exact retained RAM, no early CPU commit, lock release
  and rejection of subsequent private/public steps are checked.
- Allowed opcode families, successful expand-down access, TF/NMI/IRQ priority,
  MOV SS inhibition, HOLD, new callback NMI edges and two-phase INTA.
- The public PE gate and explicit refusal of LOCK/REP, arithmetic, strings,
  aggregate stack, far transfer, system and I/O gaps before operand effects.

These are executed synthetic instruction streams through the private subset,
stronger than the earlier helper-only roundtrips. They do **not** exercise
entry from real mode, a complete protected instruction set, a protected OS,
physical bus timing or PCS286 firmware. The remaining D gate must integrate
LMSW/far transfers, system/privilege/IOPL checks, multi-operand ordering,
stack/string/REP restart and software events, then audit all consumers before
opening the public and internal default PE barriers together. Existing
ED=1/FFFF source ambiguity, REP errata and faulting-IRET NMI timing remain
explicit evidence gaps; task/privilege transitions are separate blocks E/F.

All work in C/D1 remains local and uncommitted over
`4769e40524bc194747b142f3e7ec908ae4df0897`. No push, PR update or new remote CI
is authorized for this delivery. Older MSVC/CI results certify only the
published base, not these working files.

Local validation on 2026-09-24: UCRT64 GCC 16.2, engine-only CMake/Ninja,
Debug and Release each pass **111 ordinary CTests plus two existing skips**
(Headland and AT DMA; 113 registered). The baseline at the start of D1 passed
110 ordinary tests plus the same skips. All 50 Python tool tests pass with the
local Debug SST probe and synthetic inputs. Provenance: 40 components/213
files, zero errors. Catalogue: 32 machines/five locales. Diff check passes.
MSVC remains unavailable locally; no new remote-CI result is claimed.
