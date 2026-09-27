# Functional enhanced AT keyboard

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`blumach_at_keyboard` supplies a native byte-protocol keyboard independent of
the [AT controller](pcs286-at-kbc.md). It composes with the real 8042 and PIC in
tests. Its [individual engine clock link](pcs286-keyboard-clock.md) is now
implemented. A [coordinated pair](pcs286-keyboard-pair.md) now handles peer timing
with a common logical clock. Production board wiring and a runnable PCS286
factory remain pending. This does not establish a portable firmware boot.

## Evidence, identity and reuse

The governing development reference is IBM Technical Reference 6183355,
March 1986, enhanced 101/102-key section, printed 4-36 through 4-64
(particularly 4-38..4-48 commands and 4-50..4-60 scan tables).
Canonical asset `ibm-enhanced-keyboard-technical-reference-1986`, SHA-256
`09e2968263400b6bcd1bc0ce4fae0c2d2cc0958ff89bd5a28b068c7f046898da`.
The immutable local-only PDF stays outside Git. Special-sequence and set3
tables were visually checked. IBM 6280070 (1985) describes a different 84-key
protocol: its treatment of F0/F2 and some defaults must not be mixed into this
explicit enhanced profile. AB83 identifies the selected model, not an observed
Olivetti keyboard. The preserved PCS286 controller identity M5L8042-243P does
not establish the external keyboard's exact hardware or firmware.

Classic `src/device/keyboard_at.c` at
`4769e40524bc194747b142f3e7ec908ae4df0897` supplies the scan mappings and the
command/default organization. The portable PCS86 named-key mapping is reused.
Miran Grca, Fred N. van Kempen and BluMach notices are retained; neither source
is changed. Global host state, BIOS-dependent reset shortcuts, stale-command
heuristics and ACKs for invalid commands are excluded. Set3 per-key programming
is implemented from the manual instead of retaining the classic TODO.

## Implemented boundary

| Area | Functional contract |
|---|---|
| Initialization | Explicit native power-on interval then BAT; LEDs on during BAT, off afterwards; AA enables scanning only after accepted delivery |
| FF reset | ACK first, uninterrupted uninhibited acceptance interval, then BAT and AA; no BIOS-specific short delay |
| ED/F3 | LEDs and typematic option, scanning suspended during the option; invalid data sends FE, a new command cancels the option |
| EE/FE | Echo; resend last accepted non-FE byte, preserving pending option/reply; no prior byte yields explicit unsupported state |
| F0/F2 | Select/query scan set1/2/3, ID FA AB83; response bytes have their own bounded storage |
| F4/F5/F6 | Enable/clear; defaults/disable; defaults/enable. Defaults restore set2, typematic2B and set3 types, retaining LEDs |
| F7..FD | Set all or individual set3 key types; multiple individual IDs accepted until a new command; invalid IDs send FE |
| Input | Named engine key identifiers, duplicate levels and host repeats ignored; current modifiers select Print Screen/navigation variants |
| Typematic | Last pressed eligible key only, no fallback on release; inhibited link cannot fill FIFO with repeats |
| Queue | 16 scan bytes; complete event sequence must fit; command replies are independent and precede scans |
| Backpressure | IDLE/CAPACITY means no byte accepted; preserve byte and retry after configured byte interval |
| Failures | Other endpoint errors stick with consumed clock prefix and retained queue; never generate guest FE or replay uncertain effects |

Invalid keyboard command bytes produce the documented FE. This is distinct from
an unsupported API input or a host callback failure, which remains a host status.
Native BAT checks the constructed model's available state; it does not run or
claim to validate an authentic MCU ROM, RAM chip or electrical keyboard matrix.

The existing engine input enumeration exposes 83 named keys, including Print
Screen/Pause and the ISO extra key. ANSI excludes that extra key. It lacks
F11/F12, Num Lock and keypad key identifiers; this component does not silently
renumber the shared ABI or claim a complete 101/102-key frontend. Set3 accepts
the profile's valid programming IDs even when no corresponding input identifier
exists. ISO and ANSI backslash codes are separate.

