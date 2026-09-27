# Functional AT keyboard controller

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

`blumach_kbc8042` is a bounded native controller for composition, not a complete
PCS286 keyboard subsystem. The [separate native keyboard](pcs286-at-keyboard.md)
now supplies a tested enhanced AT byte protocol. Individual
[engine attachments](pcs286-keyboard-clock.md) and the
[coordinated pair](pcs286-keyboard-pair.md) are tested. No machine registration
or firmware boot is claimed.

## Evidence and reuse

Primary reference: IBM 5170 Technical Reference 6280070 (1985), printed pages
1-42 through 1-55. Canonical asset `ibm-at-dma-technical-reference-1985`, SHA-256
`04e83b4df038ddc9d2c45f7465ff19c1ed5628ae9826d0e3066aadd2c0740276`.
The original remains local-only outside Git; the output-port table was checked
against the rendered page. This documents the IBM AT boundary, not the exact
Mitsubishi M5L8042-243P mask or Olivetti board wiring.

Reused `src/device/kbc_at.c` at
`4769e40524bc194747b142f3e7ec908ae4df0897`: command/buffer organization and the
entire 256-byte translation table. Original Miran Grca, EngiNerd and BluMach
notices remain. The classic file is unchanged. Stateful board globals, polling,
vendor replies and BIOS-specific reset logic were initially excluded. The
explicit Olivetti profile now reuses CFh acceptance and80h/84h raw readback,
described below. No new firmware
or restricted document is included in the source tree.

The existing canonical `kbc-rom-plan` records the missing authentic ROM and a
possible later UPI-42 backend. This explicitly behavioural implementation is
compatible with retaining that later option; it does not impersonate that ROM.
No electrical/MCU emulation is required for this software-visible boundary.

## Coverage

| Boundary | Implemented and qualified scope |
|---|---|
| Host I/O | Configured byte data/status ports, IBF/OBF, command/data and system flags; status bit4 updated when OBF is produced for the required unlocked-switch profile |
| 20/60 | Read/write command byte; all bits retain RAM readback, with reserved bits externally inert. PC mode bypasses translation in the AT profile. In the explicit PCS286 profile, XLAT remains active with PC mode, following the classic Olivetti path. |
| AA | Delayed native-model self-test response55, sets system flag and disables interface; does not validate an MCU ROM or physical hardware |
| AD/AE | Inhibit/enable keyboard; already accepted data is retained; no unsolicited model-specific reply |
| C0 | Explicit board input straps; locked-switch profile unsupported rather than inventing command-response/scan classification |
| CF (explicit Olivetti profile only) | Consumed after normal IBF delay, no response or output change; classic functional compatibility, not measured MCU behavior |
| 80/84 (explicit Olivetti profile only) | Delayed raw vendor latch read/parameter write. All256 byte values retained;84 does not drive outputs. Standard D1 refreshes latch, D0/pulses keep AT output semantics. Provisional separation, not proven MCU internals. |
| D0/D1 | Buffer status, idle data/logical inhibited clock, A20 and active-low reset; D1 does not force reset inactive. Low clock drive inhibits the byte link; AE/enabling CCB releases it. D0/D1 read/modify/write while inhibited is tested. Low DATA drive is unsupported (D1 requires bit7=1) |
| F0..FF | Pulse selected low four output bits and restore only bits previously high; a bit already low generates no extra edge; overlapping pulses/D1 refuse |
| Keyboard receive | Single-byte pending-to-OBF pipeline, no extra FIFO; full/disabled paths reject without changing byte or translation state; sender must retain and retry rejected bytes |
| Translation | Classic table, F0 break conversion, extended prefixes, controller replies bypass conversion; malformed F0/high-bit suppression retained as inherited compatibility policy, not independently verified silicon behavior |
| Keyboard transmit | Host input is admitted while inhibited and enables at delayed consumption; controller parameters are excluded. Separate fallible endpoint; no synthetic ACK/BAT/RESEND. IDLE means not accepted and retains IBF for an explicit native-delay retry |
| IRQ1 | OBF gated by command-byte enable; mask changes preserve data, ordinary data read clears OBF/IRQ; tested with real cascaded PIC |
| Failures | First endpoint error retained, accepted effects survive, no replay/guest exception; read result staged on failure; reset can recover |

AB interface test, AC internal RAM/PSW dump, E0 test pins, other vendor commands,
arbitrary controller RAM access, PS/2 mouse injection, serial parity/timeouts
and locked-switch filtering are unsupported without a fabricated reply.
Keyboard-bound host bytes are admitted while the interface is disabled and
enable it at input consumption, following the classic functional policy below.
Keyboard-to-controller delivery still returns IDLE while disabled. Exact MCU
firmware behavior remains a qualification task. Keyboard command semantics, LEDs, host keys, BAT and typematic belong to
the separate keyboard device. No claim of complete POST compatibility follows.

