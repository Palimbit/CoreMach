# Headland migration from the working classic implementation

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

Local blocks, 2026-09-25: **private GC103 registers, memory routes and IOC02
classic latches ported; AT memory and I/O adapters implemented**. This is reusable work toward the PCS286 controller,
not a qualified IOC02/Headland board profile or bootable portable machine. The public
`blumach_headland_gc10x` target remains absent and its acceptance test skips.

The [local BIOS probe](pcs286-boot-probe.md) now executes preserved BIOS1.42
through this private composition. After the RTC/empty-output-read corrections
and explicit classic KBC80h/84h/CFh profile, it reaches a depleted-calendar RTC
stop while polling absent video3DAh. This is early
firmware execution, not completed POST, video, Setup or media boot.

## Functional integration priority (2026-09-25 user decision)

Build a usable emulator with explicit, tested implementation choices first;
improve physical fidelity incrementally. Unresolved chip identity, pin precedence
or electrical timing is not by itself a reason to postpone board integration.
Manufacturer evidence still overrides contradictory classic behavior. A physical
detail needs further modeling when it changes behavior visible to software or
explains an observed functional failure.

The adopted baseline for the next composition is the existing corrected classic
profile, already exercised by the private board tests. This decision changes the
work order, not runtime defaults or validation results:

| Area | Adopted functional solution | Later fidelity work |
|---|---|---|
| RAM/EMS/shadow | Existing 1/2/3/4MiB legacy initializer and LEGACY_GC103 AT adapter, including the documented context and shadow-source corrections. | Actual chip population, straps and additional geometry. The configured experimental profile is optional. |
| Registers/reset | Retain the existing classic width, unused-bit and initialization policies where documentation is inconclusive; document them as provisional. | Replace a policy when evidence or a reproducible software failure justifies it. |
| IOC02 | Reuse the three latches and existing board signal owners; implement missing software-visible control as needed during assembly. | Exact output wiring and hardware reset/readback. No forced first read or permanent BIOS alias. |
| Timing | Existing functional CPU boundaries and peripheral clocks, with explicitly provisional service costs where supported. | Measured waits and strict CPU timing; never label estimates as measured cycles. |
| Storage/errors | Existing bounded backing, explicit hole/write policies and unchanged host error propagation. | These correctness requirements remain mandatory. |

The next deliverable is a minimal executable machine using the existing board
owners, RAM and external firmware inputs. Integrate missing DMA service and
device paths as needed to reach the boot milestones below. Keep focused tests for
observable transfers, interrupts, reset and failure handling; add exhaustive
matrices only when a specific risk warrants them. Completion is working
composition with recorded approximations, not exhaustive silicon certification.
The public Headland acceptance test remains pending because the public component
integration is unfinished; its current skip is not a demand for perfect hardware
fidelity. No boot or public implementation result is claimed by this decision.

### Near-term real-firmware milestone

1. Assemble a local functional runner with reset, memory/ROM and the existing
   AT devices. Load a scanned, preserved BIOS from outside the checkout. Trace
   the reset vector, POST writes, relevant I/O and the first stopped or failed
   operation. Attempt this before completing every device or physical detail;
   a reproducible missing-device stop is useful evidence, not a boot success.
2. Use that trace to implement missing software-visible behavior and advance
   POST/Setup. Reuse existing classic devices where applicable; keep adopted
   policies explicit. Add focused regression tests for actual failures.
3. Connect the floppy controller/DMA/IRQ path and/or disk controller, then boot
   a scanned preserved DOS image with an external writable working copy or
   overlay. Preserve the originals and record the exact BIOS, RAM, devices,
   media hashes, runtime policy and observed endpoint of each attempt.

The canonical BIOS1.42 and DOS3.30a records are candidate inputs; their current
paths, hashes and clean status must be checked before execution. Neither a
classic/Circle boot nor a synthetic test is a portable-machine boot result.
No BIOS first-read override, permanent diagnostic alias or fabricated pass is
part of this milestone. Missing wiring/timing is investigated when it affects
the trace; complete electrical fidelity is not an entry requirement.

## Sources and reuse boundary

Start from BluMach's functional classic implementation, pinned to
`4769e40524bc194747b142f3e7ec908ae4df0897`:

| Classic source | Reuse decision |
|---|---|
| `src/chipset/headland.c` | Actual GC103 register, RAM/EMS translation and mapping behavior migrated privately; retain Sarah Walker, Fred N. van Kempen, GreatPsycho and Miran Grca notices/GPL. |
| `src/machine/m_at_olivetti_286.c` | Establishes that classic PCS286 selects `headland_gc10x_device`, whose variant is GC103 with no control-register index. Its PCS286 initialization selects raw CR0=0. This is implementation evidence, not package identification. |
| `src/chipset/olivetti_ioc02.c` | Separate EngiNerd/rtzor-derived controller, ports 68/6A/6C. Selector/latch/steady-state readback ported privately; inherited reset/readback policy is separated from unconfirmed PCS286 output wiring. Its first-read override is an explicit POST shortcut and is excluded. |

No classic source is edited. No port of later HT18/HT21 registers or 386 banks
is silently substituted for this variant. The classic permanent
60000h-to-80000h alias is board-level extra code, not part of the Headland
register engine or a documented switching mechanism.

Canonical development references remain outside Git:

- `headland-ht101a-ht102`, SHA-256
  `7ba3b16fed836311097cfa608306823e39b1b5f9646536f54437a44a8c94ac26`;
