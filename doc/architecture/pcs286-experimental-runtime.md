# PCS286 experimental Qt runtime

The `olivetti-pcs286-experimental` adapter uses the existing portable Qt frontend
used by PCS86 and M15. It does not introduce a separate diagnostic UI.

The boot probe and runtime share the board composition, including the private
provisional Headland/IOC02 profile. The runtime owns each board independently and
provides video, keyboard input, pause/resume, cold reset, stop and CMOS persistence.
Cold reset rebuilds RAM/components while retaining CMOS; it is not a KBC CPU reset.

## Deliberate restrictions

- Combined 128 KiB local firmware, 1 MiB RAM, one 1.44 MiB floppy drive.
- Optional floppy image is read-only, selected at startup; no hot insertion yet.
- No HDD, second drive, commercial variant selection or catalogue availability claim.
- Initial CMOS is an explicit diagnostic calendar, not a manufacturer preset.
- The runtime uses provisional instruction estimates at nominal 12 MHz:
  available manual bases plus serialized waits, otherwise an explicitly
  diagnostic 12-clock fallback (the former 1 us quantum). DMA/refresh retain
  a diagnostic minimum 12-clock quantum, not hardware-qualified DMA timing.
  Peripheral/video time uses the same estimate with retained fractional
  nanoseconds (250/3 ns per clock). This is not a cycle-exact model.
  The strict CPU clock API remains gated. Inspect timing_manual_steps and
  timing_fallback_steps for coverage; neither implies hardware qualification.
- Existing private chipset behavior is reused, not certified as electrically exact.

Run through `tools/run-blumach-portable.ps1` with machine ID above and asset roles
`firmware` and optionally `floppy-0`. Assets remain external and are not distributed.

## Local evidence, 2026-09-26

### Follow-up validation, 2026-09-27

The earlier first-launch limitations below are historical. The user subsequently
confirmed visible POST, interactive keyboard operation and MS-DOS3.30a boot in
the shared Qt frontend. Local Release now passes175 tests with one intentional
public Headland acceptance skip. Provenance:70 components,334 files,zero errors.
These observations do not qualify every diagnostic, protected-mode workload or
chipset detail. Cross-platform CI for this follow-up remains pending publication.
See `pcs286-instruction-timing.md`, `portable-speed-controls.md` and
`pcs286-runtime-performance.md` for timing limits and reproducible measurements.

### Initial launch record

Base: 10fd75ce7aba4ff7cb6ddc1ecdda4dabc6c1fabd, PR 210.
UCRT64 GCC 16.1, Debug, legacy OFF, portable Qt ON: 168 tests passed,
one existing public Headland acceptance test skipped (169 registered).
Provenance audit: 64 components, 314 files, zero errors.
The new synthetic runtime test covers two isolated sessions, boundaries, keyboard,
pause/reset/resume/stop, geometry and CMOS export. Probe CLI tests also pass.

The Qt executable was launched with locally verified BIOS 1.42 and clean DOS 3.30a
media in read-only mode. Window creation and process responsiveness are observed;
interactive Setup navigation and DOS boot are NOT yet validated in this runtime.
No Release/MSVC/remote CI validation for these local changes yet.
