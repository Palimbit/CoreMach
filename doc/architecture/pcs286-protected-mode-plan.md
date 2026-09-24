# Intel 80286 protected-mode implementation plan

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

## Scope and integration gates

Implement architectural behavior inside the instance-owned 80286 component.
Do not extend the generic CPU or bus ABI with x86 descriptor concepts. Preserve
real-mode regressions, structured host errors, observable bus transactions and
explicit unsupported results until a path is actually implemented. No timing
accuracy claim, implicit 386 semantics or BIOS-specific success shortcuts.

| Block | Deliverable | Acceptance gate | Status |
| --- | --- | --- | --- |
| 1. Interpretation | Selectors, descriptor types and segment ranges | Exhaustive selectors/access bytes and boundary tests | Implemented, standalone helpers |
| 2. Tables | GDT/LDT lookup, table bounds, selector error metadata | Synthetic tables, unusable LDTR, boundary entries, every-transfer host failures | Implemented as private lookup; instruction integration in block 3 |
| 3. Segment loads | DS/ES/SS validation, cached descriptors, CPL/RPL/DPL, accessed bit; LLDT | Null selectors, presence, type and privilege matrices; no premature state commit | Common MOV/POP/LDS/LES state path implemented; PE dispatch and LLDT opcode pending |
| 4. Protected execution | Fetch/data/stack permissions, entry via LMSW and far transfer, instruction checks | Synthetic protected programs, bounds and privilege violations | Pending; enable only with block 5 |
| 5. Faults and interrupts | Protected IDT gates, exception frames/error codes, IRQ/NMI, IRET, nested failure/shutdown | Guest repair/retry, stack failures, double-fault paths, real-mode regression | Private same-CPL interrupt/trap entry and ordinary IRET implemented; escalation and dispatch pending; gates block 4 activation |
| 6. Privilege transfers | Call gates, conforming code, stack switching, parameter copying, RETF | Same/outer/inner privilege matrices and interrupted transfers | Pending |
| 7. Tasks | TSS, LTR, busy/backlink, task gates, task switches, NT return | Synthetic tasks, invalid TSS and nested-task tests | Pending |

The ordering is incremental development, not permission to expose broken
intermediate execution: blocks 4 and 5 share a public activation gate. Full
protected-mode support is not claimed until all seven blocks pass. Instructions
such as LAR/LSL/VERR/VERW must join the relevant table/privilege blocks rather
than remain disconnected success stubs. IOPL-sensitive instructions and system
instruction privilege checks belong to block 4. Task-switch fault ordering
belongs to block 7. Keep a per-instruction coverage ledger as each block lands.

## Current block 5 foundation (not public activation)

`bm_286_pm_enter_event` validates a complete IDT gate, software gate DPL,
presence, target selector/code, current stack capacity and target IP. It handles
same-CPL nonconforming code and conforming code at DPL <= CPL. CS RPL becomes
CPL. FLAGS/CS/return-IP and optional error code are pushed; TF/NT are cleared,
and interrupt gates additionally clear IF. SS is the existing cached stack,
not reloaded from its descriptor. SP zero can allocate at the top of 64 KiB;
frames crossing the segment boundary are rejected, including expand-down bounds.

The private caller supplies return IP, EXT/software origin and error-code
presence; it owns event arbitration, NMI blocking, INTA and instruction unwind.
Returned guest fault metadata is NOT delivered recursively. Task gates and
inner privilege transfers explicitly return unsupported. Public PE execution
still refuses before fetch. No protected access checks or escalation are implied.
The subsequent private IRET helper is described below.

Architectural preflight precedes all writes. Accessed-byte RMW precedes frame
writes as a functional transaction policy, not a pin-level ordering claim.
CPU registers commit only on success. Host failures preserve status and release
LOCK; partial descriptor/stack memory effects survive and must not be replayed.
Only successful bus transfers contribute waits. Timing remains unknown.
The helper requires serialized, non-aliasing inputs and no existing bus lock.

Source: [Intel 80286/80287 PRM, chapter 9 and INT appendix B](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf).
Tests are synthetic, including every transfer failure before/after effects;
they are not hardware traces or evidence that the PCS286 boots in protected mode.

## Block A continuation: private same-CPL IRET