- `headland-ht101a`, SHA-256
  `c3e9f6e5d92ffe797df4ff74fe773ee167faa68b63cbca62b92f202415c0f1d0`.

Both preserved PDFs contain 55 pages and begin with G-2 GC101/GC102 material.
The second has an OCR layer, useful for navigation; exact tables require the
scan. The first scan's PDF page 21, *GC101/102 Configuration Options*, was
visually reviewed. Pages 9/21 describe RSEL2 for DRAM type and RSEL1:0 for
address ranges; page 21 also describes HISPEED/IOHALFSP. These document the
reference design, not the exact Olivetti strap wiring or a GC103 equivalence.
No absence of registers is inferred from an OCR search. Full schematic review
and PCS286 board applicability remain pending.

## Inherited register truth table

Every behavior in this table is **observed in classic source**, not independently
verified on physical silicon. Private implementation:
`components/chipsets/headland/src/legacy_gc103_registers.{c,h}`.

| Port/access | Read | Write/effect |
|---|---|---|
| 1EC, byte | Low byte of EMS[MAR & 3F] | Store FF00 OR byte; report that EMS slot for rebuild |
| 1EC, word | EMS[MAR & 3F] OR FC00 | Store full word; report that EMS slot for rebuild |
| 1ED, byte | FF (no CR index in this variant) | Ignored |
| 1EE, byte | Full MAR | Replace MAR |
| 1EF, byte | CR0 low five bits OR RAM straps | Store that same combination; report full mapping rebuild |
| Word at 1ED/1EE/1EF | FFFF | Ignored by the classic word handler |

Each successful guest data-port access advances the **full eight-bit MAR**
when bit 7 was set. Bit 6 does not select an EMS register; bits 5:0 do. FF wraps
to 00, clearing auto-increment. The write effect reports the slot selected
before increment. No callback or memory map is invoked by this private layer.
Native byte/word handler equivalence does not define how the eventual board
splits an odd word across ports; the adapter must specify that separately.

Initialization accepts exactly 1/2/3/4 MiB and uses classic CR0 strap values
20/60/40/A0 respectively (entries 2/4/6/8 of `mem_conf_cr0`). EMS, MAR and raw
CR0 start at zero. Classic readback already includes straps, but **raw CR0 does
not include them until the first write**. This distinction is retained for the
future address translator. It is a classic initialization policy; no documented
warm-reset retention or hardware strap sensing is claimed.

Private storage is caller-owned, isolated per instance and has no allocation,
RAM, ROM, host I/O or timing dependency. Reinitialization explicitly reapplies
that initialization policy. Invalid arguments and unowned ports preserve all
state/output values; unowned ports return UNMAPPED. Unsupported native widths
and FETCH are invalid at this layer. DEBUG reads are observational, including
MAR, and DEBUG writes return READ_ONLY. These are portable contract rules, not
additional guest registers. There is no fabricated zero-wait timing result.

## Validation and scope

`pcs286-component.headland-registers` compiles the **actual classic tables and
handlers**, extracted unchanged at configure time into the build directory.
The fixture replaces only logging and memory-map publication with observers.
It pins the normalized source SHA-256
`7a608fd0d3e58b29e738dbf502eadc556fb99f5167d683132cf78c3d10b68a13`;
source changes require reviewing the reference before updating the pin.
Classic-generated code is linked only into the test, never the portable core.

The suite checks 2,727,967 state comparisons, including all byte values for all
256 MAR values at each RAM size; all 65,536 word values at each RAM size; all
CR0 writes and ignored selector values; continuous MAR wrap; pure debug reads;
100,000 mixed accesses across two instances with reinitialization; mapping
notification equivalence; and the complete 16-bit port rejection sweep.
Bad arguments, DEBUG writes and invalid initialization preserve state/outputs.
These tests establish migration equivalence and API behavior, not hardware
fidelity, memory routing, EMS transfers or successful POST.

First-block GCC 16.2 UCRT64 Debug/Release: 132 pass and one public Headland skip
in each configuration (133 registered). Python tools: 50 pass. Provenance:
43 components / 240 files, no errors. Catalogue: 32 machines / 5 locales.
No new MSVC, GUI, remote CI or hardware-validation result is claimed.

## Private memory routes (second block)

`legacy_gc103_memory.{c,h}` adapts `get_addr`, `hl_ems_update`,
`memmap_state_update` and initialization. The instance owns the register bank,
93 window descriptors and access-selection metadata; it owns no RAM or ROM.
Register writes and their mapping effects are applied together. Queries are
pure and never advance time, read memory or synthesize an ignored write.

| Condition | Current private route (inherited except documented corrections below) |
|---|---|
| Ordinary low RAM | 00000–9FFFF to equal backing offsets, except an enabled EMS window |
| CR0 bit 2 clear | 100000 through RAM-size+5FFFF to backing A0000 through RAM-size−1 (384 KiB relocation) |
| CR0 bit 2 set | 100000 through RAM-size−1 to equal backing offsets; no relocated tail |
| System ROM | E0000–FFFFF and FE0000–FFFFFF, offset into the classic 128 KiB firmware layout |
| CR0 bits 4:3 with bit 2 set | 08 selects E0000/FE0000 shadow from backing F0000; 10 selects F0000/FF0000 from backing E0000; 18 both. RAM reads/FETCH, writes disabled. With bit 2 clear, shadow selection is suppressed. Source selection follows the GC103 p6 pointer table, correcting the classic equal-address mapping. |
| EMS enabled | CR0 bit 1 plus MR bit 9; CR0 bit 0 chooses one of two 32-register banks |
| EMS windows | 24×16 KiB at 40000–9FFFF and 8×16 KiB at C0000–DFFFF |
| EMS backing | MR page bits 4:0 plus bank bits 8:7; CR0 bit 7 selects 512 KiB versus 2 MiB banks, the latter also uses MR bits 6:5 |
| EMS beyond installed backing | OPEN_BUS metadata, no out-of-bounds pointer/access or fabricated guest exception |

