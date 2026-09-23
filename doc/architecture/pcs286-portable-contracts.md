# PCS 286 portable component contracts

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Status: **draft contracts only, 2026-09-23**. The public declarations compile;
there are no 286, Headland, AT-platform or PCS286 implementations in this change.
The machine is not registered with the runtime or frontend and cannot boot.
These headers are an implementation boundary for the next test-first work,
not a stable ABI, firmware validation, or a completed hardware design.

Base: `90a36ab53488fe209fd29da43fbb81dbc9b96a9f`, branch
`feature/pcs286-portable-contracts`, integrating later into
`architecture/portable-engine`. The existing PCS86/M15 implementations and
engine/runtime public contracts are unchanged.

## Evidence and identity

The canonical record is `olivetti/pcs286`, machine ID `pcs286`, manufacturer
`olivetti`, family PCS. Consult its current README, manifest and worklog before
implementing a task. Do not substitute the separate TI/OLIMCU PCS286S board.
The public historical identity remains in [PCS 286](../machines/olivetti-pcs286.md).
That older public page contains superseded KBC-ROM and RAMDAC statements; the
current canonical record takes precedence.

| Part | Current evidence and implementation boundary |
|---|---|
| Intel 80286 | 12 MHz documented baseline; separate interpreter, 24-bit physical addresses, real and protected mode. No V30 model flag pretending to be a 286. |
| Headland GC10x | GC102 + IOC documented on the equivalent Dario/P35 platform; exact PCS286 marking/revision remains qualified. Use GC101A/GC102-family development references, not a claim that any GC103 board is identical. |
| RAM | 1–4 MiB onboard; 1/2/3/4 MiB validation matrix pending. Up to 16 MiB is documented with an unidentified expansion, not a supported invented ISA card. |
| IOC02 / board glue | Inherited decode and reverse-engineering evidence; undocumented bits require investigation. Ports 61h–63h are board glue, distinct from IOC02 68h/6Ah/6Ch. |
| Interrupts / DMA / timer | AT-compatible functions from inherited board composition; exact integrated/discrete part attribution requires primary evidence. Two cascaded PICs and byte/word DMA are necessary behavioural boundaries. |
| RTC | 128-byte physical CMOS evidence. MC146818-compatible programming boundary is a candidate AT model, not proof of a discrete Motorola package. |
| Keyboard | M5L8042-243P identified; authentic MCU firmware is missing. `62410C62.BIN` is an x86/Ontrack artifact, never valid controller firmware. Use an explicitly behavioural Olivetti controller profile. |
| Video | PVGA1A, 256 KiB, IMS G171 per corrected canonical record. Older G176P-40 attribution is not the chosen fact. Existing portable PVGA1A is a reuse candidate with incomplete timing/DAC fidelity. |
| Floppy | WD37C65B and 3.5-inch 1.44 MB documented. Current generic 765 can supply reusable logic after an evidence/contract audit. |
| Fixed disk | IDE/ATA, Conner CP3022 615/4/17 or CP346 805/4/26 documented. XTA from PCS86 is not this interface. |
| Serial / parallel | Existing NS16450/SPP register models are candidates; exact integrated controller identity and decode must be verified before availability is advertised. |
| Expansion | ISA resource/slot records are drafted. Physical count and connector widths must be verified; the PCS86's three 8-bit slots must not be copied. |
| Optional 80287 | Explicit later milestone: ESC/WAIT, BUSY/ERROR/PEREQ/PEACK and IRQ routing need contracts and tests before claiming a populated coprocessor. Initial machine is without 80287. |

Local references (metadata only; no assets copied into Git):

- `headland-ht101a-ht102`, SHA-256 `7ba3b16fed836311097cfa608306823e39b1b5f9646536f54437a44a8c94ac26`;
- `headland-ht101a`, SHA-256 `c3e9f6e5d92ffe797df4ff74fe773ee167faa68b63cbca62b92f202415c0f1d0`;
- `brochure-pcs286` and `brochure-pcs286-de-1991`;
- the Dario 286/P35 manufacturer technical manual in the canonical
  `triumph-adler/dario286` record (equivalent-platform evidence, not an
  authentic Dario BIOS or an automatic equivalence of every board revision).

Firmware metadata:

