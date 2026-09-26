<!-- SPDX-License-Identifier: GPL-2.0-or-later -->
# AT RTC/CMOS functional core

`blumach_at_rtc` implements the reusable register/calendar core and the AT
index/data ports, including the real NMI-mask output. Its
[engine clock attachment](pcs286-at-rtc-clock.md) is now implemented; a complete
PCS286 factory remains pending. No portable boot, exact
package identification or cycle-exact board timing is claimed.

## Evidence and reuse

Manufacturer reference: Motorola **MC146818A, ADI1026R3**, printed pp.10–16,
tables5/6 and figure15, visually inspected from the
[manufacturer scan](https://www.ardent-tool.com/datasheets/Motorola_MC146818A_alt.pdf).
The local-only original is in the canonical machine library, asset
`motorola-mc146818a-datasheet`, SHA-256
`2bc7ae3d16d6a2d234481eff4fafb04955bdf88075bc626887121293400d1f1d`.
IBM5170 Technical Reference6280070 (1985), pp.1-56–1-59, supplies the AT register
context; its port70 bit7 mask wiring was reviewed for the checks/NMI block.
These establish generic functional behavior, not the exact RTC fitted to PCS286.

The implementation derives format conversion, alarm comparison, rate selection
and IRQ logic from `src/nvr_at.c`, and calendar carry from `src/nvr.c`, at
4769e40524bc194747b142f3e7ec908ae4df0897. The original GPL and BSD notices and
authors are retained, including David Hrdlička for the generic NVR source.
The classic files are unchanged. Manufacturer evidence takes precedence over
their mixed Motorola/Dallas/vendor assumptions:

- Remove PM before decoding BCD; preserve AM/PM across noon and midnight.
- Store mode changes without automatically converting calendar bytes. Software
  must reinitialize their representation, as specified by Motorola.
- PF/AF/UF latch independently of PIE/AIE/UIE. Enabling an already flagged
  source asserts IRQ immediately; disabling it does not clear its flag.
- All four C flags clear on a guest read; writes to C/D are ignored. Motorola
  VRT returns prior validity and sets after reading D with operating PS high.
  A permanently forced valid battery, as in the classic generic path, is absent.
- No host wall-clock access, global calendar, inferred century, checksum repair,
  vendor locks or BIOS-specific CMOS values.

## Bounded functional profile

The caller explicitly selects **64 bytes** (the Motorola size) or the
**128-byte extension** in the draft AT/PCS286 contract. The latter applies the
same control semantics to 114 RAM bytes; it is not a claim that Motorola had
128 bytes or that the exact PCS286 part has been identified. Bit7 of the index
write drives semantic NMI mask1; the remaining implemented address bits select
CMOS. Data accesses do not clear the mask. Guest index reads are UNMAPPED for
the board's hole policy; DEBUG can inspect the effective index and mask.

Each instance holds raw CMOS, native-cycle count, divider phase and update
state. The initial image is copied; config controls initial VRT. Control C is
cleared and reset-cleared B enables are removed at construction. With no image,
CMOS is zero, VRT0, SET1 and divider-reset1: an explicit depleted-state policy,
not a fabricated date or measured power-on state. No callbacks occur at create.
IRQ starts low and mask high; the owner supplies matching recipients or resets
the model to publish them. The running PS input is assumed high; battery voltage,
retention decay and power switching are outside this profile.

Binary/BCD and 12/24-hour dates carry through seconds, minutes, hours, day of
week, month and two-digit year. Leap years follow the two-digit four-year cycle;
year99 wraps to00 without touching ordinary RAM at32h. Alarm top bits11 mean
don't-care independently for each field. No alarm interrupt is invented during
SET or divider reset.

DV010 selects the supplied 32768-Hz clock; DV110/111 hold the divider reset.
The default QUALIFIED policy rejects other bases/test modes without mutation.
The explicit CLASSIC_STOP policy instead retains all DV encodings, stops/clears
phase and UIP outside010, and resumes the existing divider at010. This follows
the classic AT functional convention in src/nvr_at.c, not alternate-frequency
or test-mode emulation. The [real-BIOS probe](pcs286-boot-probe.md) selects it
because BIOS1.42 writes00h to A during startup. Readback, stopped calendar/PF,
reset retention and resume are tested; DSE/invalid-calendar errors remain.
Following divider release, the first update starts after16384 edges; UIP rises
eight edges before it and completion follows65 edges later. These are native
edge approximations of244µs and1984µs, not an analog accuracy claim. Updates
then recur every32768 edges. The chosen phase origin and PF first-edge phase
are deterministic model policies, not measured silicon phase relationships.
All15 documented periodic rates are implemented; RS changes do not reset the
divider. Sticky PF permits skipping invisible repeated events without losing
elapsed phase. Deadline reports the next observable transition, or IDLE/zero.

SET aborts a pending update and clears UIE/UIP, while the divider and periodic
source continue. Clearing SET does not restart the divider or resume the
aborted transfer. The A–D controls and ordinary RAM stay available during UIP.
The time/calendar/alarm bytes are still readable during the eight-edge warning;
their accesses during the actual transfer return UNSUPPORTED because hardware
does not define their values. DEBUG observes the last committed bytes. Invalid
date/BCD values may be programmed raw, but update completion returns UNSUPPORTED,
leaves calendar/UF unchanged and clears UIP. It consumes time through that
boundary; the owner must handle the failure, not claim a successful update.
SET/reprogramming can repair the calendar. No invalid date is normalized.

DSE is explicitly unsupported. SQWE is retained and reset-cleared; SQW is
unconnected and there is no exported waveform. A board needing that output or
another chip's D/battery semantics needs an explicit supported profile. This
does not require simulating the oscillator electronics or physical RAM cells.

Warm board reset preserves calendar, RAM, A, SET/DM/24h, divider phase and any
update in progress. It clears B interrupt/SQW enables and C, resets index/mask
as a board lifecycle policy, and resynchronizes the two outputs. CPU-only reset
does not reset RTC. CMOS export is a pure register snapshot, **not a phase-aware
save-state**; restore construction starts the documented model phase anew.

## Failure and ownership

IRQ and NMI-mask outputs are mandatory fallible callbacks. The owner connects
IRQ to PIC IRQ8 and NMI mask to the existing checks helper. No per-register I/O
wait or guest CPU exception is invented. First output failure is retained;
accepted state and callback effects survive. Later mutations stop until reset.
A failing readC clears C internally but does not publish a successful caller
read value. A reset stops publication at its first failed endpoint; a subsequent
explicit reset resynchronizes both. IDLE from a required endpoint is invalid.

Callbacks may inspect state/DEBUG/export but cannot mutate or destroy the
device. Single-owner operation is required. Advance accepts uint64 counts,
checks overflow before mutation and consumes only the prefix through a failing
event; state exposes the consumed count. Pure inspection remains possible after
failure. Disconnect maps/outputs before destruction while recipients are alive.

## Coverage and pending integration

Authored tests cover336 month/year/hour rollovers across all four formats,
February/leap behavior, all16 rate selections, eight enable combinations,
wildcards and nonmatches, UIP boundaries/undefined transfers, SET abort,
phase-preserving reprogramming, C/D side effects and DEBUG, read-only registers,
ordinary RAM/century preservation, reset, invalid input, allocation/output
failures before/after effects, reentry, overflow, partitioned advancement and
independent instances. Real cascaded PIC delivery selects IRQ8/vector70h and
requires C acknowledgment plus EOIs; real checks retain causes while70h masks.
The checks/286 roundtrip now uses the real RTC ports instead of its old mask
fixture, enters vector2 with IF0, clears via61h and returns with IRET. That test
uses a stopped/depleted RTC and does not claim scheduler synchronization.

GCC16.2 UCRT64 Debug and Release:142 pass and one public Headland acceptance
skip,143 registered; Python50; provenance53 components/272 files without errors;
catalogue32 machines/five locales; diff check passes. Two initial test-authoring
errors (allocator failure index and incomplete binary-to-BCD reprogramming) were
corrected before these final suites. The canonical worklog records the commands
and evidence. No physical captures, firmware, media, MSVC, GUI or
strict-clock validation are supplied by these authored tests.

The subsequent clock-link delivery shares the existing PIT adapter and adds
elapsed-cycle synchronization, read-C rearming and distinct warm/epoch reset
contracts. Next: KBC and complete board scheduling/composition. Existing private Headland
qualification and public acceptance remain pending. Headland mem2mem stays off;
CPU strict-clock gate remains closed. No publication was requested.
