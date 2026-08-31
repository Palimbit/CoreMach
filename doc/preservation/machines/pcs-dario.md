# Olivetti PCS and Triumph-Adler Dario machines

This matrix describes the models covered by the initial BluMach PCS work. It
does not contain firmware files or software media.

| Model | BluMach machine ID | Hardware status | Firmware status | Notes |
|---|---|---|---|---|
| Olivetti PCS86 | `olivetti_pcs86` | Implemented and boot-tested | BIOS/Resident Diagnostics 1.09 identified | NEC V30, integrated Paradise VGA, MM58167 RTC, XTA disk and LIM/EMS logic. |
| Olivetti PCS 286 | `olivetti_pcs286` | Implemented and boot-tested | BIOS 1.37 and 1.42 identified | Headland GC101A/GC102 family, IOC02, integrated Paradise VGA. |
| Triumph-Adler Dario 286 / P35 | `ta_dario286` | Implemented as the documented PCS 286 motherboard | No separate Dario dump currently required by the implementation | Technical manuals and contemporary reporting support the shared platform; the Olivetti ROM set is a compatibility fallback, not proof that every shipped EPROM was identical. |
| Olivetti PCS 286/S TI/OLIMCU16 | `olivetti_pcs286s` | Implemented at 12 MHz; EMS remains incomplete | BIOS 2.06 identified | BIOS clock modes are 12/12, 12/6 and 6/6 MHz. Community research reports this as a possible service replacement or PCS 286S board revision. |
| Olivetti PCS 286S commercial 16 MHz | none | Documented only | Correct BIOS missing | Kept separate from the 12 MHz TI/OLIMCU16 implementation. |
| Triumph-Adler Dario 286S / P35S | none | Documented lead only | Verified BIOS missing | Microsoft compatibility records prove the product name; exact board and firmware relationship remain unresolved. |
| Olivetti PCS 386SX | `olivetti_pcs386sx` | Usable partial emulation; POST, Setup and MS-DOS boot tested at 1-8 MiB | Phoenix 1.14 with embedded OVC 1.06 identified | Discrete HT101SX + HT113 + GC102-PC model, IOC02, TL16C451FN functional decomposition, WD37C65CJM FDC and integrated PVGA1A. EMS and broad software validation remain pending. |
| Triumph-Adler Dario 386SX / P45 | `ta_dario386sx` | Same usable partial implementation as PCS 386SX | Dario 1.14 identified separately | Executable video and system BIOS regions are byte-identical to PCS 386SX 1.14; only unused padding differs. Brand-specific external differences have not been independently validated. |

## Reported name not promoted to a machine

- **Quelle/Privileg PCS 286:** a collector report describes a Privileg-badged
  example. This is retained as a research lead, not a machine identity.

## Validation boundary

“Implemented” means that BluMach contains a selectable hardware model. It does
not claim cycle-perfect emulation. Known approximations are documented in source
comments and the in-application catalogue. Required ROMs remain external and
must be supplied legally by the user.
