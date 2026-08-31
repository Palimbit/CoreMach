# TriGem SX386M (KS-II)

## Independent identity

The SX386M is a **TriGem** motherboard and belongs to TriGem's own 80386SX
OEM family. It is not an Olivetti model, derivative or rebrand. A documented
chipset shared by unrelated computers is reusable emulation infrastructure,
not evidence of a commercial or engineering-family relationship.

TriGem manufactured this 16 MHz board for OEM systems sold at least as the
**Emerson Elite SX386/16** (SR160, FCC ID H9Z2008) and **CMS Enhancements ESP
SX386M**. The Epson-hosted *SX386M Operations Guide* describes a materially
different 20 MHz cached revision with up to 32 MB, so it is contextual family
evidence rather than an alias specification for this board.

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

Status is **experimental**. The authentic BIOS identifies Emerson, completes
the 4096 KB memory test and reaches the first-boot CMOS Setup prompt using an
external Paradise VGA. Setup input, DOS boot, all RAM populations, parity/NMI,
the 8 MHz mode and EMS remain pending. Exact peripheral ICs and the keyboard
controller mask ROM are not preserved, so AT-compatible functional models are
used and documented as such. BluMach `master` does not yet provide an exact
RAM-population list in the generic machine descriptor, so the current selector
also permits undocumented 128 KB intermediate values; the documented values
above are the historically valid choices.

## Sources

- [TriGem official company history](https://trigem.co.kr/company/company_history.jsp)
- [TriGem SX386M (KS-II), The Retro Web](https://theretroweb.com/motherboards/s/trigem-sx386m-ks-ii)
- [Emerson Elite SX386/16 preservation thread, VOGONS](https://www.vogons.org/viewtopic.php?t=86204)
- *CMS Enhancements ESP SX386M*, Micro House Technical Library board reference
- [Epson-hosted SX386M Operations Guide](https://files.support.epson.com/pdf/cw3s2c/cw3s2cu1.pdf) — different 20 MHz revision; contextual only

No manual, photograph or firmware image is copied into BluMach. Photographs
from the VOGONS contributor require permission and credit to Justin D. Morgan.