In the private API, a route marked RAM or FIRMWARE and `writable=0` describes
write protection; the future board adapter decides the guest write outcome.
EXTERNAL returns the physical address for downstream decode; it does not claim
that a card exists. `contiguous_bytes` is conservatively capped at the next
16 KiB boundary and installed backing limit. Timing stays UNKNOWN: a numeric
zero in `extra_memory_clocks` is not known zero-wait operation.

The GC102 data-buffer pin description, PDF page 13, was visually checked in
the more legible `headland-ht101a` copy (not just its OCR). With CPUHLDA low,
A20G conditions CPUA20; with CPUHLDA high the output is three-stated. The
private route API therefore clears **only bit 20**, only for CPU requests when
its explicit A20 input is low. DMA/ISA addresses are not CPU-gated. The AT
adapter still owns grant checks; reset and the source of the Olivetti A20G
input are not fabricated by this helper.

Page 21 of that same copy was also visually checked. Its documented G-2 RSEL
choices include these maps (inclusive byte addresses translated from KiB):

| Reference RSEL2:0 | Installed RAM | Reference visible RAM |
|---|---|---|
| 011 | 1 MiB, 256-Kbit DRAM | 00000–9FFFF and 100000–15FFFF |
| 110 | 2 MiB, 1-Mbit DRAM | 00000–9FFFF and 100000–25FFFF |
| 111 | 4 MiB, 1-Mbit DRAM | 00000–9FFFF and 100000–45FFFF |

These agree with the corresponding relocated *ordinary* ranges. They do not
document the inherited GC103 programming interface, a 3 MiB RSEL setting or
which RSEL signals the Olivetti board drives. The 3 MiB case remains inherited
model coverage, not a newly established physical configuration.

### Incremental behavior: documented correction after migration

Writing an **inactive-bank** EMS register first executes the classic disable
path. For C0000–DFFFF that path changes access selection to external even if
the corresponding active-bank mapping remains enabled. Rewriting CR0 rebuilds
both banks (inactive first, active last), exposing the active EMS mapping
again. At 40000–9FFFF the inactive write re-enables ordinary RAM, but the later
registered active EMS handler still takes precedence. A stateless calculation
from final register values would silently change these sequences.

The initial port retained that artifact solely for migration comparison.
The GC103 manufacturer reference acquired on 2026-09-25 now contradicts it:
MAR.D5 selects the register context for I/O, while CR.D0 selects the context
for memory cycles. The private port now retires an inactive window without
changing the live memory decode. Classic fast-path cache machinery and the
permanent PCS286 diagnostic alias are not imported. The raw classic oracle
still reproduces the old artifact; it has not been patched to conceal it.

The new test extracts the actual classic mapping functions as well as the
register handlers. A recording adapter models `set_addr` enabling its window
and the mapping registration order checked in `src/mem/mem.c`; this is a
component differential oracle, not execution of the whole classic emulator.
It checks 7,754,766 routes over all low-five-bit CR0 combinations, 1/2/3/4 MiB,
all 65,536 EMS words in each bank, boundaries across the 24-bit space, sequential
map reconfiguration, inactive-bank effects, CPU/DMA/ISA A20, and READ/WRITE/FETCH.
DEBUG/error purity and separate instances are checked. Real portable backing
storage with authored bytes checks relocated RAM, EMS sharing, high ROM FETCH,
shadow protection and RAM retention through chipset reinitialization.

### GC103 context evidence and regression (2026-09-25)

Primary reference: Headland Technology, *GC103 G2 Product Line*, preliminary
07-89 (01), printed/PDF pp2, 3, 5 and 7, visually checked from the
[manufacturer scan](https://datasheet.datasheetarchive.com/originals/scans/Scans-004/Scans-0081535.pdf).
Canonical asset `headland-gc103-g2-1989-07`, SHA-256
`09435ed60273b35f0687e8b1e61622bcfeb16eb9ef71effc02b6c10e3e03f961`,
26 pages, remains local-only outside this worktree. GC103 is described as a
companion to GC101/102, replacing the address-buffer role of GC102. This is
component evidence, not proof of the exact PCS286 board population or wiring.

**Audit recommendation: correct incrementally.** Context-selection fidelity
is now supported by manufacturer documentation; validation confidence is high
for the tested functional rule, without physical-board measurements. Evidence
coverage is strong for this rule and incomplete for the full PCS286 chipset.
No whole-machine fidelity grade or public-chip acceptance is raised.

`pcs286-component.headland-context` uses an authored register-level oracle,
independent of classic mapping functions. It checks both contexts, all 32
windows, all 1,024 MR bit patterns, enabled/disabled active pages, context
switches, three offsets per window, CPU/DMA/ISA and READ/WRITE/FETCH. It also
covers automatic traversal across contexts and carry/wrap, observational DEBUG
reads, normal reads without remapping, invalid writes, legacy byte writes and
global EMS disable/re-enable. Its 1/2/3/4 MiB configurations remain inherited
fixtures, not confirmation of the physical strap population.

The existing 7,754,766-route suite keeps every sequence and the unchanged
classic functions. A separate documented EMS oracle accounts for 205 expected
route divergences (inactive upper-window writes); all other reference routes
must still agree. The new suite adds 21,679,542 route checks. Both tests failed
against the previous private implementation, then passed after the correction.
Real backing tests additionally verify that writes before and after switching
contexts reach distinct retained RAM bytes. Timing remains UNKNOWN.

Remaining qualification: byte access to the 10-bit MR and unused readback bits;
CR software overrides versus actual RAMIM/RAMSW/SPLSW straps; reset/retention;
remaining shadow/write-control details; precise board population and IOC02 behavior. The
manual's MAR examples contain apparent numeric inconsistencies (e.g. 64
increments from 80h described as A0h); retain the explicit 8-bit-counter rule
and flag the examples rather than inventing a special counter. No other
register behavior is newly certified by this correction.

