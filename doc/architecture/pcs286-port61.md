# Port 61h: documented signal boundary

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`blumach_pcs286_port61` implements a private byte-wide AT port adapter at 61h.
It composes the existing PIT8254 and exact clock link, and requires explicit
board endpoints for refresh/error status and check enables. It is not the
complete public `pcs286_board.h` contract or a qualified PCS286 motherboard.
There is no successful default for missing board endpoints.

## Evidence takes precedence over functional legacy code

The user explicitly requires manufacturer documentation to win over conflicting
classic behavior. The classic is useful for reuse and finding discrepancies;
its ability to boot does not validate its hardware model.

The primary generic AT reference is IBM *Personal Computer AT Technical
Reference*, 6280070, September 1985. Canonical asset
`ibm-at-dma-technical-reference-1985`, SHA-256
`04e83b4df038ddc9d2c45f7465ff19c1ed5628ae9826d0e3066aadd2c0740276`.
It remains local-only outside Git. Consulted printed 1-22/23, 1-38/40 and the
Type 1 schematics; visually reviewed sheets 3, 10, 17 and 21, particularly
printed 1-92/96 (PDF108/112) and the timer block diagram (PDF39).

The more legible GC101/GC102 reference, canonical `headland-ht101a`, SHA-256
`c3e9f6e5d92ffe797df4ff74fe773ee167faa68b63cbca62b92f202415c0f1d0`, was consulted
on PDF2 and visually on PDF10: SPKR is the timer channel-2 output. This supports
the timer's role, but does not establish that every IBM discrete gate or the
GC103 classic profile matches this PCS286 board. The exact board still needs
qualification. OCR was navigation; circuit conclusions came from rendered pages.

The low-nibble write and raw OUT2 read path is derived from `src/port_6x.c` at
`4769e40524bc194747b142f3e7ec908ae4df0897`, preserving Miran Grca, rtzor/Project
BluMach and GPL attribution. The sources and public device cores stay unchanged.

| Signal | Documented AT boundary used here |
|---|---|
| Written bit 0 | Timer channel-2 GATE. |
| Written bit 1 | Speaker data; logical speaker drive is data AND OUT2. |
| Written bits 2/3 | Inverted outputs enable RAM parity/I/O check circuitry, forwarded as semantic enables. |
| Read bits 0–3 | Retained low-nibble latch. |
| Read bit 4 | REF DET from the refresh circuitry. |
| Read bit 5 | Raw OUT2, even when speaker data is low. |
| Read bits 6/7 | IO CH CK and PCK from their board circuits. |

These connections follow sheet 17's ALS175/ALS244, sheets 3/10's check signals,
and sheet 21's REF DET circuit. The adapter does not create parity-error latches
or complete refresh cycles: those are upstream producers of these signals.

## Discrepancies excluded from the port

The classic `pit_refresh_timer_at` flips bit 4 on a rising OUT1. Sheet 21 derives
REF DET through the actual refresh circuitry, separate from the timer request.
An OUT1 edge is therefore not evidence of a completed refresh. The initial local
draft inherited that shortcut; it was removed before validation after this
document review. No compatibility switch re-enables it. PIT1 belongs to the
refresh coordinator; the port accepts only PIT2 transitions. The subsequent
[functional refresh implementation](pcs286-refresh.md) now supplies that
coordinator without simulating DRAM circuitry.

The classic returns zero for error-status bits and retains bits 2/3 without
connecting the check circuits. This adapter instead requires a pure status
endpoint and a fallible check-enable endpoint. Missing endpoints reject
construction. Host failures propagate and remain host failures; the port does
not fabricate clear error flags or generate a guest NMI from a host error.

The classic PCS286 initialization selects `port_6x_olivetti_device`, whose
SWA flag implements M240 switch data at 62h. Its comment mentions 61h–63h/A20,
but the actual handlers do not implement such an A20 gate or a 63h mirror.
None of those comments establishes PCS286 wiring. This adapter decodes only
61h and introduces no 62h, 63h or 92h aliases, switch encodings or A20 effect.
Topcat read-count refresh toggles and PS/2 fixed-time refresh are also excluded.

