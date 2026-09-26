# Private 80286 protected accesses (block C foundation)

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

This local tranche adds cached-segment access validation and bounded bus
transfers. It does not enable protected instruction execution. Both existing
PE barriers remain closed. The next gate is instruction integration (block D),
including multi-operand ordering, stack/string restart and exception unwind.

## Source and interpretation

Primary source: [Intel 80286/80287 Programmer's Reference Manual, 1987,
sections 7.4 and 9.8](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf).
Printed pages 7-12/7-13 distinguish segment-load privilege checks from checks
on each subsequent access. The scan of 7-13 (PDF page 139) was visually checked.
The consulted PDF stays outside the Git checkout; no restricted document,
firmware or media is included. This is an authored implementation, with no
external emulator code copied and no new silicon measurements.

- Loading a segment checks type, presence and CPL/RPL/DPL. The existing loader
  owns these checks and the descriptor accessed-bit update. Later references
  use the hidden cache, without reading the GDT/LDT or updating A again.
- Unusable DS/ES fault with #GP(0). A valid cache is not invalidated merely
  because its visible selector is null or has different RPL bits. Unsupported
  imported CS/SS caches, missing presence in a usable cache, system types and
  impossible register/type combinations are host state errors. They do not
  fabricate a later #NP or task-switch fault.
- Execute-only CS can fetch but cannot be read as data. Code is never writable;
  read-only data rejects writes. Permission faults have #GP(0).
- Ranges are checked at their full effective width, before any transfer. SS
  range failures use #SS(0); other registers use #GP(0), even if they alias the
  same base/selector. A word at FFFF cannot wrap its second byte to zero.
- Expand-down data uses the strict interval `limit + 1 .. FFFF`, including
  when loaded in DS/ES. The scan also contains a contradictory sentence that
  recommends ED=1/limit=FFFF for a full stack. This implementation retains the
  explicit interval formula already used by the descriptor foundation (an
  empty interval at FFFF), not an invented special case. Resolving that source
  inconsistency against additional 286 evidence remains open.
- Fetch checks only requested instruction bytes. Speculative prefetch must
  not cause a code-limit exception. Instruction-length counting and actual
  multi-byte decode remain the dispatcher's responsibility.

## Private contracts and integration

`access_286.h` is private and does not extend the generic CPU/bus ABI.
`bm_286_pm_check_access` accepts wide offsets and lengths so callers can check
an entire aggregate range before effects. Empty ranges are invalid requests.
It returns permission/range metadata separately from host status, without CPU
changes or bus activity. Load-time privileges are not checked again here.

`bm_286_pm_access` checks and transfers 1..10 bytes using the existing bus ABI.
The bound is a private staging-buffer limit, not an FPU implementation or a
replacement for the instruction decoder. FETCH uses PROGRAM/FETCH byte
transactions; DATA uses little-endian aligned words or split bytes at odd
starts, plus a final byte if needed. Logical words are not re-paired across an
odd start. Each physical address wraps at 24 bits; A20 remains board-owned.

Reads are staged until all transfers succeed. Writes and other endpoint
effects already completed survive a host failure. Only successful transfers
contribute waits. All non-OK callback results retain their status and latch an
instance-owned stop flag; a second call cannot replay the failed operation.
An unrelated instance still works. Guest preflight rejection performs no bus
access and does not stop the instance. No host result becomes a guest fault.

LOCK ownership stays with the caller; this helper only marks DATA transfers
when the caller already owns the lock. FETCH cannot be marked locked. CPU
register commit, releasing that lock, multi-operand preflight, error delivery
and prefix-inclusive restart IP are caller responsibilities. Inputs must be
serialized/non-aliasing; callbacks cannot modify them during the operation.
The split/commit policy is functional behavior, not physical timing evidence.

Private exception entry and IRET now use the common SS range checker. Their
existing priority remains intact: entry's complete frame preflight and IRET's
second-word/RPL/full-frame sequence. Their stronger supported-current-stack
invariants and their existing bus/commit order remain explicit.

## Validation and remaining work

`pcs286-component.protected-access` includes:

- 12,288 register/access-byte/CPL/operation combinations, including visible
  null selectors, invalid caches, execute-only/read-only and conforming code.
- 19,267,584 range checks: every limit, applicable growth direction, all four
  registers, seven boundary/overflow offsets and widths 1..6. The oracle checks
  individual bytes with wide arithmetic. Full 64-KiB and UINT32_MAX ranges
  exercise overflow without relying on the implementation's range formula.
- Byte/word/multiword fetch/read/write, odd/even starts, 24-bit wrapping,
  preserved A20, explicit locked DATA attributes and staged reads.
- 4,500 before/after failures over every transfer and five host statuses;
  exact retained RAM, successful waits, unchanged CPU state, stop/no replay
  and independent instances are checked.
- Descriptor mutation after a load leaves the old cache usable; a failed
  #NP load preserves it. A successful reload sees the replacement.
- Three private fetch/SS/DS fault -> protected delivery -> synthetic handler
  repair -> error removal -> IRET -> access retry sequences. These are helper
  integrations, **not executed protected programs**.
- Required-byte fetch limits and invalid arguments/state. Existing protected
  entry/IRET/delivery and real-mode suites remain regression requirements.

The tested cache/transfer layer is ready for instruction integration. Still
pending: connecting each effective operand to it, multi-operand fault priority,
aggregate stack and REP/string restart semantics (including the documented
286 REP erratum), I/O preflight, real-to-PE far transfer, system/IOPL instruction
checks and the common instruction/event unwind. No string-program, physical
prefetch, faulting-IRET NMI timing, task/privilege transition or PCS286 boot
claim follows from these tests. Public PE activation requires those integration
checks and executable synthetic guest programs to pass together.

## Local validation snapshot

On 2026-09-24, UCRT64 GCC 16.2 Debug and Release each pass 110 ordinary
CTests plus the two existing Headland/AT DMA skips (112 registered). The clean
baseline before this tranche passed 109 ordinary tests plus the same skips.
Both builds use engine-only CMake/Ninja with asserts retained in tests.
The eight Python tool-test modules pass 50 tests; the SST adapter uses the
local Debug probe and synthetic inputs, without new external vectors.
Provenance audit passes for 39 components/211 files; catalogue validation
passes for 32 machines/five locales. `git diff --check` passes.

Changes remain uncommitted and unpublished on base
`4769e40524bc194747b142f3e7ec908ae4df0897`. Existing remote CI belongs to that
older published base, not this tranche. MSVC is unavailable locally, so these
new changes have no MSVC or remote-CI validation yet. No Actions files were
removed or altered, no PR was updated, and no ROM/media was executed.