Local GCC 16.2 UCRT64 Debug/Release each pass 150 tests with the public
Headland acceptance still skipped (151 registered, assertions/Werror enabled).
Python tools: 50 pass; provenance: 58 components/297 files, zero errors;
catalogue: 32 machines/five locales. No firmware/media, GUI, MSVC, physical
hardware or remote CI was exercised in this block. Work remains local.

### GC103 shadow source selection (2026-09-25 follow-up)

Printed/PDF page 6 of the same manufacturer reference was visually checked.
Its literal shadow-pointer table supplies these MR values for preparing RAM
before enabling the corresponding shadow region:

| CR enable | Visible read-only windows | MR pointers, 256-Kbit DRAM | MR pointers, 1-Mbit DRAM | RAM backing from p5 address fields |
|---|---|---|---|---|
| D4 | F0000–FFFFF and FF0000–FFFFFF | 0298–029B | 0238–023B | E0000–EFFFF |
| D3 | E0000–EFFFF and FE0000–FEFFFF | 029C–029F | 023C–023F | F0000–FFFFF |

**Documented:** the enable bits, pointer table, paired low/high windows and
read-only shadow. **Derived from the documented MR fields:** the backing
addresses above. **Observed:** classic `get_addr` instead uses equal low
addresses for both halves. The private port now uses the table-derived sources.
These are fixed sources; changing MAR or the EMS map after filling RAM does
not relocate a shadow window. Enabling shadow performs no ROM-to-RAM copy.

`pcs286-component.headland-shadow` checks 184,320 routes over every low-five-bit
CR combination, inherited 1/2/3/4 MiB fixtures, CPU/DMA/ISA, READ/WRITE/FETCH,
A20, both aliases and five offsets per 16 KiB page. A second test fills all eight
shadow pages through the literal MR values above, then checks every byte in
each enabled/disabled low/high window. Writes to these windows are rejected by
the storage fixture without altering their contents. Reinitialization restores
ROM visibility and preserves caller-owned RAM, as required by the private API.
There are 17,825,864 completed backing transfers; rejected writes are not counted
as successful transfers. Both the new regression and the existing route suite
failed against the previous implementation, then passed with the correction.

The existing 7,754,766-route suite remains intact. Its raw classic oracle is
unchanged; independent documented expectations now identify 26,112 shadow-source
divergences in addition to the 205 EMS-context divergences. The synthetic backing
test now prepares F0000 RAM for E/FE shadow, replacing its old equal-address
assumption. Authors, licences and pinned source comparison remain preserved.

Scope remains component-level: this preliminary document does not identify the
exact Olivetti board. Its ambiguous sentence about disabling RAM chip select
does not justify inventing extra behavior beyond the explicit readable-shadow
and pointer definitions. Write protection through an EMS alias of the same
physical RAM, reset retention on hardware, board straps and CR override behavior
remain unqualified; this patch does not alter them. The private CR2 dependency
is retained, consistent with p7 reserving the displaced 384 KiB for shadow/EMS.
Timing remains UNKNOWN and public Headland acceptance remains skipped.

Follow-up validation: GCC 16.2 UCRT64 Debug/Release each pass 151 tests plus
one explicit public Headland skip (152 registered). Python: 50 pass; provenance:
58 components/298 files, zero errors; catalogue: 32 machines/five locales.
No new hardware, firmware, GUI, MSVC or remote CI result is claimed.

The first backing test used EMS word 0300h, which correctly mapped beyond its
1 MiB allocation. The fixture was corrected to 0288h (bank 1/page 8 => A0000);
the production mapping and rejection assertions were not relaxed.

After the memory block, GCC 16.2 UCRT64 Debug/Release each pass 133 tests with
one public Headland skip (134 registered). The 50 Python tool tests, provenance
audit (44 components/243 files, zero errors), catalogue (32 machines/5 locales)
and diff check pass. No new MSVC, GUI, physical capture, BIOS or CI result.

### Explicit GC103 control-register straps (2026-09-25 follow-up)

The private register component now also offers
`bm_gc103_registers_initialize_strapped`. It accepts explicit FLOATING/LOW/HIGH
levels for RAM1M, RAMSW1, RAMSW2 and SPLSW; it does not infer board wiring from
installed bytes. This opt-in **register-readback profile** retains the written
CR byte separately from the value observed at byte port 1EFh:

