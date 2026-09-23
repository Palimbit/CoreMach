# PCS 286 AT fabric: first interconnect boundary

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: **architectural-boundary interconnect only**. This branch starts at
`87c3fb4876eaad086921bc3444569da026286c36` and implements the initial
`bm_at_bus` contract. It does not implement a cascaded PIC, dual DMA, ISA card
inventory, board decode, a PCS 286 machine or any firmware path.

The interconnect starts with CPU ownership. One external requester may raise
HOLD when LOCK is low; the coordinator supplies HLDA at a completed CPU
boundary. A competing requester receives `UNSUPPORTED` without altering the
existing request. Cancellation lowers HOLD immediately; if HLDA remains high,
neither CPU nor external accesses the bus until HLDA falls. A new request
during that stale HLDA level receives `INVALID_STATE`. Repeated pin levels do
not emit duplicate HOLD edges. Reset restores CPU ownership and clears LOCK,
HOLD and HLDA. This is a scheduling policy, not measured electrical timing.

The board-supplied decode callbacks own mapping, fragment splitting and
duration conversion. Their wait count is already in `requester_clock` units;
the interconnect forwards it unchanged. The CPU callback adapter supplies the
configured CPU rate. Direct transfers supply their own valid rate, allowing a
DMA controller to use a different native clock from the configured ISA rate.
The interconnect does not invent memory/ISA waits or convert them twice. A
DEBUG READ is passed to decode with its DEBUG attribute and returns zero
waits; the endpoint must avoid device side effects. A DEBUG WRITE is rejected
before decode. No denied access invokes decode or mutates its transaction.

## Source review for later PIC/DMA composition

These are reuse candidates and risks, not new AT implementations. Review base
commit: `87c3fb4876eaad086921bc3444569da026286c36`.

| Source at reviewed base | Reusable behavior | AT gaps and custody |
|---|---|---|
| `components/pc/src/pic8259.c` (last path change `74ae0150db87d8a46c0d50bfa619f1e2e09a9f18`; inherited `src/pic.c`) | Fixed priority, IRR/ISR/IMR, ICW programming, EOI and edge inputs provide a small reference. | Its own header calls it a single-PIC XT contract. It discards ICW3 cascade routing, has one-phase acknowledgement, and lacks complete spurious/trigger/rotation behavior. It registers a bus mapping in its constructor, contrary to the new build-then-publish lifecycle. A port or derived rewrite must retain Andrew Jenner, Miran Grca and BluMach notices and declare provenance. |
| `components/pc/src/dma8237.c` (last path change `b679f2a19fc3fffc577f19dad8348ee39782a10c`; inherited `src/dma.c`) | Channel register programming, byte flip-flop, masks, DREQ, count/terminal state and byte transfer logic are useful references. | Only channels 0–3; transfers synchronously through a mapped bus without explicit HOLD/HLDA grant or native-clock service. No upper word controller, cascade channel 4, full page behavior or AT timing. Constructor publishes its mapping. Retain Sarah Walker, Miran Grca, Fred N. van Kempen and BluMach notices for derived logic. |
| `components/pc/src/dma_page_registers.c` (last path change `5ea8f5c7fa3fd3d34f45e251e74d36df840933bd`) | Readable page latch versus wired address-bit distinction is useful. | It maps an XT-style bank and directly drives one four-channel controller. AT upper channels and board decode need separate evidence and tests. Its constructor also publishes a mapping. Same inherited notices apply. |
| `src/pic.c` (last path change `e22a58b4bd7e5a029ba37374e2e6e8b1a76d8686`) and `src/dma.c` (last path change `a62040ec9f87953fa1081aafd533af6639d3eca8`) | Existing AT-oriented cascade, two-phase acknowledge, upper DMA and page logic can inform a selective port. | Global state, direct CPU/device hooks and legacy timing make direct linkage unsuitable for independently instanced portable chips. Pin any copied logic to exact functions/commit and preserve upstream notices. |

The next P3 tranche must supply separate PIC and DMA implementations with
their own priority/spurious/cascade and byte/word/page/count/TC/mask/autoinit
tests. The interconnect tests alone establish no PCS 286 bootability or
hardware timing fidelity.
