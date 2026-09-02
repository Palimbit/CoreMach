# Olivetti Prodest PC 1

The Prodest PC 1 is a compact Olivetti XT-class computer from 1988 built around
the NEC V40. The documented machine can switch between 4.77 and 8 MHz and was
sold with 256, 512 or 640 KiB of RAM, integrated Yamaha V6355-compatible video
logic and a 720 KiB 3.5-inch floppy drive. Its side expansion connector is
ISA-like but omits several power, DMA and interrupt lines; it must not be
presented as a complete internal ISA bus. The rear mouse connector carries a
quadrature mouse/joystick interface, not RS-232.

## Commercial history and configurations

Olivetti/Prodest International introduced the PC 1 in 1987 as the third member
of the consumer Prodest range. It was nevertheless a new Italian x86 design,
unrelated to the Thomson- and Acorn-derived PC 128 and PC 128 S. Spanish sales
are documented by a localized manual, keyboard and MS-DOS 3.20 system disk. A
contemporary report from Informat 88 advertised the base machine at 109,000
pesetas and credited Mario Bellini with its industrial design. In Italy the
machine was supported by a dedicated publication named *PC 1*.

The floppy model was documented with 256, 512 or 640 KiB and one or two
internal MF 3510 3.5-inch 720 KiB drives. An external 5.25-inch 360 KiB drive,
MS 1040 quadrature mouse, JO 1040 joystick, monochrome and colour monitors and a
two-card side expansion enclosure were offered. Period material also advertised
RAM, network, modem, EGA, CD-ROM, hard-disk, television/telematics and music
expansions.

The **PC 1 HD** was a differentiated commercial variant. Italian advertising
documents a 640 KiB model with a 20 MB internal hard disk, while surviving
hardware evidence shows its own internal XT-class disk interface. BluMach does
not yet expose PC 1 HD as a faithful creation option because that controller has
not been modeled. The catalogue creation flow therefore offers only documented
floppy-machine combinations: 256, 512 or 640 KiB and one or two 720 KiB drives.
The 512 KiB, single-drive configuration remains the default. The unified
catalogue form and machine startup have also been validated with 256 and
640 KiB and with two internal 720 KiB drives.

## Firmware metadata

BluMach does not distribute firmware. The local files used for development are:

| Component | Revision | Size | SHA-256 |
| --- | --- | ---: | --- |
| System BIOS | 1.06 | 16,384 bytes | `15566548A275071CA38D83D44C6CBFF634B939EEA34F8D2B19DC4139D634F17D` |
| System BIOS | 1.07, 1988-12-10 | 16,384 bytes | `35EC7738948DACE2C74D3DFDA06E7D699E8B64CA4C9E27C7B9518193CC682DB7` |
| System BIOS | 1.21 | 16,384 bytes | `A2C03DC2E8752EA1632B910B4CE46AF7AB5FC6D4B352D8E21703B2ABB5791F96` |
| Character ROM | 1.01 | 8,192 bytes | `6574984968E187600AA9F80CACAAACA00F266130A0B3851F184B39CC013E30F8` |
| Italian character ROM | 1.02 | 8,192 bytes | `7269EFF083777AC6A44954C2EBC8807413DBA111E6FC65C3283D3BB2B979C61B` |

## Current emulation status

This is a bootable partial implementation, not a complete V40 emulation.
BluMach gives the processor its own device and system-register model while
reusing the NEC V20 instruction engine, which matches the V40's 8-bit external
data bus more closely than the previous V30 substitution. BIOS writes to the
V40 clock and wait-state registers now select 4.77 or 8 MHz, and the peripheral
selection registers control the minimally modeled integrated serial unit.
Generic XT PIC, PIT and DMA devices, plus a translation layer for the V40 floppy
DMA registers, still approximate the other integrated V40 peripherals. The
internal V6355 video variant loads the machine's Italian character ROM.
The Prodest video variant also exposes the DDh/DEh aliases used by the supplied
CRT.COM utility, while retaining the standard 3DDh/3DEh register pair.

With BIOS 1.07 and 512 KiB, the dedicated V40 phase passes resident diagnostics
for CPU, ROM, DMA, interrupts, timer and memory, then boots the Spanish MS-DOS
3.20 disk to DOS. Ctrl+Alt+Del also performs a successful warm reset while
preserving both turbo/8 MHz and normal/4.77 MHz state. On a cold boot, a key
press during the memory count opens the firmware speed prompt; selecting `N`
boots in normal mode, and a subsequent warm reset again passes the interrupt
diagnostic. Hardware-level interrupt, timer and DMA fidelity, GUI cold reset,
systematic keyboard coverage, the minimally modeled serial unit, parallel,
quadrature mouse, incomplete external expansion connector and a representative
software set remain explicit validation items.

## Sources

- [John Elliott's Prodest PC 1 technical notes](https://www.seasip.info/VintagePC/prodestpc1.html)
- [Spanish installation and user manual](https://archive.org/details/pc1instalacionymanualdelusuario)
- [Italian Olivetti Prodest PC 1 brochure](https://archive.org/details/olivetti-prodest-pc-1)
- [TI99IUC PC 1 archive](https://www.ti99iuc.it/web/?artid=134&pageid=articoli_tech)
- [1000bit Prodest PC 1 entry](https://www.1000bit.it/scheda.asp?id=259)