| Role | Bytes | SHA-256 |
|---|---:|---|
| BIOS 1.42 combined reference | 131072 | `afbd051666869f3f58f23e52f9dd468fb9ad9f629d2dbccfda5324e83a897621` |
| BIOS 1.42 low/even | 65536 | `8bde0d14c7b42328ca27c4eb6f355de20b5aeaeaeeac3f5d51dc21cb70cb0e57` |
| BIOS 1.42 high/odd | 65536 | `10f1d90ba9f9eafa7d89cc5cc48c605d0b4750df5c91d6127511ce1a74698ad1` |
| BIOS 1.37 combined, archive label 1.34 | 131072 | `888db2d5a9ed4f5c39b8167fb25f9ad26a1a42dddfe0ba2b7c1ccdafa04905d4` |

Firmware is supplied as immutable blobs. The expected low BIOS window is
E0000h–FFFFFh. The reset fetch at FFFFF0h must reach the appropriate ROM bytes;
the precise high-address board decode needs verification, not a CPU-wide
20-bit mask. The earlier files named video-at-E0000 and BIOS-at-F0000 form
the older system firmware and must not become a fabricated C0000 option ROM.

## Composition and ownership

```text
Runtime session (clocked, nanoseconds)
  +-- PCS286 composition and board signals
  |     +-- Intel 80286 (native cycles, real/protected mode)
  |     |     +-- AT arbitration/interconnect
  |     |           +-- Headland -> RAM / system ROM / external memory decode
  |     |           +-- I/O decode -> independently owned components
  |     +-- cascaded PIC <--- PIT0 / KBC / FDC / RTC / ATA / ports
  |     +-- dual DMA + pages <--- FDC DREQ2; bus grant from arbiter
  |     +-- IOC02 and 61h-63h glue -> A20 / reset / parity / refresh / speaker
  |     +-- keyboard protocol <-> 8042 -> IRQ1 / A20 / reset request
  |     +-- WD37C65B <-> floppy drives <-> caller-owned block media
  |     +-- ATA PIO <-> caller-owned block media
  |     +-- PVGA1A + single DAC owner -> neutral framebuffer
  |     +-- optional ISA resources (construction time only)
  +-- rational clock links -> native device edges and next deadlines
```

The machine owns components, mappings and wiring. Each chip owns its mutable
state; components do not know a runtime session, frontend, BIOS address used
as an execution heuristic, or another machine's globals. Shared RAM/ROM bytes
belong to the machine, Headland owns the routing rules, and the AT interconnect
owns arbitration and wait-domain conversion. Code reuse must preserve authors.

All new device constructors allocate through copied host services and clear
their output pointer on failure. Configuration structs are copied; referenced
blobs, media, callback contexts, child devices and ISA descriptors remain
borrowed for the documented lifetime. No constructor executes firmware or
silently registers an incomplete component with a bus or scheduler.

Construct chips first, wire stable contexts second, publish mappings and timed
sources last. The generic bus has no unmap and the engine has no source-removal
operation. On failed machine creation, retain the partial machine in the
runtime output so runtime can destroy its engine before freeing components.
An individual failed attachment must not publish a callback to freed state.
After engine destruction, release clock links, CPU/board/device objects and
finally the referenced RAM/ROM/media wrappers. Never free the borrowed media.

All callbacks are synchronous, single-threaded and non-reentrant. Pin outputs
use semantic asserted/deasserted values, independent of physical active-low
wiring. No callback directly calls reset/run on a CPU that is executing it.
I/O-triggered reset is latched for a safe architectural boundary. User reset,
CPU-only reset, reset of a peripheral and loss of battery power are distinct.

Host allocation/release and the CPU access endpoint are mandatory. Pin/trace
observers may be NULL for isolated component tests; a production composition
must verify that every required board connection is wired. CPU interrupt
acknowledge is required before INTR can be used. No missing keyboard/media
endpoint may synthesize a successful response. Validate arguments and buffer
sizes before allocation or mutation; destruction accepts NULL. These draft
structures are native C API values, not portable byte-serialized state.

Inspection and DEBUG transactions have no guest side effects: no FIFO pop,
status-C clear, IRQ acknowledgement, latch change or time advancement. A chip
may reject a debug operation it cannot inspect safely. Guest-visible failure
(empty drive, command abort, CPU exception) uses hardware state/protocol.
`BM_STATUS_*` errors represent invalid host contract use, missing implementation
or a host capability failure; they must not substitute for normal guest errors.

## Time and bus boundaries