## Timing, ownership and limits

All four delays and the native rational rate are mandatory caller configuration.
The test profile uses 1 MHz and input2/self-test10/output3/pulse6 native cycles:
only the approximately6-microsecond pulse has an IBM reference. Other values
are synthetic test choices, not measured PCS286 timings. Self-test adds its
delay to input consumption. `next_deadline` reports positive native delays or
IDLE/zero; there is no background worker or wall-clock use.

Stable same-edge order is pulse restoration, input consumption, output-ready
commit, then changed output publication. This avoids a transient uninhibit at
coincident input/output readiness. Accepted bytes are translated at receipt,
not reinterpreted if the command byte changes before output publication.
Occupied response buffers refuse without mutation. Empty OBF reads now return
the retained output latch (initially zero), following the classic AT port-data
read policy. The real BIOS probe exposed the old IDLE result as a bus-contract
stop during an ordinary drain read. Repeated empty reads generate no new byte,
IRQ or time advancement and cannot consume a pending reply. Callback failures
still propagate; no ACK or success indication is fabricated. See the
[BIOS execution evidence](pcs286-boot-probe.md).
Lifetime cycle overflow rejects. A failed advance consumes only its prefix
through the failed event; inspection reports that boundary.

Create copies configuration/host services and publishes no maps or callbacks.
All four line callbacks are required and fallible; keyboard-command absence
rejects transmission. Outputs may inspect/DEBUG, but cannot reenter mutation,
destroy the component or synchronously deliver an ACK back into it. A future
keyboard queues that ACK for its own timed delivery. Line callback IDLE is a
host contract error; only keyboard transmit explicitly supports backpressure.

Peripheral reset cancels buffers, pending translation/transmit/pulse and sticky
failure, restores explicit initial A20/reset and publishes all lines. It retains
the native lifetime counter. CPU-only reset must preserve the controller. The
board must latch reset requests for an architectural CPU boundary; KBC cannot
run or reset an executing CPU from its output callback.

## Validation and next step

### Host transmission while inhibited, 2026-09-26

The BIOS probe exposed a host/controller boundary error: after ADh, an ordinary
write to60h was rejected with IDLE before entering IBF. The CPU subsequently
stopped. In the classic implementation, `kbc_ibf_process` enables the keyboard
before forwarding such a byte; this is general behavior, not an FFh special case.

The portable controller now admits the byte when IBF and output capacity allow,
then clears CCB bit4 and releases the modeled clock at the configured input
consumption boundary. It publishes the enable before calling the separate
keyboard. The latter supplies ACK/BAT/other replies through its own state machine.
Parameters of controller commands60h/D1h/84h do not take this path. Other CCB bits,
A20/reset, translation prefix and the raw Olivetti latch are preserved.

IBM6280070 printed1-50/51 (PDF66/67) defines IBF and distinguishes keyboard data
from controller parameters;1-52/53 defines disable/enable, and1-48 distinguishes
the physical inhibit switch. Page1-51 was visually checked against the original.
These descriptions do not explicitly specify this automatic CCB transition.
The exact enable transition is therefore recorded as a **classic-derived
functional policy**, not a newly proven Mitsubishi or IBM mask-ROM fact. The
older rejection was an unqualified limitation, not a documented requirement.

If publishing enable fails, the accepted enable state and IBF remain, no command
is delivered, and the sticky host error stops execution. A delivery failure may
have already accepted the byte and is never retried. Only the keyboard endpoint's
explicit IDLE means not accepted and schedules an input-delay retry. Enabling is
not repeated as an edge during those retries. Full/pending OBF still refuses
admission without enabling or discarding data. No synthetic ACK, fallback success,
firmware-PC condition or host-to-guest exception conversion was added.

Tests cover every byte in both AT/Olivetti profiles, deferred enable, IBF busy,
inhibited receive, real delivery, no unsolicited reply, retained register bits,
low-clock release, controller parameters, output collisions, endpoint
backpressure, callback reentry, failures before/after effects and reset recovery.
The native and engine-attached keyboard pair now reset after ADh without AEh and
receive actual FAh/AAh. An authored CPU CLI program verifies the same path. The
classic, PCS86 and shared video implementations remain unchanged in this increment.

### Full command-byte latch, 2026-09-26

IBM 5170 Technical Reference 6280070, printed pages 1-51 and 1-52, says the
byte following command60h is placed in the controller command byte. It marks
bits7 and1 reserved and instructs software to write them as zero; it does not
specify a rejected host transfer. The portable controller therefore retains all
eight bits in RAM cell20h and returns them through command20h. Reserved bits
have no modeled external effect. Documented IRQ, system-flag, disable, PC-mode
and translation behavior remains unchanged.

