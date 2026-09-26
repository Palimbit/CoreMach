# Functional AT refresh

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`blumach_pcs286_refresh` connects the timer request to a logical refresh event
and REF DET, using the existing AT HOLD/HLDA/LOCK arbiter. It is a private,
functional building block, not a complete PCS286 factory or a physical DRAM
simulation. The owner still has to compose the machine's service loop.

The user clarified the scope: reproduce effects relevant to software and device
interactions; do not reproduce every physical gate. This supersedes earlier
pending lists that required actual DRAM bus cycles before producing REF DET.
Manufacturer documentation continues to outrank contradictory classic code.

## Evidence and abstraction

IBM *Personal Computer AT Technical Reference*, 6280070, September 1985,
printed 1-23 and Type 1 sheet 21 (printed 1-96, PDF112), distinguishes OUT1's
request latch, CPU bus acknowledgement and refresh detection. Sheet 21 was
visually rechecked: OUT1 clocks the request latch; the REF DET flip-flop is
clocked by the refresh signal. This supports retaining one pending request
and toggling the detection bit when that request is serviced, rather than on
each raw timer edge regardless of bus ownership.

The more legible G-2 GC101/GC102 reference, canonical asset `headland-ht101a`,
PDF5, was visually checked: CPUHRQ requests the CPU bus for DMA and refresh,
and CPUHLDA acknowledges relinquishing it. PDF2 describes the refresh counter
and timer. These support the functional request/grant boundary, not a claim
that the GC103 profile or every PCS286 connection has been qualified.

Original documents stay outside Git. SHA-256:

- IBM asset `ibm-at-dma-technical-reference-1985`:
  `04e83b4df038ddc9d2c45f7465ff19c1ed5628ae9826d0e3066aadd2c0740276`.
- Headland asset `headland-ht101a`:
  `c3e9f6e5d92ffe797df4ff74fe773ee167faa68b63cbca62b92f202415c0f1d0`.

The new helper composes the existing portable arbiter, PIT clock link, CPU
signals and port61 adapter. It does not duplicate those components or alter
the classic. The classic OUT1 callback identifies the integration point;
its unconditional detection-bit toggle is not reused as the behavioral rule.
No external code, ROM or restricted documentation was imported.

## Implemented behavior

- Every rising OUT1 edge latches one pending request. Duplicate high levels do
  nothing; further pulses while pending coalesce instead of building a queue.
- `service()` preserves existing DMA8, DMA16 or ISA ownership. Once the bus is
  available it requests the new ownership-only `BM_AT_MASTER_REFRESH` master.
  LOCK defers HOLD; an old HLDA must fall before a new request can be granted.
- After matching HOLD/HLDA, one atomic logical event clears pending, toggles
  REF DET and releases HOLD. CPU ownership still follows normal HLDA release.
- REFRESH is rejected by the ordinary memory/I/O access API, including DEBUG.
  No dummy RAM/MMIO read, DMA channel, page register, DACK, address/count update,
  row-address counter or memory endpoint is used to simulate the event.
- Pure state inspection supplies bit4 to the board's existing port61 status
  aggregator. Reads and DEBUG do not themselves complete refresh or toggle it.
  Parity and I/O-check status still belong to their separate producers.

`bm_at_bus_arbitration()` is a pure snapshot, not a new grant operation. Existing
master enum values, data-transfer behavior and one-requester policy are retained.
The helper waits behind existing owners instead of interpreting an arbitration
conflict as a host failure. No preemption, starvation bound or chipset-specific
priority is asserted by this functional model.

There is deliberately no physical service duration. The event takes place at
an architectural boundary, with real ownership exclusion and CPU HOLD handling,
but without a claim of stolen-clock accuracy. Persistent byte-array RAM needs
no charge restoration. RAS/CAS, decay, analog margins and row counters have no
modeled consumer here and are omitted. Accurate refresh duration/priority can
be added if timing-sensitive behavior requires it; they are not prerequisites
for this functional path. The strict CPU timing gate stays closed.

## Lifetime and scheduling

Caller-owned storage borrows a reset bus and requires reset PIT OUT1 low.
REF DET starts at zero as deterministic emulator policy, not documented silicon
power-up. Full reset orders engine/PIT links and bus before this helper and the
port adapter. Reset rejects an active/locked bus; CPU-only reset preserves the
timer, pending request and detection state. There are no allocations, mappings
or retained external event callbacks in the helper.

PIT callbacks only latch inputs. The board calls `service()` after timer
synchronization and after CPU acknowledgement, never recursively executing the
CPU from a pin callback. `IDLE` means no event completed; it may have raised
HOLD. `OK` means exactly one event completed. Callback mutator reentry is rejected,
while read-only inspection sees the state published before HOLD release.

The future machine scheduler must visit possible service boundaries; it cannot
advance arbitrarily across many periods and then pretend all refreshes occurred.
Pulses genuinely deferred by ownership coalesce. This is an explicit scheduling
obligation, not a claim that the complete machine loop already exists.

## Validation and next work

`pcs286-component.refresh` checks 1,000 coalesced edges, repeated levels, LOCK,
delayed/stale HLDA, exclusive DMA8/DMA16/ISA ownership, rejected fake refresh
transfers, reentry, invalid arguments, reset and independent instances.

The integration fixture uses the actual clocked PIT, AT bus, portable 286 and
61h adapter. An authored `NOP; IN AL,61h; HLT` program observes the completed
event. A half-millisecond delay accumulates timer requests without changing
REF DET; 1,000 ordinary plus 1,000 DEBUG reads cannot invent completion.
Two owner scheduling granularities (113 and 1,000 ns, below a refresh period)
produce the same independently calculated 67 events over one millisecond at
the fixture's declared 1,193,182-Hz rate and divisor18, including the programming
edge. The DMA/ISA contention actors exercise real bus ownership and accesses;
they do not claim physical DMA-controller refresh interleaving. Error/check
endpoints remain explicit synthetic fixtures, not implemented parity/NMI.

GCC 16.2 UCRT64 Debug and Release: 140 pass and one public Headland skip,
141 registered. Python: 50 tests; provenance: 51 components/267 files, zero
errors; catalogue: 32 machines/five locales; diff check passes. No BIOS/media,
MSVC, GUI, hardware or CI validation is claimed for this delivery.

The subsequent [check/NMI component](pcs286-checks-nmi.md) now provides functional
error capture and routing through the existing port61/CPU interfaces.
Next: RTC/KBC and complete board scheduling and memory/error-source composition.
Exact PCS286 chipset qualification remains
separate. Analog circuitry is not a prerequisite for functional emulation;
audible speaker output is still unimplemented. Public Headland acceptance
remains pending and DMA mem2mem stays disabled for Headland. All work is local.
