# Intel 80286 private same-CPL protected IRET

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Scope and source

`bm_286_pm_iret` completes handoff block A at the **private helper** boundary.
The tests connect protected event entry, a synthetic handler action and return;
they do not execute a protected guest program. Both public/internal PE execution
barriers remain unchanged. Exception escalation, protected access checks and
instruction integration are still required before enabling public PE execution.
No firmware, board boot, physical timing or full-ISA claim follows.

Primary source: Intel, [80286 and 80287 Programmer's Reference Manual,
210498-005 (1987)](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf),
IRET B-51/B-52, system flags section 10.1, NMI section 9.2 and accessed-bit
section 11.1. The complete B-52 and 10-1 scanned pages were visually checked,
including the privilege inequality signs. The scan's physical PDF pages are
259/260 for B-51/B-52 and 179 for 10-1; HTML mirrors use different page offsets.
Consulted PDF SHA-256:
`ad487ba99b48cd9f61b14c0fe912a04c7cdb4c7c14a18419aa9faf62d8962460`.
The manual remains outside Git, with unknown distribution rights; only its
reference and hash are recorded here. No external emulator body was copied.
Existing CPU authors, licenses and derived-rewrite provenance remain intact.

## Architectural checks

The current NT flag selects the task-return path before ordinary stack access.
That path explicitly returns unsupported. A saved NT bit is instead restored
by an ordinary return and can cause a subsequent IRET to select task return.
No backlink, task switch or outer-privilege return is simulated.

For NT clear, Intel B-52 checks the second stack word, then return CS.RPL,
then the complete six-byte same-level frame. The helper preserves that ordering:

| Condition | Result |
| --- | --- |
| Second stack word outside cached SS bounds | #SS(0) metadata, no transfer |
| Return RPL below current CPL | #GP(return selector), before whole-frame check |
| Return RPL above current CPL | Unsupported outer return; no descriptor read |
| Same-level six-byte frame outside cached SS bounds | #SS(0), before CS lookup |
| Null CS, absent LDT or descriptor-table overrun | #GP(0) for null, otherwise #GP(selector) |
| Non-code CS or wrong privilege | #GP(selector), before presence |
| Valid code type/privilege but P clear | #NP(selector), before IP check |
| Return IP beyond inclusive code limit | #GP(0) |

Nonconforming code requires DPL=CPL; conforming code permits DPL<=CPL.
Execute-only code is valid. LDT index zero is a usable selector when the cached
LDTR and its bounds allow it. Error metadata clears RPL and retains TI; EXT is
zero for this instruction-origin fault. No recursive guest exception delivery
occurs. Inconsistent imported SS caches remain host invalid-state errors,
distinct from valid cached stacks with guest limit violations.

Frame bounds are contiguous, without wrapping an individual frame at offset
FFFF. Expand-down stacks require every byte above the limit. A valid frame
starting at FFFA ends at FFFF and the final SP becomes zero. Physical accesses
still wrap at 24 bits, independently of segment checks; A20 belongs to the board.
SS is used from its hidden cache and is never reloaded from the GDT/LDT.

CS cache, IP, SP and FLAGS commit together after all reads and the accessed-byte
RMW succeed. IRET consumes six bytes, never automatically discarding an error
code. The test handler explicitly removes the error word before returning.
Same-level return preserves DS/ES/SS, CPL and unrelated registers.

## FLAGS and event ownership

Section 10.1 permits IOPL restoration only at CPL0 and IF restoration only when
the executing CPL is no greater than the incoming IOPL. Other defined flags,
including NT, TF and DF, are restored at every CPL. The existing core's canonical
286 FLAGS policy supplies bit 1 as one and reserved bits 15/5/3 as zero. No 386
EFLAGS, VM or RF semantics are introduced.

On success the helper clears NMI blocking and preserves a pending NMI edge.
It does not sample TF, synthesize a pending trap, consume an interrupt shadow
or deliver an interrupt. Those instruction-boundary actions belong to the future
dispatcher. Tests explicitly preserve pending trap/shadow state even when the
restored flags differ, so private state tests cannot masquerade as completed
protected instruction execution.

The manual documents NMI re-enabling by IRET but the sources reviewed here do
not establish faulting-IRET unblock timing. This helper retains the existing
success-only architectural commit policy, including NMI blocking on rejection
or host failure. Fault-time NMI arbitration remains an explicit evidence and
integration gate; no later-x86 rule is silently adopted.

## Host contract and transaction policy

