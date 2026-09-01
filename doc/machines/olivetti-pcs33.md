# Olivetti PCS 33

PCS 33 is a separate 1992 PCS office product, not an alias for PCS 11 or
M300-02F. A contemporary announcement describes a 25 MHz 80386SX, 2 MiB RAM,
1.44 MiB floppy and IDE disks from 40 to 120 MiB.

## Platform relationship

The preserved Resident Diagnostics 6.03 image is identified by its source as a
PCS 33 dump. BluMach now gives it the independent machine identity
`olivetti_pcs33`, fixed at 25 MHz, while reusing the recovered BA013/25 TOPCAT
core. That is sufficient for an initial model, but it is not
enough to declare every board revision, enclosure, connector and factory
option identical. A PCS 33-specific service chapter or physical inspection
remains desirable.

BluMach therefore keeps three different layers:

- product: PCS 33, with this independent ficha;
- emulator machine: `olivetti_pcs33`;
- hardware core: shared BA013/25 TOPCAT model;
- firmware profile: PCS 33 6.03.

## Current boundary

- Observed before the split: PCS 33 6.03 follows the BA013/25 boot path.
- Pending after the split: repeat POST, Setup and boot validation under the new
  independent machine/NVR identity.
- Modelled: TOPCAT, compatible WD90C11 video and functional I/O substitutes.
- Pending: PCS 33-specific physical validation, exact external assembly,
  documented disk tests and broader software compatibility.

Firmware identification metadata:

- expected filename:
  `roms/machines/pcs33/BIOS-R6.03.ROM`
- size: 131,072 bytes
- SHA-256: `DBB08FCA6FC31448B6ED9A312FBACD3AAE9DA57AEFE40E411F1EFE953680B2F2`

Source: [contemporary 1992 PCS range announcement](https://www.techmonitor.ai/technology/olivetti_adds_low_end_easy_to_run_models_to_pcs_line).

BluMach records metadata only and does not distribute firmware.