| CR bit | Floating input | Input LOW | Input HIGH |
|---|---|---|---|
| D7, RAM1M | Written D7 | Read 1 | Read 0 |
| D6, RAMSW2 | Written D6 | Read 1 | Written D6 |
| D5, RAMSW1 | Written D5 | Read 1 | Written D5 |
| D2, SPLSW | Written D2 | Read 1 | Written D2 |
| D4/D3/D1/D0 | Written bit | Written bit | Written bit |

Evidence: manufacturer register definitions pp5–7, with pin identities checked
visually on pp12–13. D7 selects the inverse connected level; the other three
bits explicitly OR the software bit with the inverse input for readback. The
CR software latch initializes to zero even when a strap makes a read bit one.
The p12 summary repeats a bank-count row and refers to 1ECh rather than the
control register; the detailed p6 tables and bit descriptions take precedence.

The existing initializer remains the explicit legacy population profile and
passes its unchanged classic differential suite. Both profiles reuse the same
MAR/MR and I/O handlers. Non-CR widths, unused MR bits and MR/MAR initialization
remain inherited policies. Initialization copies pins by value; no live pin
changes or physical warm-reset retention are promised. Failed initialization
preserves state. Normal and DEBUG CR reads are pure; accepted CR writes retain
the exact software byte and report a full-map change even if readback is equal.

Readback alone is **not a memory-decoder qualification**. The fixed-population
private mapper accepts only the legacy profile and returns UNSUPPORTED for
I/O or resolution if configured registers are substituted, preserving state
and outputs. The configured geometry initializer below now supplies a separate
bounded memory profile. Tied-input decode precedence and the real board's straps
remain pending. In particular, a readback OR rule does not by itself prove how a
hard-tied SPLSW input interacts with software for memory decoding.

`pcs286-component.headland-control` uses independent truth tables for 62,373
CR reads, all 81 pin combinations and all 256 software values. Every value is
written after both FFh and 00h to catch accumulated/sticky writes. Tests cover
zero initialization versus strapped readback, copied configuration, aliased
reinitialization, unaffected EMS/MAR, pure DEBUG reads, invalid pins/arguments,
unchanged error outputs, profile/instance isolation and explicit mapper refusal.
The 2,727,967 classic register comparisons and previous context/shadow suites
remain unchanged and pass. No physical PCS286 strap population is asserted.

Final GCC 16.2 UCRT64 Debug/Release: 152 passes and one public Headland skip
(153 registered). Python: 50 pass; provenance: 58 components/299 files;
catalogue: 32 machines/five locales; all checks pass. The first Release run
exposed a pre-existing test bug: `board_control_test.c` passed `&cpu` to an
engine callback requiring `cpu.context`. That call had undefined behavior;
the earlier board-control strict-clock assertion was not valid evidence.
The test now uses the correct context and also checks zero cycles/no accesses
and rejection of a second attempt until reset. Both complete suites were rerun
successfully. CPU implementation and its already valid acceptance tests remain
unchanged; no timing gate was relaxed.

### Configured GC103 geometry (2026-09-25 follow-up)

`bm_gc103_memory_initialize_configured` connects the existing configured CR
readback and corrected classic address translation to explicit physical DRAM
density and installed bank count. It allocates no storage. The legacy initializer
and its differential oracle remain intact. The AT adapter requires an explicit
`CONFIGURED_GC103` selection matching the borrowed model; substituting configured
registers into a legacy mapper still fails without effects.

Manufacturer pp2/5-7 document bank count, density, MR address fields and floating
software control. This bounded profile accepts uniform 256K DRAM (512KiB per
16-bit bank) or 1M DRAM (2MiB per bank), with one to four contiguous installed
banks. RAMSW1/2 and SPLSW must float. RAM1M can float or be connected consistently
with the physical density. Inconsistent density wiring and unresolved tied
RAMSW/SPLSW decode return UNSUPPORTED before changing the model. The component's
8MiB capability is not a claim of 8MiB onboard PCS286 RAM; its board contract
continues to cap onboard backing at 4MiB.

CR.D7 selects geometry; D6:5 select the ordinary linear extent, independently of
installed backing. CR2 relocation and CR3/4 shadow source selection reuse the
qualified private behavior above. Decode is computed from current registers,
so growth/shrink and context changes cannot retain stale windows. The existing
MR bank/page translation can reach an installed bank beyond the selected linear
extent: this is a retained **inference** from the separate linear decoder and
MR bank fields on pp2/5, not a physical-board observation. Missing installed
banks yield OPEN_BUS route metadata, never a host pointer or successful read.
The existing AT adapter applies the caller's explicit reject/FF policy.

Selected/installed density mismatch refuses RAM, EMS and shadow routes with
unchanged output; ROM and external routes remain accessible so control I/O can
restore valid geometry. The manual's fixed 0-640KiB linear description does not
settle the selected 512KiB case: ordinary low-memory decode is UNSUPPORTED there,
while explicit EMS and ROM remain usable. No DRAM alias behavior is guessed.
MR reset/non-native widths and write protection through EMS aliases remain
inherited or unresolved. The 128KiB firmware layout remains a composition policy.