The optional speaker callback reports digital transitions from the documented
AND. It does not reuse classic divisor-dependent muting, amplitude shortcuts,
host audio buffers or filters. Analog speaker/filter fidelity and audio output
remain separate work; there is no claim of audible output from this component.

## Ownership, time and failures

Caller-owned unused storage copies configuration and borrows PIT, its clock
link and stable callback contexts. Initialization publishes no mappings, pins
or callbacks. The owner supplies reset devices and check circuitry, connects
every PIT2 edge, and maps this adapter as a byte resource in the existing board
I/O decoder. PIT0 remains routed to the PIC. Status must supply only bits4/6/7;
an invalid bit mask is a host DEVICE_ERROR, not a fabricated port value.

Normal reads/writes synchronize the PIT first, including idle periods. A write
updates the low nibble, sets GATE2, publishes changed digital speaker output,
updates check enables and rearms the clock link. A gate-triggered callback sees
the new data bit. This order is a functional boundary policy, not propagation
delay modeling. Repeated same-level signals do not create new edges. There is
no read-count timing, host-clock dependence or implicit wait-state estimate.

DEBUG reads sample the pure board endpoint and cached PIT output without
advancing the PIT or consuming state. They may be stale relative to virtual time.
DEBUG writes reject. Transport validation precedes synchronization; waits are
preserved and converted by the outer decoder. Mutating callback reentry rejects;
state/DEBUG observation is allowed except recursive status sampling.

The first host failure stops ordinary accesses until full reset. Caller results
remain untouched, but accepted latch/gate/output/endpoint effects remain.
Accepted GATE effects are rearmed even if check-enable delivery fails. There
is no automatic retry, rollback or host-to-guest exception translation. DEBUG
can still inspect after a retained error. Full reset orders engine, clock link,
board status circuitry, then port reset before resuming. CPU-only reset preserves
the port and PIT. Destroy engine/mappings before borrowed link/device/context.

## Validation and remaining work

`pcs286-component.port61` checks 4,096 combinations of all write values,
external status bits and raw OUT2. It checks GATE and check-enable truth tables,
clocked speaker transitions, direct-data drive, disabled-speaker OUT2 readback,
1,000 reads without invented refresh, explicit REF DET changes independent of
PIT1, DEBUG purity, reset, reentry, two-instance isolation and rejected mappings.
Failure tests cover missing endpoints, status errors/invalid masks, retained
check-enable effects, clock-rearm overflow, unchanged caller results and no retry.
An authored integration uses the actual private AT decoder, PIT clock link,
PIC IRQ0/vector20 and 61h resource, including a propagated board-status error.
The status/check endpoints in this fixture are explicit synthetic signals,
not a production refresh/parity implementation.

GCC 16.2 UCRT64 Debug and Release each pass 139 tests with one public Headland
skip (140 registered). Python tools: 50 pass; provenance: 50 components and
264 files, zero errors; catalogue: 32 machines/five locales; diff check passes.
The first build of the new fixture lacked the existing contracts include target;
that test dependency was corrected. No functional test failure was hidden or
relaxed. No new MSVC, GUI, ASan, CI, hardware or firmware validation is claimed.

The subsequent [functional refresh delivery](pcs286-refresh.md) supplies pending
requests, bus ownership and logical REF DET events. Actual DRAM cycles are not a
functional prerequisite; precise stolen clocks remain outside the timing claim.
The subsequent [check/NMI component](pcs286-checks-nmi.md) supplies the error
capture and routing endpoints, including source-specific clear and global mask.
Remaining: qualify PCS286 signals, RTC/KBC and full memory/error-source and
machine composition/scheduling. No board factory
supplies synthetic success for missing pieces. CPU strict-clock remains blocked,
DMA mem2mem remains disabled for Headland, and public Headland acceptance skips.
