# Olivetti PCS 11

PCS 11 is a separate commercial and physical product in Olivetti's low-end PCS
range, not an alias for M300-02F. Contemporary descriptions place it in a
compact A4-footprint or booksize enclosure for home and small-business use,
with 2 MiB RAM, 1.44 MiB floppy and a typical 40 MiB hard disk.

## Platform relationship

Surviving board evidence identifies Olivetti BA013 and VLSI TOPCAT
VL82C320/VL82C331 in PCS 11. The preserved PCS 11 firmware releases R2.04 and
R2.07 correspond to the 16 MHz system. BA013 is a reusable emulation platform,
not proof that PCS 11 and M300-02F are the same finished machine.

BluMach now exposes `olivetti_pcs11` as a separate 16 MHz machine identity. It
shares the BA013 TOPCAT implementation with M300-02/M300-02F but has its own
NVR namespace and a BIOS selector containing the exact R2.04 and R2.07 dumps.
This avoids treating a physically different commercial product as an M300
alias. The new target compiles but still awaits POST and operating-system
validation.

## Current boundary

- Product ficha: independent PCS 11 record.
- Emulator machine: `olivetti_pcs11`, fixed at 16 MHz.
- Selectable BIOS revisions: R2.04 and R2.07.
- Pending: POST and boot validation, enclosure-specific behaviour, television
  connection, factory storage/software configurations and broad application
  validation.

Verified PCS 11 firmware metadata (references only; firmware is not included):

| Board | Revision | Internal date | Size | SHA-256 |
|---|---|---|---:|---|
| PCS 11 / BA013 16 MHz | 2.04 | 06/03/92 | 131,072 | `A26220590540E2E4DA71FC5EFD97C9C6EC9E14C4F04CAD705BC60F02366907B8` |
| PCS 11 / BA013 16 MHz | 2.07 | 03/01/93 | 131,072 | `DAEF0B341FF3446DA6C439D7ECABC579FFB29B953FA5307442CB9238C1F28392` |

Both contain OVC 1.06 and identify an 80386SX. R2.04 and R2.07 differ in
10,958 bytes, so R2.07 is a real firmware revision rather than a renamed copy.

Sources:

- [Contemporary 1992 PCS range announcement](https://www.techmonitor.ai/technology/olivetti_adds_low_end_easy_to_run_models_to_pcs_line)
- [Preserved BA013 board record and firmware inventory](https://theretroweb.com/motherboards/s/olivetti-ba013-m300-02)
- [PCS 11 R2.04 preservation package](https://theretroweb.com/motherboard/bios/ba013-2-04-68455d3661669806977912.zip)
- [PCS 11 R2.07 preservation package](https://theretroweb.com/motherboard/bios/olivetti-pcs-11-r2-07-61e6bb82d9618185665720.zip)

BluMach records metadata only and does not distribute firmware.
