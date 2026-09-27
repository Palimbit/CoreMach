# PCS286 private IOPL, FLAGS and scalar I/O (D4)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Local uncommitted continuation of C/D1/D2/D3 on `4769e40524bc194747b142f3e7ec908ae4df0897`.
The public PE gates remain closed; no new remote CI, timing, OS or machine
acceptance claim. No external implementation code, ROM or media was introduced.

## Source and scope

Intel [80286/80287 PRM 1987, 210498-005](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf):
10.1/10.3 (PDF179/183/184), CLI B-30 (PDF238), HLT B-42 (PDF250), IN B-45
(PDF253), OUT B-81 (PDF289), POPF B-86 (PDF294), PUSHF B-89 (PDF297), STI
B-106 (PDF314), and existing FLAGS/event contracts. Original read outside Git.

Documented: CLI/STI and scalar IN/OUT require CPL <= IOPL; failure is #GP(0).
HLT requires CPL0 regardless of IOPL. POPF does not fault merely for insufficient
privilege: it preserves IF when CPL > incoming IOPL and preserves IOPL outside
CPL0. NT is restorable at every CPL. Real POPF keeps its earlier NT/IOPL policy.
Section 10.1 and B-86 resolve the inconsistent IF sentence in section 3.11.1;
the implementation follows their explicit privilege rules, as does private IRET.

The private positive list adds PUSHF/POPF, SAHF/LAHF, CMC/CLC/STC, CLD/STD,
CLI/STI, HLT and the eight immediate/DX byte/word IN/OUT forms. Existing real
handlers are reused with explicit protected checks. LOCK and INS/OUTS/REP,
aggregate stack instructions, software interrupts and other opcode families
remain pending; this is not completion of all IOPL-sensitive execution.

## Integration contract

PUSHF/POPF use the common two-byte SS access and ignore segment overrides for
the stack. Range/expand-down checks precede bus effects. SP and POPF FLAGS commit
only after the complete word succeeds. Canonical reserved FLAGS behavior is
unchanged. Permission to restore IF uses the old IOPL, never the saved image.

CLI/STI check privilege before changing IF or the shadow. HLT checks CPL before
setting halted. Scalar I/O checks after fetching an immediate port byte, when
present, and before any device transfer. This ordering is functional policy,
not a silicon exception-priority measurement. No 386 I/O permission bitmap.

I/O retains the existing 16-bit port space, odd-word split, FFFF->0000 wrap,
staged accumulator update and successful-transfer wait accounting. Host failures
retain completed device/RAM effects and original status, stop the instance and
cannot be replayed. They never become guest #GP or #SS.

The existing functional STI shadow delays INTR for one following instruction;
it does not mask NMI or sampled TF. POPF creates no such shadow. TF sampling
uses the incoming instruction state: newly set TF takes effect after a following
instruction, while clearing TF does not cancel an already sampled POPF trap.
HLT wakes through the bounded event coordinator with the following IP saved.
Exact pin timing and repeated inhibition sequences are not certified here.

## Observed synthetic tests

`pcs286-component.protected-iopl` adds:

- 2,097,152 decoded POPF cases: every saved word, all CPL/old-IOPL combinations
  and both old IF values. Independent bit-by-bit expected result, complete
  architectural comparison and alternating SS alignment; no premature TF trap.
- 704 CLI/STI/HLT/scalar-I/O cases across CPL, IOPL, IF and alignment. Denial
  checks #GP(0), saved prefix IP/FLAGS and zero device transfers. Allowed cases
  cover immediate/DX ports, AL/AX, input/output and odd 16-bit port wrap.
- 64 PUSHF/simple-FLAGS cases, SS/expand-down boundaries and SP wrap, plus two
  executed POPF #SS/handler/MOV-SS/IRET/retry programs without fixture edits
  after execution begins.
- STI versus INTR/NMI/TF; POPF IF without shadow; TF set/clear sampling;
  idle HLT followed by IRQ/NMI wakeup; NMI latched by an I/O callback.
- 2,940 host failures before/after every transfer on 24 routes, five statuses,
  two alignments. Includes all newly allowed opcodes, privilege denial and
  stack-fault delivery. Exact retained RAM/device bytes, architectural state,
  lock release and stopped no-replay are checked.

GCC16.2 UCRT64 engine-only Debug/Release: 114 ordinary CTests pass plus two
existing Headland/AT-DMA skips (116 registered), assertions enabled. After the
full Debug run, a test-only old-TF-clear case was added and the affected target
rerun successfully; Release includes that case. Python: 50 tests; provenance:
40 components/216 files, no errors; catalogue: 32 machines/five locales.
MSVC is unavailable locally; existing published block-B CI does not validate D4.

## Remaining gate

Next: ordinary/aggregate protected stack operations and control-flow consumers,
then strings/REP, INS/OUTS, LOCK privilege/exclusion/restart and software events.
Keep joint access/event/instruction activation review, D3 query evidence gaps,
E/F tasks/privilege transfers, ED=1/FFFF ambiguity, REP errata, faulting-IRET NMI
and physical timing open. PCS286 chipset/device assembly remains a later gate.
