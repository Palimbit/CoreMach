# TriGem SX386 OEM and rebrand relationships

This record keeps commercial names separate from motherboard families. A
shared case, manual family name or catalogue association is useful evidence,
but does not by itself prove byte-identical firmware or identical hardware.

| Product | Underlying platform | Relationship | Confidence | Evidence and limitations |
|---|---|---|---|---|
| Emerson Elite SX386/16, model SR160 | TriGem SX386M / KS-II, 16 MHz | OEM system and present emulation identity | `confirmed` | Photograph, FCC ID H9Z2008, chipset markings and preserved AMI firmware `DG2X-6080-020491-KB`. |
| CMS Enhancements / CMS Peripherals ESP SX386M | TriGem SX386M / KS-II, 16 MHz | OEM system or board-market identity | `strong` | Micro House board reference matches the board layout, I/O, jumpers, memory and slot population. A separately sourced CMS firmware dump is not preserved. |
| Epson CW3S16 | Possibly TriGem SX386M / KS-II, 16 MHz | Reported Epson Canadian OEM product | `probable` | Archival motherboard catalogue and a third-party manual-scan record associate the name with this family. No Epson-labelled board comparison or verified ROM dump is preserved. Do not use the Emerson ROM as proof of Epson firmware identity. |
| Epson CW3S20/C | TriGem SX386MC, 20 MHz | Related later/different TriGem platform | `confirmed-not-alias` | Epson-hosted operations guide specifies 20/8 MHz, 32 KB cache, five 16-bit ISA slots and up to 32 MB. Those characteristics do not match the 16 MHz KS-II. |

## Firmware evidence

| Product or platform | Identification | Preservation state |
|---|---|---|
| Emerson Elite SX386/16 | `DG2X-6080-020491-KB` | Preserved as a 64 KiB image and as its original LOW/HIGH pair; both representations normalize to the same SHA-256. |
| Epson CW3S16 candidate | `DNSX-6080-051690-KB` | Catalogue metadata only; no downloadable verified image located. |
| Epson CW3S20/C / SX386MC | `DVSX-6080-060290-KB` | Printed in the official operations guide; no dump located. |

## Commercial and software evidence

No contemporary brochure or price list has yet been located for the Emerson
Elite SX386/16, CMS ESP SX386M or Epson CW3S16. This is an open preservation
gap, not evidence that such material never existed.

The Epson-hosted CW3S20/C guide gives a reliable package inventory: system and
power cord, keyboard, MS-DOS diskettes, GW-BASIC diskette, MS-DOS User's Guide
and GW-BASIC User's Guide. The monitor was separate. It describes MS-DOS 4.01,
20 MB and 40 MB hard-disk configurations, and the TriGem `TGSS.COM` speed
utility supplied on the DOS media. No additional bundled applications are
documented.

## Criteria for selectable machines

- Emerson may use the current `sx386m` implementation and preserved firmware.
- Epson CW3S16 should remain catalogue-only until its board and ROM identity
  are confirmed. If its ROM later proves byte-identical, a separate commercial
  entry is optional rather than technically necessary.
- Epson CW3S20/C requires a separate machine model, cache/memory work and its
  own firmware dump; it must not be produced by merely raising the current
  machine clock to 20 MHz.