`bm_286_pm_iret` follows Intel B-52's second-stack-word/RPL/full-frame checks,
validates code/privilege/presence/IP, restores FLAGS under section 10.1 and uses
the existing accessed-byte RMW. CPU state and successful NMI unblock commit only
after all transfers complete. Current NT and outer returns remain unsupported;
the handler must discard its own error code. Pending NMI/trap and shadow are
preserved for future instruction-boundary integration. Faulting-IRET NMI timing
remains an evidence gate, not an assumed later-x86 behavior.

[Source review and tests](pcs286-protected-iret.md) distinguish architectural
rules from transaction policy. Private entry/handler/return tests pass without
opening either public PE gate. Next work is exception escalation and protected
access checks, followed by their joint instruction/event integration gate.

## Block 1 contract

`descriptor_286.h` is private, not installed. Parsing accepts eight readable
bytes without alignment requirements and never changes them. It classifies
segment/system descriptors independently of presence, extracts 24-bit segment
bases and 16-bit limits, and preserves the final reserved word for diagnostics.
Gate payloads are intentionally deferred. Reserved bits do not acquire 386
meanings or invented fault semantics. Null selector detection is distinct from
LDT index zero.

The range helper checks a nonempty contiguous segment-relative range, without
16-bit wrapping. It does not check presence, read/write permission, CPL, RPL,
DPL, physical address formation or A20. It rejects non-segment descriptors.
Callers must perform the remaining access checks when integrated in later blocks.
No global mutable state, allocations, host calls, bus accesses or CPU changes.

Tests cover all 65,536 selectors, 256 access bytes, 65,536 reserved words,
all segment limits at boundaries, all offsets for five selected limits in
both growth directions, byte ranges against a separate wide per-byte oracle,
overflow, zero length, source preservation and a full 64-KiB code range.
Windows host tests are not evidence of execution on PowerPC big-endian.

## Block 2: table access

Private `bm_286_pm_lookup_descriptor` uses the existing `bm_bus_access_fn`
transaction contract with DATA reads, little-endian aligned words or split
bytes on odd bases. No direct RAM pointer, writes, accessed-bit update or LOCK.
Increasing-address transfer order is a functional policy, not a physical bus
trace. Addresses wrap to 24 bits; A20 remains a motherboard responsibility.
Only successful transfers contribute reported waits, matching the CPU path.
No partial descriptor is published on host failure; endpoint effects are not
rolled back and the helper does not retry.

The return status preserves host errors, including IDLE (not a continuation).
On OK, a separate reason distinguishes found, null selector, unavailable LDT
and table overrun. These are not automatically #GP: later instruction/task
callers choose fault vector or non-faulting query semantics. Selector error
metadata clears RPL; EXT is supplied by the caller. Cached LDTR.valid is
authoritative; this helper neither reloads nor validates the LDTR descriptor.
Presence/type/permissions of the fetched descriptor are left to block 3.

Tests enumerate every selector and every limit for GDT/LDT, including LDT
index zero; exercise odd/even transfers, physical wrap, and five host statuses
at every transfer before/after effects. Protected execution remains disabled.

## Block 3a: segment-load preparation

`bm_286_pm_prepare_load` connects lookup to ordinary DS/ES, SS and LLDT rules.
It returns either a prepared cache, a guest #GP/#NP/#SS with error code, or an
unchanged host status. It never delivers an exception or modifies CPU state.
It is NOT suitable for task switches or privilege-level stack switches, which
have different checks/faults. The instruction caller still owns operand-fetch
ordering and the SS interrupt shadow.

DS/ES accept data or readable code. Readable conforming code does not use the
ordinary data privilege comparison. SS requires writable data and matching
privilege levels. LLDT requires CPL zero and a GDT LDT descriptor; it ignores
descriptor DPL. Null DS/ES/LDTR produce an unusable prepared cache; null SS
requests #GP(0). Null hidden-field zeroing is emulator policy, not silicon
retention evidence. Type/privilege rejection takes precedence over presence.