The existing engine contract remains unchanged: scheduler time is nanoseconds,
each CPU/device has a rational native clock. CPU step reports elapsed native
CPU cycles, including wait cycles exactly once. Headland reports memory-clock
waits; the interconnect converts them, with retained phase, to the requesting
CPU or DMA domain. Requests carry their master identity and requester rate.
No floating-point wall time, arbitrary delay loop or guessed fixed CPU cycles.

The initial scheduler is architectural-boundary based. This scaffold does not
claim T-state bus arbitration, cycle-exact concurrent CPU/DMA execution or
cycle-exact video. HOLD/HLDA and LOCK have explicit boundaries so later work
can improve fidelity without using host threads. If a test requires finer
ordering than the scheduler provides, record that limitation and propose the
smallest shared-engine extension before implementing a shortcut.

Each timed chip exposes `advance(native_cycles)` and `next_deadline()`. OK
requires a positive next delay; IDLE means zero/no transition. Clock links own
the single serviced-cycle cursor, synchronize before I/O/input and rearm after
mutations, including when a formerly idle device receives a command. A debugger
must never synchronize by mutating chip state. The machine maps synchronized
I/O wrappers rather than the raw chip functions. DMA is serviced on granted
requests through its own board-owned timed source; do not add an always-active
poll loop. Its consumed clocks include the DMA memory transaction waits.

The documented 6 MHz mode remains a follow-up because the current engine has
no public runtime CPU-rate-change API. Do not implement it by changing the
meaning of a tick, multiplying instruction counts or restarting the engine.
Memory waits at 12 MHz also need evidence; empty timing data is not zero wait.

## Candidate board routes to prove

These are implementation planning routes based on AT composition and the
inherited PCS286 code, not a newly certified motherboard schematic.

| Owner | I/O candidate | Signals |
|---|---|---|
| PIC pair | 20h/21h, A0h/A1h | slave INT -> master IRQ2; CPU INTR/INTA |
| DMA pair and pages | 00h–0Fh, C0h–DEh even ports, 80h–8Fh decode | channels 0–3 bytes, 4 cascade, 5–7 words |
| Timer | 40h–43h | OUT0 IRQ0; OUT1 refresh/glue; OUT2 speaker/glue |
| KBC | 60h, 64h | IRQ1, board A20 source, CPU reset request |
| Board glue | 61h–63h | parity/I/O-check NMI, PIT2 gate, refresh status |
| IOC02 | 68h, 6Ah, 6Ch | selector-dependent outputs, evidence pending |
| RTC/CMOS | 70h/71h | IRQ8, NMI mask |
| Floppy | owned WD offsets near 3F0h–3F7h, excluding 3F6h | IRQ6, DMA2, terminal count |
| ATA | 1F0h–1F7h, 3F6h | IRQ14; 16-bit data versus 8-bit task file |
| Video | existing PVGA1A windows/ports, DAC at 3C6h–3C9h | framebuffer; exact scan timing pending |
| Serial/parallel | verify board addresses before enabling | expected AT routing, not assumed physical IC identity |

One owner per register/window. ROM shadowing/remapping goes through a routing
layer, not overlapping unconditional mappings. A20 handling applies to the
documented requester, not indiscriminately to DMA or ISA masters. The ISA
descriptor states width, resources, IRQ/DMA claims and physical slot ID; the
future composition validates all claims before publishing any of them.

## Reuse and independent work packets

Next step is a contract-test PR, followed by bounded implementation agents.
No agents were launched for this scaffold. Their branches should start from
the reviewed scaffold on the portable integration branch. An implementation
must not merge merely because its headers compile or one BIOS reaches POST.

