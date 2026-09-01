# Olivetti PCS 46/C

## Identity

| Field | Documented value |
|---|---|
| BluMach machine ID | `pcs46c` |
| Commercial model | Olivetti PCS 46/C |
| System board | BA2036; based on the T.1 BA2044 base assembly |
| CPU | Intel i486DX2/50, 25 MHz external bus |
| Memory | 4 MiB soldered plus one 4, 8, 16 or 32 MiB SIMM: 4/8/12/20/36 MiB total |
| Main controller | ETEQ Cheetah ET6000 |
| AT support | 83C206Q RTC/CMOS, DMA and interrupt functions |
| Super I/O | National Semiconductor PC87310 |
| Video | Cirrus Logic CL-GD5428, 1 MiB, integrated OVC |
| Current status | Bootable **partial emulation** |

The Olivetti service guide groups PCS 46/C with related M4-4x, M4-6x and PCS4xx
designs. `M4-46` is nevertheless a distinct commercial model/board variant,
not an alias for PCS 46/C. BluMach records the shared design family without
collapsing those product identities.

## Mapping from the real board to BluMach

- The existing ET6000 model supplies memory control, shadowing and the board's
  configuration interface.
- Common AT PIC/DMA/PIT and `nvr_at` currently provide the external behaviour
  of the physical 83C206Q. This is a functional substitute, not a dedicated
  83C206Q implementation.
- PC87310 supplies floppy, serial, parallel and primary IDE behaviour. The
  service guide also identifies Olivetti MSI hard-disk buffer/logic; its
  electrical buffering is not separately modelled.
- The dedicated onboard GD5428 profile loads OVC from the lower half of the
  same preserved image and fixes video memory at the documented 1 MiB.
- The 8042 keyboard/mouse controller uses the compatible Olivetti KBC model;
  its original controller firmware is not distributed.

The preserved logical image represents one physical 27C010. Its lower 64 KiB
contain OVC and its upper 64 KiB contain Resident Diagnostics/system BIOS. The
board presents OVC at `C0000h` and system BIOS at `F0000h`; BluMach models those
two decode windows rather than mapping the entire image linearly.

## Firmware identification

- expected file: `roms/machines/pcs46c/OLIVETTI.BIN`
- size: 131,072 bytes
- SHA-256: `E34EE6ADE7CEA7D40FA15D63C3AFAF43CD68E05932841D877915741716BD1995`
- Resident Diagnostics identification: 1.05, 1993-07-07
- OVC identification: 1.09

The hash is identification metadata, not permission to redistribute the image.
BluMach does not include the firmware.

## Validation boundary

Observed in BluMach:

- cold POST and 4 MiB memory detection;
- internal GD5428 video and OVC execution;
- CMOS diagnostics;
- floppy and MS-DOS 3.30a boot;
- a second boot using the same NVR state;
- Ctrl+Alt+Del warm reset.

Still pending:

- Setup date/time persistence after editing;
- IDE with documented factory geometry and storage diagnostics;
- the other documented RAM populations;
- wider DOS/application and expansion-card compatibility;
- chip-specific 83C206Q behaviour and electrical/cycle accuracy.

Consequently the machine is useful and bootable, but the documentation and GUI
must not label it a complete or cycle-accurate emulation.

Primary source: [Olivetti Pocket Service Guide, M4-4x / M4-6x / PCS4xx](https://www.ardent-tool.com/Olivetti/Docs/service_guide/systems2/cap2.pdf).
