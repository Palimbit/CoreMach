# Manuals and technical documentation

The following publications informed the implementation. They are not copied
into this repository unless a later rights review establishes permission.

| Scope | Document | Source | Use |
|---|---|---|---|
| Dario 286 / PCS 286 | *Technische Unterweisung TA Dario 286*, BA025, June 1990 | [Classic Computing download 175186](https://forum.classic-computing.de/index.php?file-download/175186/) | Board layout, jumpers, memory, ports and diagnostics. |
| Dario 286 / PCS 286 | *Installations- und Bedienungshandbuch TA Dario 286* | [Classic Computing download 175187](https://forum.classic-computing.de/index.php?file-download/175187/) | Installation and operation. |
| Dario 286 / PCS 286 | Dario 286 illustration volume | [Classic Computing download 175188](https://forum.classic-computing.de/index.php?file-download/175188/) | Mechanical and connector reference. |
| PCS 286 | SUPSI VET PCS-286 record/manual | [SUPSI](https://lastin.dti.supsi.ch/VET/sys/Olivetti/PCS-286/00496C-Olivetti-PCS-286.pdf) | Physical and historical cross-check. |
| PCS 286S | Installation/use manual holding | [Museo del Calcolatore record 1795](https://www.museodelcalcolatore.it/file.php?cod=1795) | Bibliographic record; access-controlled, do not mirror. |
| PCS 286S | English installation/operations guide holding | [Centre for Computing History record 19012](https://www.computinghistory.org.uk/det/19012/MS-DOS-Manuals-for-Olivetti-PCS-286S/) | Physical holding; no public scan assumed. |
| PCS 286/S TI | TI TACT82411 technology preview | [DosDays](https://www.dosdays.co.uk/media/texas_instruments/TACT82411_Preview.pdf) | Related TI chipset-family behavior; not proof of exact PCS board wiring. |
| PCS 386SX / Dario 386SX | Photographed PCS 386SX motherboard and component inventory | [Jonathan Dupré](https://www.jonathandupre.fr/articles/33-ordinateurs-old-school/315-olivetti-pcs-386sx/) | Primary physical evidence for HT101SX, HT113, GC102-PC, M5L8042, TL16C451FN and WD37C65CJM population. |
| PCS 386SX / Dario 386SX | Independent Emerson Elite SX386/16 board and BIOS preservation | [VOGONS](https://www.vogons.org/viewtopic.php?t=86204) | Corroborates the HT101SX/HT113/GC102 set and the firmware-visible `1ECh-1EFh` interface. |
| PCS 386SX / Dario 386SX | TI TL16C451/TL16C452 data sheet, SLLS053C | [Texas Instruments](https://www.ti.com/lit/ds/symlink/tl16c451.pdf) | Documents the UART plus bidirectional Centronics functional decomposition. |
| PCS 386SX / Dario 386SX | Headland GC101/GC102, HT18 and HT21 family documentation | Archival component-data-sheet holdings | Comparative evidence only. HT18 is not the PCS 386SX chipset and its revision bits, CR5/CR6, sleep state and fast-A20 port must not be attributed to this board. |
| TriGem SX386M / CMS ESP SX386M | *CMS Enhancements ESP SX386M*, two-page board reference | Micro House Technical Library archival copy; see [The Retro Web board record](https://theretroweb.com/motherboards/s/trigem-sx386m-ks-ii) | Independent TriGem board: 16 MHz CPU, no cache, 8 MB maximum, layout, ISA slots, I/O, jumpers and SIMM populations. Link/metadata only pending rights review. |
| TriGem SX386M family | *SX386M Operations Guide* | [Epson support](https://files.support.epson.com/pdf/cw3s2c/cw3s2cu1.pdf) | Context for a different 20 MHz cached TriGem revision; not the 16 MHz KS-II specification. |

Component datasheets should normally be referenced from the original vendor or
a stable archival host instead of being copied into the tree.