Inputs must be serialized, non-aliasing, internally consistent running protected
state, with no pre-existing LOCK. Reads use the existing synchronous bus endpoint.
Selector read, descriptor lookup, IP/FLAGS reads and locked accessed update are
a functional transfer policy, not a measured bus sequence. The selector is read
first to establish the architectural RPL/frame-check precedence. Every guest
check and frame read precedes the only write (the accessed byte).

The existing consumed load plan performs the locked byte RMW, rereading the
current access byte even if its initial A bit was set. Its other current bits
survive; the CS cache derives from the validated descriptor snapshot. Concurrent
replacement of an entire descriptor is not made atomic by this operation.

Every non-OK endpoint status is returned unchanged, with no guest fault metadata,
automatic retry or CPU commit. Only successful transfers contribute waits.
LOCK always releases. Completed endpoint effects, including a write whose
callback subsequently fails, survive; the caller must stop instead of replaying
the helper. Missing access callbacks are invalid arguments; absent lock support
refuses before the accessed RMW. No new public API, scheduler or CPU timing
constant is added.

## Validation on the continuation workstation

The imported bundle matched SHA-256
`7aa9c4dfc66d3d9921c87548b62c65a23049786a554fcc510d9f496ec13d2a14`.
Git verified complete history, HEAD `fd7a20afbb56105f9a6844de0f96293dd4528219`
and required ancestor `bcaa691c8f0ebca4778e2d75113af9d4e3db71e1`.
The verified integration base was `8e5cd917d95536fbdfe65ce2d5d658eda0633d20`
on `architecture/portable-engine`, 51 commits behind the imported HEAD.
[PR #209](https://github.com/BluMach/BluMach/pull/209) is documentation only;
the implementation history was imported from the bundle without rebasing.

Fresh GCC 16.2.0 UCRT64 Debug and Release engine-only builds reproduced 107
ordinary passes and the two existing Headland/AT DMA skips before emulation
changes. All 30 Python tests passed with the newly built SST probe configured;
without that probe one adapter integration test explicitly skipped. Baseline
provenance: 38 components/206 files, zero errors. Catalogue: 32 machines/five
locales. Local MSVC reproduction was unavailable: Visual Studio 2022 exists
but the C++ toolchain is absent and CMake cannot select an installed instance.
The prior workstation's MSVC results are historical evidence, not a local pass.

`pcs286-component.protected-iret` adds:

- 2,097,152 FLAGS cases: every saved word, four CPLs, four incoming IOPLs and
  both incoming IF states, using a separate per-bit permission oracle.
- 8,192 access-byte/CPL/RPL/GDT-LDT combinations, plus null selectors, table
  bounds, presence/type/privilege precedence and explicit unsupported paths.
- 1,572,864 stack-range cases: every SP for twelve limits in both growth
  directions, checked with a per-byte wide oracle, plus every 16-bit target IP.
- 2,000 injected failures: five host statuses, before/after every transfer,
  four CPLs and independently aligned/odd stack and descriptor locations.
  Every callback verifies no premature CPU mutation; partial A writes survive.
- 32 private interrupt/trap-entry/handler/return combinations, with/without
  error words, every CPL and both alignments. Saved NT is restored correctly.
- 512 current-access-byte RMW cases, physical wrap, stack/descriptor overlap,
  missing callbacks, NMI/TF/shadow ownership, independent instances and public
  PE rejection before fetch.

Tests use authored synthetic memory only. The initial test compilation caught
an incorrect call shape for `bm_null_host_services`; it was corrected without
relaxing warnings or assertions. Public real-mode behavior is unchanged.

Final local GCC UCRT64 Debug and Release each pass **108 ordinary tests** with
the same two explicit skips. The pinned optional SST selection still reports
65,843 matches, 5,156 pending and one revoked out of 71,000, with zero
discrepancies. All 30 Python tests, provenance (38 components/207 files),
catalogue and diff checks pass. MSVC remains unavailable locally as described
above; remote CI results must be read separately from the implementation PR.

Reproduction uses the handoff CMake options, fresh `build/handoff-debug` and
`build/handoff-release` directories, and the ordinary CTest/Python/provenance/
catalogue checks. Keep `-UNDEBUG` on Release tests. Optional pinned SST remains
a real-mode regression only; it cannot validate protected IRET.

Next: handoff block B exception classification/escalation and #DF/shutdown;
then C protected fetch/data/stack access and D instruction/event integration.
Keep task and outer returns unsupported until their own blocks E/F are complete.