Normal set2 Pause is explicitly unsupported: the printed 1986 table has a
seven-byte sequence while the classic implementation has eight bytes including
a second E1. The discrepancy is not silently resolved into a hardware fact.
Set1 Pause, set3 Pause and Ctrl+Pause are tested. Other unmodeled keys reject
before changing device state.

## Explicit policies and timing

The caller supplies all initialization/BAT/acceptance/byte durations plus the
native rational clock rate. Tests use 1MHz, power-on150ms, BAT300ms,
acceptance500us and byte100us. The first three follow documented bounds;
100us is a synthetic byte-boundary service interval, **not serial wire timing**.
Typematic uses ceiling conversion of `(1+D)*250000us` and
`(8+A)*2^B*4170us`, the nominal IBM formula; oscillator tolerance and scan-matrix
sampling are not simulated. No wall-clock thread is involved.

When inhibited, transmission and typematic stop; release starts a full byte
interval and full initial repeat delay. This restart phase is an explicit
functional policy, not measured keyboard firmware behavior. Power-on and BAT
continue while inhibited; FF acceptance requires uninterrupted release.
Same-edge order is phase completion, transmission, repeat generation.
One large advance and partitioned advances produce the same timestamps/state.

Overflow preserves earlier whole sequences plus a separate overrun marker
(FF set1, 00 set2/3), and drops later events until it drains. This follows the
manual's 17th-byte/whole-sequence description; another response-code paragraph
says replace the last byte. The selected policy does not claim reconciliation
with every firmware revision. Released keys still update physical levels.

Inputs while scanning is suspended update physical levels without scans.
Resumption creates no new make for a held key and no orphan break for a press
that was not scanned. Current modifier levels determine special packets at
each event. Commands interrupt pending replies at accepted byte boundaries;
there is no partial serial-byte replay or physical serial mode autodetection.

Configuration and host services are copied at construction, with no callbacks.
Inspection is pure. Mutation/destroy from callbacks is refused except synchronous
`set_inhibit` feedback during send: this is necessary when the real controller
accepts a byte and immediately lowers its clock. It changes only semantic line
state and cannot recursively execute the device. Peripheral reset recovers host
failures and cancels queues/keys, retaining lifetime cycles and external inhibit.
CPU-only reset does not reset the keyboard. Lifetime overflow rejects unchanged.

## Validation and remaining work

`pcs286-component.at-keyboard-contract` exercises 81 ordinary named-key mappings
in all three sets, Print Screen/Pause variants, ANSI/ISO, default and programmed
set3 types, all128 typematic options with a nonintegral native clock oracle,
power-on/BAT/FF/LED phases, command replacement/resend/invalid options, FIFO whole
sequences/overflow/response priority, disabled edges, duplicates, host repeats,
two held keys, reentry, allocation failure, sticky failures before/after effects,
every transfer in ID and Print Screen packets, backpressure, cycle overflow,
instance isolation and partition-equivalent timestamps.

The integration test uses the actual keyboard, controller and cascaded PIC:
translated make/break, ID translation83→41, OBF/inhibit, IRQ1/INTA/vector31h,
data-read/EOI and FF ACK held until the CPU reads it before the BAT interval.
Its small-step scheduler is test-only; it is not the production board scheduler.

Native keyboard-block Debug/Release:145 pass plus one intentional public Headland skip
(146 registered). Python50; provenance55 components/281 files without errors;
catalogue32 machines/five locales. No new MSVC, ASan, GUI, physical keyboard,
authentic keyboard ROM, firmware/media boot or remote CI result is claimed.

Individual KBC/keyboard clock links are now tested; see their linked document
for that block's totals; the coordinated pair records newer integration results.
Next is board A20/reset/IRQ/input composition. Exact Olivetti keyboard profile, set2 Pause reconciliation,
missing input identifiers and controller diagnostic/low-DATA limitations remain
explicit. Strict CPU clock gate stays closed; Headland mem2mem stays disabled.
No commit, push, PR, CI change or merge in this local session.
