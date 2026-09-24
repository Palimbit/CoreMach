# Intel 80286 private protected exception delivery

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Scope and evidence

Handoff block B now has a private, bounded delivery coordinator connected to
the existing same-CPL IDT entry and IRET helpers. It classifies event origin,
selects return IP/error-word policy, escalates guest entry rejection, exposes
shutdown and permits an eligible NMI recovery attempt. Public `step` and the
instruction execution helper still reject PE. No protected guest program,
task switch, 80287, firmware boot or physical timing is demonstrated.

Source: Intel [80286/80287 PRM, 210498-005 (1987)](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
9.1-9.2 (origin/EXT, NMI), table 9-4 (priority), 9.6.2 (double fault),
9.6.6-9.7.1 (error words, instruction restart, single step), 11.6 (shutdown),
B-8/B-9 (exceptions) and MOV B-73 (SS inhibition). Complete scanned pages 172
(9-10) and 216 (B-8) were visually inspected. The same original/hash and
outside-Git custody as [the IRET review](pcs286-protected-iret.md) are retained.
The source's instruction restart caveats are not erased by this delivery API.
The originating instruction implementation must supply correct architectural
unwind and address information; #DF and an offending #9 task are not restartable.

The exception algorithm is authored from that source, reusing the project's
private helpers. Existing CPU authors, licenses and derived-rewrite history
remain intact. No external emulator implementation or restricted asset is added.

## Origin and return policy

`bm_286_pm_deliver` receives a private request and instance-owned host-stop
latch. The request distinguishes a detected exception, a software interrupt,
and a boundary sample. Classification uses that origin, not just the vector:
`INT 8` and INTR vector 8 are not a double fault and do not push an error word.

| Origin | Saved IP | Error word | EXT for entry failures |
| --- | --- | --- | --- |
| Detected #0/#5/#6 | First instruction prefix (`restart_ip`) | None | 0 |
| Detected #7/#16 | `restart_ip` | None | 1, per Intel's extension-origin rule |
| Explicit #8 | `restart_ip` | Zero | 0 |
| Detected #10/#11/#12/#13 | `restart_ip` after caller unwind | Supplied fault metadata | 0 |
| Software INT/INT3/INTO | `next_ip`; failed entry returns to `restart_ip` | None, for every vector | 0 |
| Pending single step | Current boundary IP, after completed instruction | None | 1 |
| NMI or accepted INTR | Current boundary IP | None | 1 |
| Pending extension overrun #9 | Current boundary IP, not the old NPX instruction | None | 1 |

Selector/error metadata is retained through escalation: IDT is bit 1, TI/index
retain their descriptor meaning and EXT follows the initiating origin. Explicit
no-information #SS(0)/#GP(0) results stay zero; #DF always receives zero. No
386 page faults, debug-register causes, VM/RF or later exception matrix is used.
Unsupported/reserved detected causes stop before a bus call. #9 is accepted
only through boundary arbitration, not as a synchronous instruction fault.

## Bounded escalation

An entry rejection before any write may select another guest handler:

- First exception #0/#10/#11/#12/#13: the next protection violation selects #8.
- #1/NMI/software INT/#5/#6/#7/#9/#16/INTR: first deliver the new protection
  exception; only failure of that contributing exception selects #8.
- A guest protection violation while entering #8 asserts shutdown.

The helper attempts at most three entries: original, protection exception, #DF.
Its result records attempted vectors/error words and reports a delivered vector
only on successful entry. A later fault *executing handler instructions* starts
a new delivery; it is not an automatic double fault. There is no recursive call
or persistent "inside a handler" double-fault flag.

Same-CPL entry currently rejects with #NP, #SS or #GP before writes. Consequently
only one attempt writes an accessed byte/frame. A transport error exits
immediately and never becomes another exception. Task gates and inner privilege
transfers remain unsupported, including an otherwise valid task gate for #DF.
That implementation gap is not shutdown. Intel recommends a task gate to obtain
a reliable #DF context; synthetic same-CPL #DF tests do not substitute for TSS
implementation or make a damaged stack recoverable.

## Event and signal ownership

An explicit instruction fault/software request takes precedence over boundary
events. Boundary priority is sampled #1, NMI, pending #9, INTR. `trap_pending`
is authoritative; TF is not resampled from FLAGS. SS inhibition blocks those
boundary events, STI inhibition blocks INTR only, and IF masks INTR only.
An idle poll consumes no shadow or pending event. HOLD and completed-instruction
TF sampling remain caller-owned until dispatcher integration.

The private #9 input is a caller-owned pending indication for synthetic tests.
It is consumed by its future owner only if accepted as `vectors[0] == 9`.
The core still has no populated NPX or #9 signal API; #16 input metadata likewise
does not claim floating-point execution or detection of real NPX errors.

NMI consumes its accepted edge before bus callbacks; an edge raised during
delivery remains pending. Successful delivery (including an escalated handler)
blocks further NMI until IRET. A host error restores the accepted pending edge
and stops the private delivery state. Guest failure of a shutdown-recovery NMI
leaves shutdown asserted and NMI blocked; no repeated recovery attempts occur.

Shutdown clears halt, pending trap and the interrupt shadow, and notifies the
board only on a transition. INTR/#1/#9 do not wake shutdown. An eligible NMI
attempts entry with staged CPU state while shutdown remains externally visible;
success deasserts shutdown and retains PE. Guest rejection retains shutdown,
without escalating that recovery attempt. Reset remains the other exit source;
the existing CPU reset owns reset of architectural state and pins. Reentrant
reset/state import is prohibited during synchronous callbacks.

Host failures do not fabricate a shutdown transition, including failure during
#DF or NMI recovery. Saved CS/IP/SP/FLAGS commit only after all frame transfers.
Only entry-owned fields are committed, preserving NMI edges latched into the
original state by callbacks. These staging and fault-time signal choices are
functional policies, not measured silicon timing; faulting-IRET NMI timing
remains the separate evidence gate documented in the IRET review.

## INTA, LOCK and host failures

Only a selected INTR calls the controller, phase 0 then phase 1 exactly once.
Escalation uses the captured vector and never repeats INTA. The inherited
B-2/later functional exclusion spans both phases through the first complete
stack word, including both fragments of an odd word. An accessed-byte RMW may
nest inside that exclusion without an extra physical LOCK transition. All
protected descriptor/frame transfers inside the window carry LOCKED. If entry
escalates before writing, exclusion continues to the first resulting stack word;
host failure/unsupported/shutdown releases it. This extension is functional bus
ordering, not a claim about descriptor/pin sequencing on physical hardware.

Successful transfers alone contribute waits. Every non-OK accepted transfer
retains its original status, completed external effects and an instance-owned
stop latch. A second delivery call is refused before any endpoint or INTA call.
Clear that latch only on actual CPU reset or explicit stopped-state import,
never to replay a failed operation. Missing required callbacks reject before
acceptance; no-event IDLE is distinct from a latched endpoint IDLE failure.

## Validation and next gate

Baseline reproduced on `ada7a285747d`: 108 ordinary CTests in each GCC UCRT64
Debug/Release build, with the existing Headland/AT DMA skips. All 17 remote
checks on that preceding commit subsequently completed successfully.

`pcs286-component.protected-delivery` covers:

- All 256 detected-cause inputs at four CPLs and both alignments; every
  software/INTR vector with and without an initial missing handler.
- Documented escalation classes, #DF failure, IDT/TI/EXT and DPL precedence,
  zero error words, invalid stack versus invalid imported state.
- 576 pending-event/IF/shadow/halt/shutdown combinations, plus #1/NMI failure
  chains, no accidental INTA, lock window and new NMI edges from callbacks.
- 11,800 before/after failures over five host statuses, six delivery paths,
  four CPLs and both alignments. Exact retained RAM effects, waits, CPU staging,
  lock release, no shutdown fabrication and no replay are checked.
- Shutdown NMI success/failure and blocked retry, unsupported #DF task gates,
  independent instances, missing callbacks and eight private repair/IRET/retry
  sequences. Existing public PE-refusal and real-mode regressions stay enabled.

Reproduction uses the existing `build/handoff-debug` / `build/handoff-release`
engine-only configurations. Both pass 109 ordinary tests plus the two existing
skips. All 30 Python tests pass; provenance covers 38 components/208 files with
zero errors, and the catalogue remains valid at 32 machines/five locales.
Local MSVC remains unavailable; current remote CI is reported separately on
draft PR #210. Tests use authored synthetic data only. The preceding optional
SST run is historical real-mode evidence, not a test of this protected tranche.
The first remote MSVC build caught C4244 in the test trace's narrowing address
parameter. The trace now retains the bus contract's 64-bit address and separately
asserts the CPU's 24-bit limit; `/W4 /WX` and all assertions remain enabled.

Next is handoff C: protected fetch/data/stack access checks. D must join those
checks, instruction restart, TF/HOLD/event sampling and these delivery helpers
before either PE execution barrier can be removed. E/F remain privilege/task
work. Timing, NPX implementation and a complete PCS286 are separate milestones.
