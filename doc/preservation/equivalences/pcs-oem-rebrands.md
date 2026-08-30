# PCS OEM, rebrand and clone relationships

| Product A | Product B | Relationship | Confidence | Evidence and limitations |
|---|---|---|---|---|
| Olivetti PCS 286 | Triumph-Adler Dario 286 / P35 | Same TA-built motherboard marketed under both brands | `confirmed` | TA technical manuals, board photographs, contemporary press and collector comparison. A distinct Dario BIOS dump has not yet been preserved. |
| Olivetti PCS 386SX | Triumph-Adler Dario 386SX / P45 | Same executable platform and firmware code | `confirmed` | Separate LOW/HIGH dumps interleave correctly. Video BIOS and system BIOS code are byte-identical; unused EPROM padding is `00h` on Olivetti and mostly `FFh` on Dario. |
| Olivetti PCS86 | Triumph-Adler manufacturing | Manufactured by TA for Olivetti | `strong` | Contemporary reporting explicitly attributes PCS86 manufacture to TA, while also stating that TA did not market it in the cited UK range. |
| Olivetti PCS 286/S TI | PCS 286 service replacement or PCS 286S revision | Possible shared service/revision board | `probable` | 2021 community photographs and discussion identify TACT82300 + OLIMCU16. No service bulletin has yet established the exact sales identity. |
| Olivetti PCS 286S 16 MHz | Triumph-Adler Dario 286S / P35S | Probable commercial counterpart | `probable` | Both names occur in Microsoft compatibility material, but no matched firmware or primary board comparison is preserved. |
| Olivetti PCS 286 | Quelle/Privileg-badged PCS | Reported mail-order rebrand | `reported` | A Classic Computing collector reports seeing a Privileg-badged example. No serial plate, board photograph, manual or ROM dump is presently available. |

## Rules for adding a clone

1. Record the commercial name, country, approximate date and visible model or
   board identifiers.
2. Compare board population, connectors, chipset markings and firmware labels.
3. Hash every independently obtained firmware dump, even when it is believed to
   be identical.
4. Compare executable regions separately from unused EPROM padding.
5. Add a selectable machine only when software-visible behavior or required ROM
   paths justify it. A badge-only variant may remain a catalogue relationship.
