# PCS286 private real-to-protected transition (D2)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Local, uncommitted continuation of C/D1 on `4769e40524bc194747b142f3e7ec908ae4df0897`.
No publication, new remote CI, public activation or machine acceptance.

## Sources and retained caches

Intel's [1987 80286/80287 PRM, 210498-005](https://bitsavers.org/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf)
section 10.4.2 (PDF185) describes LMSW followed by an intrasegment jump during
initialization. B-56..58 (PDF264..266), B-65..67 (PDF273..275) supply JMP,
LGDT/LIDT, LLDT and LMSW checks. B-57 was also visually inspected.
LMSW keeps PE sticky and changes no segment cache. Near JMP checks retained
CS; a subsequent direct far JMP loads a protected descriptor. No physical
prefetch queue or timing is modeled.

A [286-specific STOREALL experiment](https://rep-lodsb.mataroa.blog/blog/the-286s-internal-registers/)
reports raw access `82h` after reset/real segment loads, with ordinary hidden
caches behaving as writable data, including CS. We use this reported encoding
instead of the old zero placeholder. It is external experimental evidence,
not a new BluMach hardware observation. [Collins's cross-generation account](https://www.rcollins.org/ddj/Aug98/Aug98.html)
instead mentions `93h`; this discrepancy is recorded, with the narrower 286
report selected here. No STOREALL/LOADALL implementation or other reset claims
are adopted. No external implementation code was copied.

`bm_286_cached_descriptor` interprets exact `82h` only in ordinary hidden
segment caches. Table decoding still treats `82h` as an LDT descriptor.
After LMSW the retained caches remain usable; visible real SS selector bits
are not retroactively subjected to protected SS load checks. A subsequent
segment load applies ordinary protected rules. Real readback/exception tests
now assert `82h`, retaining their full architectural comparisons.

## Bounded integration contract

The uninstalled `bm_286_pm_step_subset` delegates PE=0 to the existing real
decoder, then uses the private positive opcode list at PE=1. D2 adds:

- Near JMP `E9/EB`: checked target, unchanged CS.
- Direct far JMP `EA`: nonconforming code at the same CPL, GDT/LDT lookup,
  RPL <= CPL, DPL = CPL, presence, IP limit, locked accessed-bit update and
  staged CS/IP commit. CS.RPL becomes CPL; FLAGS/SP/CPL are preserved.
- SGDT/SIDT/LGDT/LIDT: whole six-byte operand preflight and shared protected
  transfers; table loads commit after all reads. Base is 24 bits. Store byte
  five retains the existing undefined-value FF policy.
- SMSW/LMSW, CLTS and LLDT: required privilege checks before data access,
  sticky PE, GDT-only LDT load, null LDTR and guest fault delivery. LLDT does
  not update an ordinary segment accessed bit. LLDT in real mode and register
  forms of table instructions deliver #UD.

Guest rejection unwinds to the first prefix through the existing coordinator.
Host errors retain status/effects, release exclusion and stop without replay.
Whole-operand preflight prevents writes before a later range rejection.
Encoding-versus-privilege check ordering is functional policy, not measured
silicon exception priority. Timing stays UNKNOWN.

Conforming code, indirect transfers, gates/tasks, LTR, descriptor queries,
IOPL-sensitive instructions and remaining opcode families are pending.
Recognized deferred gate/task/conforming JMP targets stop as UNSUPPORTED,
without manufacturing a guest fault or simulated success. Public step/run/
clocked and the default internal PE path remain blocked.

## Observed synthetic validation

`pcs286-component.protected-transition` uses authored bytes and tables:

- Two aligned/odd-table programs start at actual reset without state import,
  execute LGDT/LIDT/LMSW/near JMP/far JMP, load SS/LDTR/DS, deliberately use
  null DS, execute a repair handler, discard the known error word, IRET and
  retry. First fetch is physical `FFFFF0h`.
- 16,384 direct-JMP access/CPL/RPL/GDT-LDT/alignment combinations, plus null,
  table/IP boundaries and presence priority.
- 64 system privilege/alignment cases; 64 whole table-operand boundary cases
  across DS/SS and expand-up/down; 128 protected MSW combinations; nine LLDT
  null/type/TI/presence/bounds cases; public refusal after real LMSW.
- 1,460 failures before/after every transfer on six routes (far JMP, SGDT,
  LGDT, LLDT, LMSW and near-JMP fault delivery), five host statuses and two
  alignments: exact retained memory/state, lock release and stopped no-replay.
  Prior D1 event and C access matrices remain enabled.

GCC 16.2 UCRT64 engine-only Debug/Release: 112 ordinary CTests pass per build
plus two existing Headland/AT-DMA skips (114 registered), assertions enabled.
Python: 50 tests. Provenance: 40 components/214 files. Catalogue: 32 machines/
five locales. MSVC is unavailable locally; published block-B CI does not
validate this uncommitted tranche.

## Next gate

Continue D with descriptor queries and instruction/operand privilege audit,
then aggregate stack, strings/REP, I/O, software events and restart paths.
Require joint access/event/instruction consumer review before public PE.
E/F privilege transitions/tasks, ED=1/FFFF ambiguity, REP errata, faulting-IRET
NMI timing, physical timing and complete PCS286 assembly remain open.
No ROM/media execution, BIOS shortcut or OS boot claim.