| Packet | Owned area | Depends on | Required acceptance evidence before integration |
|---|---|---|---|
| P0 contracts/tests | coordinator; contract headers and future `tests/components/pcs286` | this scaffold | resolve draft semantics, authored synthetic fixtures, error/ownership/clock tests that fail against missing behaviour; no BIOS blobs in Git |
| P1 Intel 286 | `components/cpu/80286/` | P0; mock bus/INTA | reset and hidden CS base; full opcode classification; FLAGS/stack/string semantics; protected descriptors, limits, privilege, gates, task switches, faults/double faults/shutdown; NMI/INTR shadows; absent FPU; native timing provenance |
| P2 Headland | `components/chipsets/headland/` | P0; mock backing storage | documented register reset/readback, RAM banks, low/high ROM, shadow/EMS/remap enable/disable, A20/requester handling; no permanent POST alias |
| P3 AT fabric | `components/pc/` AT bus/PIC/DMA | P0; mock CPU and devices | cascade/EOI/spurious vectors, channel-4 cascade, 16-bit addressing/page/count/TC, mask/autoinit, LOCK/HOLD and conflict validation |
| P4 timer/RTC | `components/pc/` 8254/RTC plus their clock links | P0 | count/status readback, exact chunk equivalence, gates, IRQ transitions, RTC UIP/BCD/alarm/periodic/status-C, depleted versus restored CMOS |
| P5 keyboard/glue | KBC/keyboard plus `systems/olivetti-pcs286/` board and IOC02 component | P0; P3 mock IRQ | ACK/BAT/inhibit/RESEND/queue overflow, set translation, LEDs, A20/reset, parity/NMI and real IOC02 state; no ROM/BDA patch or first-read trick |
| P6 storage | WD37C65B and ATA contracts/implementations | P0; P3 DMA contract | command phases, absent/write-protected media, DMA/PIO/TC, multi-sector read/write, ATA status/IRQ/CHS/IDENTIFY, persistence and second boot later |
| P7 video/ports | PVGA1A, IMS G171 and reusable UART/SPP | P0; board evidence | single DAC owner, palette readback, fonts/text/graphics/CRCs, timing limits documented; UART/SPP reuse audited |
| P8 machine integration | PCS286 composition, clock wiring; frontend only after gates | P1–P7 | validate config; two isolated sessions; partial cleanup; BIOS 1.42 cold POST/Setup, floppy boot, reset, keyboard and video; then 1.37 and RAM matrix |

P3/P4/P6 share `components/pc/`: assign exact filenames in agent prompts and
keep CMake/provenance/common-header edits coordinator-owned to avoid conflicts.
P5 may similarly need to be split between keyboard and IOC02 agents. Parallel
implementation starts only after P0 establishes the interfaces and fixtures.

Every agent handoff must include this document, the canonical asset/source
IDs, exact base commit, owned paths, forbidden shortcuts, tests to run, and
the required provenance entry. Report implemented, partial, unsupported and
timing-unknown separately. Do not relabel a partial interpreter as complete.
Full components are the unit of work; arbitrary slices of opcodes are not
independently acceptable integration milestones without an explicit coverage
matrix. The 286 is reusable beyond this machine.

Any reused behaviour from the legacy implementation or the separate Circle
pilot must first be audited and retain the original notices. Pin its source
commit/path and classify `selective-port` or `derived-rewrite`. The scaffold
itself introduces declarations only (`new`); this label does not pre-approve
the provenance of later bodies. The Circle pilot's host POST/DOS results do
not validate this portable engine or physical Raspberry Pi execution.

## Completion and later scope

For initial availability: authored component tests, real/protected-mode 286
diagnostics, POST/Setup/video/keyboard, floppy read/write/boot, correct machine
reset, all failure paths and a clearly described timing policy. HDD-backed
commercial variants additionally require partition/format/write/persist/second
boot. Runtime availability comes from an implemented/validated machine
definition, never from the historical catalogue merely containing the product.

Later milestones: physical ISA inventory/cards, optional 80287, verified
runtime 6/12 MHz switching and wait-state policies, >4 MiB only with identified
expansion evidence, full video timing and exact chip-specific serial/parallel
behaviour. Bare-metal hosts can consume the same contracts without Qt, JSON,
host threads or filesystem types. FPGA conversion is not a promise of this C ABI.

## Building this scaffold

Configure engine-only with `BLUMACH_BUILD_LEGACY=OFF`,
`BLUMACH_BUILD_ENGINE=ON`, `BUILD_TESTING=ON` and build normally. The
`blumach_pcs286_contract_check` object target compiles each new public header
individually as C11; when a frontend already enables C++, it also checks C++17.
No CPU/device function bodies are supplied and the check objects are not
linked into products. Interface targets all end in `_contracts`.

Use the existing regression suite and `tools/provenance_audit.py`. Passing
these checks means the scaffold is well formed and existing products remain
testable, not that PCS286 hardware is implemented. Behavioural PCS286 tests
are deliberately the next phase requested by the user.

Initial structural validation on Windows: all 20 included new/reused headers
compiled separately under GNU C11/C++17 and MSVC C11; the existing engine-only
suite passed 73/73. Provenance covered 31 components without errors; the
provenance/catalogue Python suites passed 16/16 and the unchanged catalogue
validated 32 machines in five locales. No ROM was loaded for this scaffold.