The access byte is preserved; `needs_accessed_write` explicitly prevents a
prepared data/stack cache from being mistaken for a completed load. Accessed
writeback/exclusion and cache commit are supplied by block 3b below; instruction
dispatch and protected fault delivery remain pending. Tests enumerate 49,152 type/CPL/RPL/table/alignment cases,
48 null cases, before/after transfer failures, and invalid table/cache inputs.
Reference: Intel PRM B-61 (LDS/LES), B-66 (LLDT), sections 7.4 and exception
12 definition. See [LDS/LES](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=271),
[LLDT](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=276),
[segment types](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=138),
[stack faults](https://kitchen.manualsonline.com/manuals/mfg/intel/80287.html?p=221).

## Block 3b: accessed writeback and cache commit

`bm_286_pm_commit_load` consumes a prepared plan, performs an access-byte
read/OR-one/write under the existing lock callback, releases exclusion and
only then commits the destination cache. Null/LDTR plans do not write A.
All valid data/code plans request the RMW, even when the initial descriptor
snapshot had A set: a bus master might clear it before the locked read.
This corrects the earlier preparation-only optimization. The captured physical
access-byte address is independent of later table-register changes.

Intel PRM [11.1](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=189)
specifies an indivisible locked access-byte update. Byte-sized RMW after the
descriptor reads is our functional transaction policy, not measured pin timing.
The locked read preserves the current byte's other bits. It does not make
concurrent replacement of an entire descriptor transactional; the caller must
serialize the same instruction and must not nest an already-owned bus lock.

All non-OK statuses release the lock, preserve the destination and consume the
plan, preventing replay after possible endpoint effects. Only successful
transfers add waits. Missing lock support refuses before accessing the byte.
Tests cover 1,024 combinations of byte contents, initial A and alignment,
physical wrap, five errors before/after read/write, no replay, and no-write
null/LDTR loads. PE dispatch, SS shadow and fault delivery remain pending.

## Common load-path integration

MOV-to-ES/SS/DS, POP ES/SS/DS and LDS/LES now call the same private
`bm_286_load_segment_state` path. Real mode retains its previous cache values
and never invokes descriptor callbacks. Protected state uses preparation and
locked writeback/commit. LDS/LES general-register and POP stack-pointer changes
remain after successful segment loading; SS shadow stays instruction-owned.
No new public API or protected-step bypass exists.

The public PE gate remains before fetch, and the instruction execution helper
also still refuses PE. Tests exercise protected architectural states through
the common load path, not protected opcodes. Guest rejection metadata must not
be delivered through the existing real-mode frame. LLDT is supported by the
private state load path but its opcode dispatcher remains pending. Block 3
is therefore not complete as a protected instruction feature.

Added tests sweep all selectors for three real-mode destinations (196,608
loads), test protected read/write failures before/after effects for each target,
and preserve SP/IP/registers/shadow while checking cache commit. Existing real
MOV/POP/LDS/LES and LOCK suites exercise the actual connected instruction path.

## Next: protected execution and exception-entry integration

Connect lookup to DS/ES/SS and LLDT validation, with explicit permission and
presence checks, fault metadata and staged architectural state. Add accessed
writeback with the required exclusion rules only after their source review.
Keep public execution gated until protected fault entry is available.

Use the existing CPU bus path, not direct RAM access. Validate the complete
eight-byte entry against the table limit before any descriptor transfer. Use
the cached LDTR state and distinguish null/unusable selectors, architectural
fault metadata and host transport failure. Preserve error codes and transfer
ordering; never roll back external side effects or disguise a host failure as
a guest protection fault. Table reads do not themselves load a segment or set
its accessed bit. Test even/odd locations, final valid/invalid entries,
24-bit addressing, before/after transfer errors and session isolation.

## Evidence and completion

Use authored synthetic programs first: construct a GDT, request PE, far jump,
load segments, write an observable result and recover from a deliberate fault.
Then extend to interrupts, privilege transitions and tasks. Firmware boot is a
later integration check, not a substitute for these tests. Continue optional
real-mode SingleStepTests; they do not certify protected behavior.

Primary reference: Intel, *80286 and 80287 Programmer's Reference Manual*,
1987, 210498-005: chapters 6/7 (selectors/descriptors), 9 (interrupts),
11 (protection), appendix B (instruction/type tables), appendix C (386 differences).

- [Intel PRM scan](https://bitsavers.trailing-edge.com/components/intel/80286/210498-005_80286_and_80287_Programmers_Reference_Manual_1987.pdf)
- [Descriptor layout, section 6.5](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=118)
- [Table bounds](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=131)
- [Expand-down segment rules](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=190)
- [286/386 descriptor distinctions](https://tv.manualsonline.com/manuals/mfg/intel/80286.html?p=335)

Reused CPU implementation retains its existing authors and derived-rewrite
provenance. The new helpers have separate provenance; this is not a claim that
the complete emulator is independent of its historical source.
