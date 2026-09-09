# Engineering the Toshiba T5200 model

## Outcome and evidence boundary

BluMach now has an experimental T5200 implementation that boots unmodified
Award V1.30 and its matching Toshiba VGA firmware, reaches Toshiba MS-DOS 3.30,
passes the exercised TEST3 paths and presents the external VGA and internal
orange plasma outputs at the same time. It covers a useful vertical slice of
the plasma T5200 family; it is neither a T5200C model nor a cycle-accurate
reconstruction of Toshiba's custom logic.

In this account, **documented** means stated by Toshiba literature,
**observed** means reproduced by firmware, diagnostics or a controlled guest
probe, **inferred** identifies the most consistent interpretation of those
observations, and **approximate** identifies deliberately compatible behavior
whose original electrical implementation is still unknown.

## Why this machine was difficult

The maintenance manual names the T4758A, T9761, BGS and PDC-GA functions but
does not provide enough register-level detail to recreate them. The keyboard
path also includes an 8749 SCC whose firmware has not been preserved. The
64 KB CG-ROM containing four regional character sets is missing, and the
integrated panel is not a second independent video adapter: it transforms the
same Paradise/BGS display state while external colour VGA remains available.

That last point materially changed the design. Treating the photographs as
evidence for an external-only T5200, or treating the panel as a generic amber
monitor, would both have hidden firmware-visible choices and the simultaneous
physical outputs described by Toshiba.

## From evidence to implementation

The reusable i386DX, AT DMA/PIC/PIT, RTC, FDC, UART, IDE and Paradise PVGA1
cores provide compatible bus-level functions. They do not claim to be the
T4758A or T9761. Small T5200 devices supply only the behavior demanded by the
preserved BIOS: cache/platform register transitions, keyboard status bits and
the sixteen-selector EMS interface with four 16 KB slots at D0000h.

Award V1.30 performs the same 344-operation EMS selector sequence at every
tested memory size. A generated DOS probe then established independent
readback, four simultaneous slots and distinct page families. That evidence
supports the implemented firmware contract, but not an attribution to a
particular gate array or its real timing.

Video required a T5200-specific Paradise/PDC layer. Firmware and TEST3 showed
that CMOS 38h bit 2 selects Plasma versus CRT-only, while other saved fields
select VGA colour/monochrome, panel brightness, CGA level conversion and
Single/Double character presentation. Toshiba VCHAD independently exercised
the PDC's 64-byte table and sixteen intensity ordinals. The external window
therefore retains the colour PVGA1 output while the internal window applies the
observed Toshiba conversion to a separately presented scanout.

The maintenance manual documents Ctrl+Home as recovery when the plasma is
blank and the CRT indicator is lit. All five preserved unmodified system BIOS
revisions lack immediate references to the T5100-like ports 8060h, 8064h and
8066h. An indirect or SCC-local transaction remains possible, but there is no
evidence for copying the T5100 protocol. BluMach consequently implements the
documented one-way result as a machine-scoped keyboard action that temporarily
enables the PDC panel without changing CMOS or the external image.

## Useful failures and rejected shortcuts

Early runs exposed three important wrong assumptions:

- A retained VGA option-ROM signature caused the Award CGA POST branch to
  report a video fault. Coupling port 3C3h to option-ROM visibility, while
  retaining the separate BGS/CGA window, allowed the original POST to make the
  distinction itself instead of bypassing the test.
- PDC private register 15h bit 3 initially looked like a persistent panel
  selector. Traces showed both Plasma and CRT-only completing boot with that
  bit clear; TEST3 persistence instead identified CMOS 38h bit 2.
- A single orange-tinted output looked plausible but contradicted the manual,
  TEST3 and hardware photographs. The final design keeps external colour and
  internal plasma as simultaneous presentations of one adapter.

Generic chipset substitutions were also rejected where they would invent an
identity: no CS8220 is advertised as Toshiba logic, no T3200/T5100 reverse
display key is inherited, and no synthetic regional glyphs are described as
the missing CG-ROM. Patched BIOS images were useful research references but
are neither required nor distributed.

## Compromise ledger

| Subsystem | Current fidelity | Boundary |
|---|---|---|
| CPU and standard AT peripherals | Reused compatible cores | Toshiba integration and timing are approximate |
| System and VGA firmware | Original local images | Metadata only is public |
| Cache/platform registers | New behavioural subset | Electrical owner and side effects incomplete |
| EMS window | Guest-validated behavioural subset | Gate-array ownership and timing unknown |
| Paradise VGA | Reused PVGA1 plus T5200 extensions | Broad mode coverage is incomplete |
| Internal plasma/PDC | Firmware- and diagnostic-driven conversion | Orange calibration and exact analog response unknown |
| CGA/BGS | Functional separate aperture and output conversion | Exact BGS logic and regional CG-ROM unavailable |
| Runtime display recovery | Documented outcome, direct approximation | SCC/KBC protocol and CRT indicator unknown |
| Floppy | Reused AT FDC with observed status | External routing and low-density timing incomplete |
| IDE | Standard initialization | Conner models not yet validated or offered by creation |

## Validation ladder

Host contract tests cover register transitions, EMS mapping and the Ctrl+Home
callback. Guest-authored probes cover all sixteen EMS selectors, four live
slots and installed-memory bounds. Original Award firmware supplies POST and
Setup coverage; Toshiba TEST3 supplies model, memory and saved configuration
checks; VCHAD supplies an independent PDC-table consumer. Toshiba MS-DOS 3.30
then validates both floppy capacities and the simultaneous display paths in a
period environment. Finally, the normal Qt 6/Ninja build, catalogue source
checks and catalogue unit tests exercise public integration.

Original and executable-bearing local media remained write-protected. Writable
tests used disposable derived images; destructive writes were not performed on
preservation originals. Real-hardware timing, analog measurements and
proprietary-slot electrical tests were unavailable and are not implied by the
passing software tests.

## Reproduction and replacement criteria

The reference configuration is a 20 MHz i386DX, 2 MB RAM, Award V1.30, the
1988 Toshiba VGA image, internal video/FDC, a 1.44 MB floppy and simultaneous
external/plasma presentation. The catalogue also exposes validated 2 MB steps
through 14 MB, a 720 KB drive and external-only presentation.

An 8749 SCC dump or a trace of the real keyboard/PDC lines should replace the
direct Ctrl+Home path and model the CRT indicator. A CG-ROM dump should replace
the inferred regional characters. Logic traces or schematics should replace
the narrow cache/platform/EMS subsets. Photometric measurements from a working
panel should replace the host RGB orange curve, and verified Conner images plus
cold-boot tests should enable the historical hard-disk choices.

The principal implementation lives in `src/machine/m_at_toshiba_t5200.c` and
the T5200 extensions in `src/video/vid_paradise.c`. Keyboard integration is in
`src/device/keyboard.c`; the focused runtime contract is exercised by
`tests/t5200_display_hotkey_test.c`. The catalogue entry is under
`src/qt/catalog/source/machines/toshiba/toshiba-t5200/`.

No proprietary ROM, disk, manual, diagnostic program or validation capture is
included in the public tree.