`pcs286-component.headland-geometry` checks 15,741,444 routes using independent
population tables and expected physical offsets: both densities, 1-4 installed
banks, every CR value, all 16KiB region endpoints throughout 24-bit space,
CPU/DMA/ISA and CPU-only A20, READ/WRITE/FETCH, both contexts, all MR bank/page
fields, inactive writes, EMS disable, and repeated geometry growth/shrink.
It also checks invalid/fixed pins, unchanged error outputs, pure DEBUG,
reinitialization and instance isolation. AT integration uses bounded authored
storage through the actual adapter: populated/hole crossing preflight,
read/write/FETCH endpoint failures before/after every fragment, retained effects,
no retry/error substitution, A20, context switching, shadow source/protection,
density recovery, 8MiB extent and EMS beyond the selected linear top. Strict
timing still refuses access; DEBUG remains untimed and observational.

Local GCC 16.2 UCRT64 Debug/Release validation: 153 pass plus the intentional
public Headland skip (154 registered); Python tools 50; provenance 58 components/
300 files; catalogue 32 machines/five locales. The new test initially lacked
an AT contract include dependency; its CMake link dependencies were corrected.
No public Headland acceptance, physical timing, actual board profile, machine
factory, firmware execution, MSVC/GUI/ASan/CI result or publication is claimed.

## Private IOC02 classic latches (third block)

`components/chipsets/olivetti-ioc02/src/legacy_ioc02_registers.{c,h}` ports the
classic latch behavior into caller-owned, independent state. It has no global
machine handle, allocation, callback, CPU reference or first-read flag. The
private target is `blumach_ioc02_legacy_registers`; the public IOC02 contract
remains a draft without an implementation.

| Access | Inherited policy, not a documented hardware claim |
|---|---|
| Initialize/reinitialize | Select=04h, data=04h, control=FFh |
| 68h byte | Read/write all selector bits |
| 6Ah byte write | Store only if selector bits 4:0 are not all zero; other selector bits do not enable the latch |
| 6Ah byte read | Stored data XOR 20h, on **every** read, including the first after initialization |
| 6Ch byte | Read/write all control bits |

There is a single data latch, not a separate bank for each selector. Reads do
not modify state. DEBUG reads use the same ordinary readback and DEBUG writes
return READ_ONLY. Only the three byte ports are owned; gaps are UNMAPPED and
native words/FETCH are rejected. The future adapter must specify split I/O,
open-bus and timing policy. Failed calls preserve state and output arguments.

Successful accesses report raw latch `written` and `changed` masks. A write
of the same value reports a latch write without a transition; a disabled 6Ah
write reports neither. These are observations for a future owner, **not
decoded physical output signals**. No bit is connected to Headland, A20, reset,
shadow RAM or an alias without supporting evidence.

The classic comments attribute the 6Ah inversion and selector gating to
PCS386SX Phoenix 1.14 reverse engineering. They do not establish those rules
for physical PCS286 hardware. Reset values are also inherited model policy.
The classic PCS286 first-read 04h override deliberately bypasses a POST test;
it is excluded, so the private model reads **24h** after initialization. Merely
clearing the classic `first_read_hack_enabled` flag would still force 04h in
read-first sequences, which the new regression test explicitly demonstrates.
This scope does not promise that existing firmware will pass that test.

For additional context, the preserved Dario technical manual
`docs-manufacturer-ta-dario-286-technische-unterweisung-ba025-1990-06-pdf`
(related record `triumph-adler/dario286`, SHA-256
`16cc61f91331f777352adaed635d681e57d6e9eeb43bb2d29496fbfeb9674c85`)
was read in place. PDF page 11 / printed page 9 was visually checked: its
CSAA03/06/09 placement drawing labels GC102, GC101 and IOC. This is placement
evidence for those Dario boards, not an IOC02 programming specification or
proof of every PCS286 board revision. That page supplies no register-to-pin
wiring. No absence of documentation is inferred from an OCR search. The more
legible Headland reference and its applicability limits above remain in force.
Neither document was copied into Git or changed.

`pcs286-component.ioc02-registers` compiles the actual classic state, handlers
and reset code unchanged from a generated test-only include. Its normalized
source SHA-256 is pinned to
`32c5a3e7bd04d670d3f1ca3aa01c7fdaf76cf9e2a0961e8776d9a6affa9a4506`.
The differential fixture explicitly primes the classic `first_read_done` flag
to compare only steady-state logic; separate assertions verify the intentional
first-read difference. Logging is suppressed and machine identities are
test-local inputs to the original reset function. This does not execute the
whole classic emulator or any firmware.

Coverage: 1,810,131 classic latch comparisons; all 256 selectors with all 256
data values; every old/new data pair for all eight disabled selectors;
same-value writes; control readback; pure DEBUG; 100,000 mixed accesses with
two instances and reinitialization; all 65,536 ports; invalid arguments,
widths and operations with preserved outputs. A real portable bus-router
fixture checks sparse byte mappings, write effects, gaps, rejected wide
access and debug inspection. Its adapter is authored test infrastructure,
not production AT wiring, timing or firmware validation.

After this block, GCC 16.2 UCRT64 Debug/Release each pass 134 tests with one
public Headland skip (135 registered). Python tools: 50 pass. Provenance:
45 components / 247 files, zero errors. Catalogue: 32 machines / 5 locales;
diff check passes. No new MSVC, GUI, ASan, remote CI or hardware result.

## Private AT memory composition (fourth block)

`systems/olivetti-pcs286/src/headland_at_memory.{c,h}` implements
`blumach_pcs286_headland_at`. It composes the already ported GC103 routes with
the real PCS286 backing store and existing exact engine clock arithmetic.
It copies no classic algorithms and does not modify the CPU, AT bus, DMA,
IOC02 or GC103 route engines. The caller-owned adapter copies its configuration,
borrows its route/endpoint contexts and allocates or publishes nothing.
Its ready-made backing endpoint delegates to `bm_pcs286_memory_access`.

