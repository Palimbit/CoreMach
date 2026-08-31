# Olivetti PCS family

BluMach initially supports three members of Olivetti's compact Personal
Computer Systems range. They share a product-family identity, but they are not
minor revisions of one motherboard and must not share firmware blindly.

| Model | BluMach machine ID | CPU | Documented memory | Current result |
|---|---|---|---|---|
| PCS 86 | `olivetti_pcs86` | NEC V30, 8 MHz | 640 KiB plus optional onboard EMS | POST, Resident Diagnostics and MS-DOS 3.30a boot |
| PCS 286 | `olivetti_pcs286` | Intel 80286, 12 MHz | 1-4 MiB | POST, Setup and floppy boot; partial hardware fidelity |
| PCS 386SX | `olivetti_pcs386sx` | Intel 80386SX, 16 MHz | 1-8 MiB | Usable partial emulation: POST, Setup, MS-DOS, internal VGA, 80387SX and 100 MB IDE; broader software and EMS validation pending |

Detailed pages:

- [Olivetti PCS 86](olivetti-pcs86.md)
- [Olivetti PCS 286](olivetti-pcs286.md)
- [Olivetti PCS 386SX](olivetti-pcs386sx.md)

## Preservation rules

Firmware selection is model-specific. A disk carrying an Olivetti label may be
useful for boot validation without being original distribution media for that
machine. BluMach documentation therefore distinguishes:

- **documented**: supported by a manual, brochure, board photograph or other
  identified source;
- **implemented**: represented by the current emulator code;
- **validated**: observed completing a test in a named configuration;
- **approximation**: sufficient for current progress but not yet demonstrated
  to match the physical circuit.

Original ROM and disk captures are never modified. Interleaved ROMs, flat disk
images and test profiles are derivatives and must retain their provenance and
hashes outside the source tree.

## Common operating notes

All three machines use onboard video in their factory configuration. Select
`Internal` rather than adding a generic VGA card. The PCS 286 and PCS 386SX use
Phoenix/Olivetti resident diagnostics and CMOS data whose error messages are
often useful evidence of a missing hardware behaviour, not merely bad user
settings.

The repository contains emulation code and documentation, not redistributable
firmware or commercial operating-system media. Users must provide the required
ROM files themselves.