This matters after the real absent-ATA timeout: the unchanged BIOS writes CCB
47h at F000:55E8. Bit1 is retained instead of becoming a host
`BM_STATUS_UNSUPPORTED`; there is no BIOS-value special case. Authored coverage
writes and reads back all256 values in both command profiles and checks the
documented status/inhibit/IRQ outputs independently.

### PCS286 PC-mode translation, 2026-09-26

The real BIOS trace writes command bytes64h and65h: both set PC mode and XLAT.
The AT profile previously gave PC mode precedence and therefore passed the
keyboard's set-2 F1 stream (`05`, `F0 05`) unchanged. The BIOS continuation loop
at `F000:C268` accepts set-1 `3B`, so the physical-key event could not continue.

The classic controller already contains an explicit Olivetti rule: PC mode does
not imply that Olivetti keyboard bytes are already set1, and XLAT still converts
the set2 stream. The portable `BM_KBC8042_COMMANDS_OLIVETTI_PCS286` profile now
uses the same rule. The generic AT profile keeps its prior PC-mode bypass. This
is selected by machine/controller profile, never by a BIOS address, byte value
or firmware hash. It does not invent an ACK, diagnostic result or key press.

Authored component coverage checks F1 make and break in both profiles: AT with
CCB60h returns `05/F0 05`; PCS286 returns `3B/BB`, with the existing break-prefix
state and output pipeline. The policy remains classic-derived functional
compatibility because the authentic M5L8042-243P mask ROM is not preserved.


### Olivetti80h/84h compatibility, 2026-09-26

The same explicit profile now accepts80h and84h. These are **command bytes
written to I/O64h**, not chipset I/O addresses80h/84h. Headland GC101/GC102
documentation supplies the external controller selection (`/CS8042`, PDFp5)
and A20 gate/HLDA behavior (PDFp13); it does not establish these Olivetti firmware
commands. Those pages were visually rechecked in the canonical more-legible
`headland-ht101a` reference. No exact PCS286 wiring is inferred from them.

Classic `write_cmd_olivetti` reads raw P2 on80h, and `write_cmd_data_olivetti`
stores the84h parameter without calling `write_p2` and its A20/reset handlers.
The portable profile represents that behavior with a separate vendor readback
latch. It starts at configured initial P2;84 stores all bits after normal input
delay and80 snapshots them into the existing delayed response pipeline. D1 also
records its accepted full byte, while its existing driven-output masks/checks
remain. D0 still reports actual AT outputs/buffer state. CF, keyboard activity,
enable/disable and pulses do not rewrite the vendor latch. Peripheral reset
restores it; CPU-only reset preserves it with the rest of the KBC.

This is an explicit functional approximation of the inherited latch behavior.
The classic shares P2 with polling/serial state and increments P1 after80h;
those unrelated side effects are not copied. Raw readback never drives reset,
A20 or low serial DATA on a later buffer publication. There is no BIOS address
test, forced expected value or manufactured ACK. Authentic MCU firmware or
targeted observations can replace this policy later.

Tests cover all256 parameter values, deferred commit, no line/keyboard output,
response capacity and full-buffer snapshot, real IRQ/read-clear, D0 versus raw
latch, D1/A20, reset pulse coexistence, parameter replacement/refusal, unchanged
C0 straps, host failure and reset. Generic AT still refuses80/84/CF;8Bh for the
different PCS286S is not added. A CPU-level authored program round-trips zero
through84/80 and reaches HALT without a CPU reset. The BIOS now proceeds beyond
this command block; current stop and validation are in the
[boot probe record](pcs286-boot-probe.md).

### Olivetti CFh compatibility, 2026-09-26

The first `BM_KBC8042_COMMANDS_OLIVETTI_PCS286` increment added only CFh to the default AT
command set. The probe selects and reports this profile. The classic
`write_cmd_olivetti` handler at the revision above accepts CFh without a reply
or output effect; its "POST separator/no-op" label is implementation evidence,
not a verified Mitsubishi243 firmware specification. No test result or firmware
address selects the behavior. Authentic MCU firmware is still missing.

CFh uses existing IBF admission/consumption, including busy rejection and
replacement of an unfinished parameter at consumption. It does not clear a
pending/full OBF, restart output/pulse delays, lose a translation prefix,
change IRQ/A20/reset/inhibit or send keyboard data. Existing status command
bit and elapsed time still change normally. Generic AT still rejects CFh;
all other unimplemented commands still reject in either profile.

