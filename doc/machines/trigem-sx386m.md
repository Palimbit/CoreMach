# TriGem SX386M (KS-II)

## Independent identity

The SX386M is a **TriGem** motherboard and belongs to TriGem's own 80386SX
OEM family. It is not an Olivetti model, derivative or rebrand. A documented
chipset shared by unrelated computers is reusable emulation infrastructure,
not evidence of a commercial or engineering-family relationship.

TriGem manufactured this 16 MHz board for OEM systems sold at least as the
**Emerson Elite SX386/16** (SR160, FCC ID H9Z2008) and **CMS Enhancements ESP
SX386M**. An archival motherboard catalogue also associates the **Epson
CW3S16** with the board, but no Epson-labelled board comparison or firmware
dump has yet confirmed that relationship. It therefore remains a probable OEM
identity rather than an implemented alias.

The Epson-hosted *SX386MC Operations Guide* describes the related but
materially different **Epson CW3S20/C**: 20/8 MHz operation, 32 KB cache and up
to 32 MB. It is contextual family evidence and a candidate for a future machine,
not an alias specification for this 16 MHz board.

TriGem, also known as Sambo Computer/TG Sambo, was founded in Seoul in 1980.
It produced the SE-8001 in 1981 and became both a Korean PC manufacturer and
an international OEM.

## Documented hardware

- Intel 80386SX at 16 MHz; documented 8 MHz low-speed mode
- optional Intel 80387SX, enabled by jumper J2
- Headland HT101SX, HT113 and GC102-PC chipset
- parity DRAM: 512 KB, 640 KB, 1, 1.5, 2, 3, 4, 4.5, 5, 6 or 8 MB
- onboard IDE, floppy, two serial ports, one parallel port and AT keyboard
- four 16-bit and two 8-bit ISA slots; external ISA video adapter
- 254 × 218 mm OEM motherboard

## Firmware and emulation

The preserved linear 64 KB AMI image identifies itself as
`DG2X-6080-020491-KB` and is expected at
`roms/machines/sx386m/EESX386.BIN`. BluMach records its metadata but does not
distribute it. The BIOS accesses the HT113 interface at 1ECh–1EFh, clears 64
mapping registers and programs CR0–CR4, including CR2=DAh; it does not use
fast-A20 port 92h.

The BIOS also exposes the board-level sequencing of its integrated legacy I/O.
It first probes the LPT and UART addresses selected by CMOS byte 36h while the
motherboard ports are still disabled, so that an actual ISA add-in card can be
reported as a conflict. It then writes the selection byte to port 03F3h: bits
0–1 select LPT at 378h, 3BCh, 278h or disabled, while bits 2 and 3 disable COM1
and COM2. BluMach models this latch and delayed activation without assigning it
to an unverified physical IC.

Status is **experimental**. The authentic BIOS identifies Emerson, completes
the 4096 KB memory test and reaches the system-configuration summary using an
external Paradise VGA. The BIOS now enumerates COM1 at 3F8h, COM2 at 2F8h and
LPT at 378h without false add-in-card conflicts. Setup input, DOS boot, all RAM populations, parity/NMI,
the 8 MHz mode and EMS remain pending. Exact peripheral ICs and the keyboard
controller mask ROM are not preserved, so AT-compatible functional models are
used and documented as such. BluMach `master` does not yet provide an exact
RAM-population list in the generic machine descriptor, so the current selector
also permits undocumented 128 KB intermediate values; the documented values
above are the historically valid choices.

## Commercial versions and supplied software

The preserved Emerson computer is model **SR160**, carries FCC ID **H9Z2008**,
was made in Korea and contains the TriGem board. Its firmware and physical
machine are directly documented, but no contemporary Emerson price list,
brochure, factory software list or complete package inventory has yet been
located. Consequently BluMach does not infer a bundled operating system or hard
disk configuration for the Emerson product.

The available Epson evidence must be divided carefully:

| Product | Evidence | Present conclusion |
|---|---|---|
| Epson CW3S16 | Third-party 218-page manual scan record and an archival motherboard-catalogue association with the 16 MHz SX386M | Probable 16 MHz OEM product. A firmware dump, Epson-labelled board photograph and an authoritative specification page are still required. |
| Epson CW3S20/C | Epson-hosted 220-page *SX386MC Operations Guide* | Confirmed related 20 MHz cached TriGem system, but a different platform from the emulated KS-II. |

The CW3S20/C guide describes how that system was supplied and operated. The
package contained the system and power cord, a 101-key keyboard, MS-DOS
diskettes, a GW-BASIC diskette, and printed MS-DOS and GW-BASIC guides; a
monitor was obtained separately. The installation chapter explicitly uses
MS-DOS 4.01. The DOS media also carried `TGSS.COM`, a TriGem utility for
switching between the 8 and 20 MHz operating speeds. The guide discusses 20 MB
and 40 MB hard-disk configurations and documents ROM-resident advanced tests
for hard disk, floppy, keyboard, video, printer and serial ports. No evidence
has been found for additional Epson productivity applications, so none are
claimed as bundled software.

No redistribution permission has been established for these manuals. BluMach
records links and bibliographic metadata only and does not copy the PDFs.

## Firmware still sought

- Epson CW3S16 / 16 MHz: AMI identification
  `DNSX-6080-051690-KB`, recorded by an archival catalogue but not available as
  a verified ROM image.
- TriGem SX386MC / Epson CW3S20/C: AMI identification
  `DVSX-6080-060290-KB`, printed in the operations guide; no ROM dump located.

The two downloadable files currently associated with the 16 MHz board are the
linear and LOW/HIGH representations of the same Emerson firmware. Interleaving
the pair produces the existing `EESX386.BIN` hash; it is not a second Epson
BIOS.

## Sources

- [TriGem official company history](https://trigem.co.kr/company/company_history.jsp)
- [TriGem SX386M (KS-II), The Retro Web](https://theretroweb.com/motherboards/s/trigem-sx386m-ks-ii)
- [Emerson Elite SX386/16 preservation thread, VOGONS](https://www.vogons.org/viewtopic.php?t=86204)
- *CMS Enhancements ESP SX386M*, Micro House Technical Library board reference
- [Epson-hosted SX386MC Operations Guide](https://files.support.epson.com/pdf/cw3s2c/cw3s2cu1.pdf) — CW3S20/C, different 20 MHz revision; contextual only
- [Epson CW3S16 third-party manual record](https://manualzilla.com/doc/7429223/epson-cw3s16-canadian-product-specifications) — bibliographic and partial-text evidence only
- [TriGem OEM/rebrand research record](../preservation/equivalences/trigem-oem-rebrands.md)

No manual, photograph or firmware image is copied into BluMach. Photographs
from the VOGONS contributor require permission and credit to Justin D. Morgan.
