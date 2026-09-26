# Public functional protected execution

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Supported API profile

`bm_286_step` and `cpu.ops.run` now execute the reviewed C/D/E/F functional
profile in protected mode. The [joint review](pcs286-protected-full-joint.md)
preceded activation; its statement that public PE is blocked describes that
earlier checkpoint. This change does not certify the complete CPU/80287,
protected operating systems, silicon timing or a portable PCS286 firmware boot.

The mode dispatch occurs before real-mode event arbitration, HOLD and idle
handling. PE uses the existing protected access, decoder, delivery, privilege
and task mechanisms. LMSW retains the current segment caches until guest
reload; guest programs establish GDT/IDT/LDTR/TR and perform far transfers.
No generic CPU/bus ABI, structure version, configuration switch, BIOS shortcut
or diagnostic bypass was introduced. The old uninstalled test spelling
`bm_286_pm_step_subset` is an inline alias of public `bm_286_step`; there is
one protected executor, now static in the component.

`bm_286_step_clocked` still refuses before executing a boundary, returns zero
cycles, releases any retained exclusion and latches a host stop. Every
functional boundary has UNKNOWN timing. `cpu_cycles` is only the known bus
wait lower bound, never an elapsed duration for the native-clock scheduler.

## Budgets, stops and lifecycle

The existing `run` budget measures successful boundaries, not instructions,
CPU clocks or nanoseconds. A completed REP element, delivered exception or
external interrupt consumes one unit. Successful HLT or shutdown entry also
consumes one; subsequent idle reports IDLE with no consumption or trace.
On an error, `consumed` counts preceding successes and excludes the failing
boundary. Successful boundaries retain their existing trace callback.

A zero budget is an empty OK operation, even on a stopped instance. It
performs no bus access, acknowledges no event, and does not recover a stop.
Positive-budget run and step then still reject until reset. Invalid arguments
do not execute a boundary. Conformance state import at an idle API boundary
discards REP/decode continuation; it cannot clear a latched host stop. Reset
returns to real mode, clears task/event/continuation state and releases locks.
The existing rule against reset/import/destroy or nested execution inside
callbacks remains in force. Inputs may change through the signal API.

Endpoint failures, including endpoint IDLE, keep their exact host status,
completed writes/acknowledgements and signal changes. They do not become
guest exceptions. Exclusion releases and retries are latched off. Task-load
failures retain the context prescribed by the existing F contract. An actual
guest shutdown can accept an eligible NMI into a valid recovery context; a
host-stopped CPU cannot use NMI or import to evade its stop.

## Executable evidence

`cpu286_protected_full_joint.c` includes only installed public headers; its
target no longer has the private CPU include directory. It runs the twelve
authored reset programs from the joint review through both step and run(1):
ordinary and task privilege transfers, partial task repair, REP with
HOLD/NMI/IRQ, nested returns, IOPL repair, HLT, shutdown and task-NMI recovery.

- Each API runs 150,360 before/after bus-failure cases, preserving exact CPU
  snapshots, RAM effects, callback NMI and stopped-instance behavior: 300,720
  in total. Strict clocks are still checked at 768 reached PE boundaries per
  API, with no fetch, INTA or architectural mutation: 1,536 total.
- Twenty-four additional complete programs use run budgets 1, 7 and 256.
  Final CPU/RAM, every trace boundary and its waits, and lock/INTA counts match
  step execution. Trace callbacks drive only external signals. HOLD splits a
  run without losing pending NMI, and idle/zero budgets consume nothing.
- Ten before/after failures during a multi-boundary run verify two preceding
  successes, exact writes and NMI, failed imports/retries, instance isolation
  and reset followed by real execution. Twenty failures across both INTA
  phases and both APIs verify exact status, no guest frame and no replay.
- Existing instruction-family/privilege/task tests now reach the public path
  through the compatibility alias. Public LMSW-to-NOP continues in PE; public
  step followed by run resumes a retained locked REP without refetch, releases
  its lock on completion, and retains existing lifecycle checks.

Old fixtures whose PE case only asserted the public pre-fetch gate now test
the remaining strict-clock gate explicitly. Their other real-mode/unsupported
cases remain. No protected instruction assertion was removed to admit a new
behavior. Three obsolete assertions found during the first full run were
updated; no further CPU semantic workaround was required.

## Qualifications and next work

Final GCC16.2 UCRT64 Debug/Release validation: 125 ordinary CTests pass and
the two existing Headland/AT DMA skips remain (127 registered), assertions
and Werror enabled. Python50/50; provenance41 components/229 files without
errors; catalogue32 machines/five locales. No MSVC or new remote CI result.

The source decisions and functional policies of C/D/E/F are unchanged:
[instruction/fault policy](pcs286-protected-instruction-policy.md),
[task policy](pcs286-protected-tasks.md) and the joint review remain the
semantic authority. There is no new Intel interpretation or external code
intake in this API-routing change. Authors, licenses and provenance remain.
Reserved encodings/prefix combinations outside the positive profile still
return UNSUPPORTED. Populated 80287 execution, physical fault microstate,
short-TSS partial-image ordering, the 32-context host bound, SS/NMI inhibition
qualifications and logical LOCK/INTA timing retain their documented limits.

Functional public activation is complete for this profile. Next machine work
is the separately documented Headland/IOC02 and AT DMA implementations, then
board assembly and validation. Establish instruction/bus timing before any
strict native-clock scheduling or timing/boot claim. No ROM, external vectors,
media, restricted manual or other-emulator implementation entered Git; only
authored synthetic programs were executed. Work remains local and unpublished
against `architecture/portable-engine`, with PR209 as documentary context.