Authored tests cover both profiles, invalid/copied configuration, IBF delay,
parameter replacement, pending/full output and IRQ, break prefix, active reset
pulse, DEBUG purity, retained host failure and reset. A CPU-level CLI test
polled consumption, checked no reply and confirmed84h still rejected at that stage. BIOS1.42
now passes CFh and stops at80h, F000:F252 after614584 attempts. Debug/Release
154 pass plus one public Headland skip/155; Python59. See
[current probe evidence](pcs286-boot-probe.md). This supersedes the older
component-only validation and next-step text below.

Authored tests cover initial/self-test status and deadlines, empty/full buffers,
DEBUG and failures without caller mutation, raw controller replies, enable/IRQ
changes with OBF full, missing/rejecting keyboard endpoint, retained bytes and
break prefix, accepted/rejected command replacement, standard make/break,
right Ctrl/Print Screen/Pause,512 untranslated byte cases,256 pulse/initial
combinations including already asserted reset, idle and coincident events,
100 partition comparisons, two instances, allocation/reset/lifetime overflow,
callback reentry and failures before/after effects on all five endpoints.
Real PIC IRQ1/INTA/vector31h/read/EOI is exercised twice with make/break data.

Controller-block validation: Debug/Release144 passes plus one intentionally skipped
public Headland acceptance (145 registered), Python50, provenance54 components/
279 files with no errors, catalogue32 machines/five locales. No new MSVC, GUI,
ASan, physical keyboard, authentic firmware, media, remote CI or full-board
evidence. Initial allocator-failure test used the wrong failure-injection index;
the test was corrected. Review fixed the same-edge inhibit transient and made
D0 reflect logical clock inhibition. A final review added explicit low-clock
drive/release so D0/D1 read-modify-write also works while inhibited. Strict CPU clock gate remains closed and
Headland mem2mem remains disabled.

The keyboard block now also tests direct controller/PIC wiring; its linked
document records the newer suite totals and bounded keyboard profile.
Next: board A20/reset composition using the coordinated pair. Resolve the
listed low-DATA-drive/serial-control limitations and diagnostic profile before claiming a
usable PCS286 POST. Full board scheduling, storage and board qualification stay
separate. Work remains local; no commit, push, PR, CI change or merge.

## Internal RAM and independent buffers, 2026-09-26

Commands20h-3Fh and60h-7Fh now expose the controller's32-byte internal RAM
address space following the classic generic handler. Cell20h remains the
command byte; cells21h-3Fh are retained independent bytes and peripheral reset
clears them. BIOS1.42 specifically uses2Dh/6Dh to clear cell2Dh bit7. The IBM
5170 reference documents separate input and output registers and the diagnostic
dump of controller RAM; the full command ranges are retained classic functional
compatibility, not a claim about the missing Mitsubishi mask ROM.

A full OBF no longer blocks an independent host data write. Thus EDh's first
real keyboard ACK may remain readable while the LED option enters IBF; the
keyboard's second ACK waits until OBF is drained. A response still moving toward
OBF rejects overlap. Tests cover all31 additional cells, reset, isolation from
CCB/lines, the exact2Dh/6Dh cycle and both native/engine keyboard-pair ACK order.

## Auxiliary byte channel used by BIOS 1.42

A retained portable trace reached `F000:5630` after the floppy diagnostic and
stopped because command A8h was still outside the component contract. Bounded
inspection of `F000:5620`–`567A` and `F000:8CF8`–`8F17` establishes the complete
flow: the BIOS enables the auxiliary KBC channel with A8h, unmasks IRQ12, writes
mouse reset FFh through command D4h, and waits for FAh, AAh and an identification
byte. On timeout it uses A7h to disable the channel and restores the PIC mask.
This is a mouse-presence probe, separate from the keyboard POST result.

The explicit PCS286 profile now accepts A7h/A8h and the one-byte D4h parameter.
It records enabled state and can deliver the byte to an optional fallible
auxiliary endpoint. With no endpoint attached, as in the current boot probe,
the byte is consumed and no reply is created. The BIOS must therefore take its
real bounded absent-mouse timeout. Incoming auxiliary bytes, their status bit,
IRQ12 delivery and a mouse protocol device remain outside this component
increment. The generic AT profile still rejects these commands.

Authored tests cover no-device enable/write/disable, attached delivery,
endpoint backpressure without duplication, callback reentry, failures before
and after endpoint effects, rejected-command state retention, self-test and
peripheral reset. A CLI-level authored program verifies the composed board
leaves OBF empty rather than manufacturing reset ACK/BAT. This behavior is
derived from the observed BIOS flow and the inherited controller channel model;
it is not a claim about the missing Mitsubishi mask-ROM implementation.

The retained no-mouse run then reached `F000:5684` and issued controller
command A4h. Code at `F000:5689` waits for its response and sends A6h only when
that response is FAh. The functional controller implements the documented
no-password state by returning F1h. Password loading and enforcement remain
unmodeled; no installed password is fabricated.