The explicit `BM_HEADLAND_AT_LEGACY_GC103` profile acknowledges the inherited
model's remaining limitations. It uses the corrected context selection described
above; this does not activate or fully qualify the public Headland profile.
The board supplies A20 explicitly and invokes this decoder **after** the AT
interconnect checks ownership. The adapter does not duplicate arbitration.

| Boundary | Implemented composition policy |
|---|---|
| Logical transfer | MEMORY/PROGRAM/DATA, READ/WRITE/FETCH, 1–8 bytes, either byte order; reject any part beyond FFFFFFh before endpoint effects |
| Physical fragments | Internal RAM/ROM use aligned 16-bit lanes; external target width is explicitly 8 or 16 bits. Split on lane, width and route boundaries. This is a functional adapter policy, not a measured waveform. |
| Routing | Snapshot the complete route plan and A20 input before callbacks; CPU-only bit-20 conditioning, DMA/ISA addresses unchanged |
| Unpopulated space | Explicit REJECT or FF-read/ignored-write policy for OPEN_BUS routes and an absent external endpoint only |
| Installed endpoint failure | Propagate UNMAPPED, READ_ONLY and all other errors unchanged; do not reinterpret a missing implementation or backing-size mismatch as an empty socket |
| Protected ROM/shadow writes | Explicit REJECT or IGNORE; ignored writes never touch the backing endpoint |
| Preflight rejection | Strict unknown timing, disallowed holes/protection and configured-cost overflow reject the whole logical transfer before any endpoint |
| Runtime failure | Stop at that endpoint, retain earlier effects and any effects of the failed callback; no rollback or automatic retry. Preserve the caller's transaction/read value. Report completed and attempted bytes as host diagnostics. |
| DEBUG | Read-only, no clock conversion, no route/register or persistent progress mutation; endpoint must itself be observational |

All endpoint calls are synchronous and non-reentrant. Access and A20 setters
reject callback reentry. Borrowed objects must stay alive and route/configuration
mutation or reinitialization must wait until the logical transfer completes.
Reinitializing the adapter or route model does not clear backing RAM.

### Explicit timing policy

STRICT rejects the inherited UNKNOWN routes before accessing memory. The named
`PROVISIONAL_SERVICE_CLOCK` policy instead requires caller-supplied service
clock and per-target extra clock costs. Endpoints return additional waits in
that **same service-clock domain**, with zero on entry. The adapter sums those
durations and uses existing `bm_clock_position_*` arithmetic to round upward
once to requester clocks. The AT interconnect forwards the result without a
second conversion. No rounding credit carries into another logical transfer.

The numerical parameters used in tests are authored fixture inputs, not a
Headland/PCS286 wait-state table. Costs are extra waits, not complete electrical
bus-cycle durations. No unknown route or CPU instruction becomes documented
timing; the CPU strict-clock gate remains closed. Clock representation or result
overflow returns CAPACITY_EXCEEDED. Overflow introduced by an endpoint's dynamic
waits can follow completed memory effects, which remain visible; a failed
endpoint's status takes precedence over its returned wait/value garbage.

### Composition validation

`pcs286-component.headland-at-memory` checks:

- 90,112 byte-oracle read/FETCH cases over 1/2/3/4 MiB, eight CR0 settings,
  CPU/DMA8/DMA16/ISA, A20 levels, byte orders, sizes and decode boundaries;
- 12,288 writes with direct backing checks, including split EMS pages, A20
  discontinuities, ROM/shadow protection and external space;
- 1,920 injected endpoint failures before/after effects at every fragment of
  internal, mixed internal/external and byte-wide external transfers, across
  eight error statuses, all three operations and both byte orders;
- 5,000 exact rational-wait cases plus distinct target costs, one-rounding and
  no-cross-request-credit checks, 64-bit reduced rates, exact UINT32_MAX and
  configured/dynamic overflow, strict rejection and untimed DEBUG;
- real AT ownership, LOCK/HOLD/HLDA, CPU-only A20, DEBUG under external ownership,
  independent instances, reset retention, external widths, reentry and invalid
  argument checks, and absence of the permanent diagnostic alias.

An authored reset trampoline at FFFFF0h runs on the actual 286 and AT bus with
this adapter. Its program writes relocated RAM, programs the inherited EMS
ports and reads that same backing through an EMS window. The external endpoint
and native I/O wrapper are test fixtures, not a complete board I/O decoder or
firmware. Injecting a reset-fetch host failure stops the CPU without delivering
a guest exception or repeating the access. The initial test incorrectly
expected a second DEVICE_ERROR; existing CPU code/tests specify INVALID_STATE
after the stop latch. Only that expectation was corrected; CPU code and the
no-repeat assertion remain unchanged.

The initial Release run also failed a bytewise structure comparison in the
fixture. A raw snapshot alone did not make that comparison portable. The final
test compares every defined transaction field, excluding unspecified C padding;
no implementation behavior was changed for this failure. Final GCC 16.2 UCRT64
Debug/Release: 135 pass and one public Headland skip in each (136 registered).
Python tools: 50 pass; provenance: 46 components/250 files, zero errors;
catalogue: 32 machines/5 locales; diff check passes. No new MSVC, GUI, ASan,
remote CI, physical measurement or firmware-validation result is claimed.

## Private board I/O composition (fifth block)

`systems/olivetti-pcs286/src/board_io.{c,h}` adds the private functional
`LEGACY_GC103_AT` decoder to `blumach_pcs286_headland_at`. It invokes the existing
Headland, IOC02, AT PIC and AT DMA implementations, without changing their
algorithms or resetting them. All four initialized children are required;
the PIC must already use bases 20h/A0h and DMA memory-to-memory stays disabled.
Configuration and up to 16 external resource descriptions are copied; child
and callback contexts are borrowed. No machine factory or pin wiring is implied.

| Decode | Explicit composition policy |
|---|---|
| Headland | 1ECh–1EFh, native aligned words; a word at 1EEh goes to the inherited word handler (FFFF/ignored write), not MAR/CR0 byte writes. Odd words split into byte lanes. |
| IOC02 | Only byte ports 68h/6Ah/6Ch. Holes are not mirrors. Raw latch effects do not drive A20, reset or remapping. |
| PIC | Byte ports 20h/21h/A0h/A1h. Existing initialization/INTA/error rules apply. |
| DMA | Byte ports 00h–0Fh, even C0h–DEh and pages 87h/83h/81h/82h/8Bh/89h/8Ah. Odd upper ports, refresh 8Fh and spare latches are not invented. |
| External resources | Inclusive exact ranges with explicit byte/word width, callback and extra clocks. Reject overlaps with any built-in port or another range before publication. Split at range boundaries and aligned native lanes. |
| Unclaimed ports | Caller explicitly selects rejection or FF reads/ignored writes. Installed callback errors, including write-only DMA register reads returning UNMAPPED, never fall back to FF. |

The entire 1–8-byte logical I/O transfer is planned before effects; reject
crossing FFFFh without wrapping. Both logical byte orders are supported;
native Headland registers use little-endian lanes. FETCH and DEBUG writes
are unsupported. DEBUG reads are observational and untimed. Ordinary failures
retain preceding and failed-callback effects, preserve the caller transaction,
and stop without retry, rollback or guest-exception conversion. Completed and
attempted byte counts are host diagnostics. Reentry is rejected; borrowed
child/configuration mutation must wait until the whole access completes.

STRICT still rejects unknown timing before effects. Named provisional costs
and endpoint waits share a service clock. Memory and I/O now use the same
private `at_decode_clock` helper, extracted unchanged from the memory adapter:
existing engine arithmetic, sum then ceil once to requester clocks, no carry
between requests. Configured overflow rejects before effects; dynamic overflow
can follow completed effects. These policies are authored functional choices,
not newly documented silicon timing, odd-word waveforms or board decode mirrors.
The more legible Headland reference retains the applicability limits above.

`pcs286-component.board-io` checks all 65,536 ports against real component
inspection, 1,152 external width/endian/boundary cases and 832 before/after
endpoint failures over both byte orders, reads/writes and eight statuses.
Additional checks cover real-device partial effects, EMS route updates, IOC02
first read/gating, PIC initialization/masks, DMA shared flipflop/pages, strict
timing, exact mixed-target waits, overflow, DEBUG, reentry, invalid arguments,
resource conflicts, copied configuration, isolation/reset retention and real
AT LOCK/HOLD/HLDA ownership. The existing CPU reset/EMS/backing program now
uses this production decoder in place of the earlier test-only I/O wrapper.
It still executes authored bytes, not firmware, and retains its host-stop tests.

Initial validation found a missing contract include dependency in the new test
target, then two fixture errors: PIC programming before its required ICWs and
a stale injected error carried into the next case. Dependencies and fixture
setup were corrected; real component rejection rules were preserved. Historical
failed logs are retained. Final GCC 16.2 UCRT64 Debug and Release each pass
136 tests with one public Headland skip (137 registered). Python tools: 50 pass;
provenance: 47 components/255 files, zero errors; catalogue: 32 machines/five
locales; diff check passes. No new MSVC, GUI, ASan, CI, physical measurement
or firmware-validation result is claimed. All work remains uncommitted/local.

## Next blocks

1. Build the minimal functional runner and attempt real-BIOS execution with
   external preserved inputs and traceable results, as specified above.
2. Integrate DMA service, required devices/signals and lifecycle to advance
   POST/Setup and then real-media boot. Use the corrected classic Headland
   memory and explicit AT ownership. Expose/test the public functional
   composition as it becomes ready; memory-to-memory remains outside scope.
   Address IOC02 behavior when it affects execution, recording provisional
   policies where justified.
3. Improve straps, reset/width details, EMS-alias protection and timing as a
   separate fidelity backlog. Promote a question to a blocker only when it
   prevents required software-visible behavior or breaks correctness. Exact
   hardware claims and firmware validation still require their own evidence.

The [8254 component](pcs286-at-timer.md) is now implemented using the existing
PIT core, with an AT decoder/PIC IRQ0 composition test. PIT/RTC clocks, port61/checks/refresh and the keyboard pair are now implemented
in their bounded contracts. The [board control owner](pcs286-board-control.md)
connects A20/reset/IRQ with the CPU and memory. The private
[board services owner](pcs286-board-services.md) now coordinates peripherals;
full machine lifecycle and remaining devices are pending. These steps do not
qualify the inherited Headland profile.

The DMA prerequisite remains closed with its documented exclusions. Keep
memory-to-memory disabled for Headland until its own wiring supports a profile.
CPU functional protected execution and the strict-clock gate are unchanged.
No commit, publication, new asset, firmware execution or restricted-document
copy accompanies this block.
