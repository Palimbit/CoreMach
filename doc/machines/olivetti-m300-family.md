# Olivetti M300 family

`M300` is a commercial family name, not a single compatible motherboard.
BluMach therefore identifies each board generation independently and does not
assume that firmware, memory maps or support logic can be exchanged between
models with similar names.

## Status vocabulary

- **Documented hardware** means that the component or configuration appears in
  Olivetti primary service documentation.
- **Modelled** means that BluMach has a dedicated implementation of that
  component or behaviour.
- **Functional substitute** means that a compatible 86Box device supplies the
  externally visible function, but is not a chip-accurate model of the part on
  the board.
- **Observed** records a test actually completed in BluMach. It does not imply
  broad software or electrical timing validation.

The established machines below remain **partial emulations**. M300-02 is a new
machine target with an exact firmware dump but still awaits its first run-time
validation. The table describes the strongest observed path, not a claim of
complete hardware reproduction.

| Model | BluMach ID | Documented platform | Observed path | Important boundary |
|---|---|---|---|---|
| M300 with IF378 | `olivetti_m300_if378` | 386SX/16; Intel 82230, 82231 and 82335; IN108, GO477 and GO481 | Resident Diagnostics and MS-DOS 3.30a | 82230/82231 and GO477 are composed from compatible AT devices; GO481 is not yet reproduced internally. |
| M300-02 BA013/16 | `olivetti_m30002` | 386SX/16; VLSI TOPCAT VL82C320/VL82C331; WD90C11C | Exact R1.02 image connected; code compiles; POST pending | Shares the recovered BA013 core, but the 16 MHz path and final R1.03 service BIOS remain unvalidated. |
| M300-02F BA013/25 | `olivetti_m30002f` | 386SX/25; VLSI TOPCAT VL82C320/VL82C331; WD90C11C | Cold POST, memory paths, EMS register activity and boot | TOPCAT is an initial register/memory-map model; the 87C310, Acer FDC and MSI glue are aggregated through compatible devices. |
| M300-08 BA319/BA324/BA325 | `olivetti_m30008` | 386SX/20; OPTi 82C283 plus 82C206; OAK OTI067 | POST, Setup, calendar rollover, floppy and MS-DOS 3.30a | 82C206 is represented by common AT functions; IDE and wider software validation remain pending. |
| M300-15 BA320 | `olivetti_m30015` | Intel 386SX/25; AMD 386SX/25 documented as a later alternative; OPTi 82C283 plus 82C206; OAK OTI067 | POST, Calendar PASS, floppy and MS-DOS 3.30a | The support-chip composition is functional, not chip-accurate; IDE and broad software validation remain pending. |

## M300 with IF378 CPU card

The modular machine combines an IF378 386SX CPU card, IN108 bus adapter,
GO477 multifunction board and GO481 video adapter. Board revisions UC.097/093
and UC.112/113 are documented. The CPU support logic is split physically:
82230 supplies clock, RTC/CMOS, interrupt and bus control; 82231 supplies timer,
DMA, memory mappers and refresh; 82335 supplies memory mapping, parity and reset
logic.

BluMach models the 82335 directly. Common AT PIC, DMA, PIT, RTC and port-61
devices provide the observable 82230/82231 functions. Generic IDE, WD37C65-
compatible floppy and NS16450 serial devices approximate GO477. Small I/O
wait-state helpers are retained because Resident Diagnostics depends on the
buffered-board timing; they are explicitly functional timing approximations.
The internal GO481 video board is not modelled, so an external compatible VGA
must currently be selected.

The service guide documents 1 to 12 MiB through motherboard memory and AMB2678
expansions. The present selector exposes this range in 1 MiB steps because the
complete population table has not yet been reconstructed; not every exposed
step is claimed as a verified factory population.

Known firmware metadata (firmware is not distributed):

- expected file: `roms/machines/m300_if378/BIOS.ROM`
- logical image: 65,536 bytes
- SHA-256: `858C46B968832D3CE26C8ABBD9A825782427F8DB1D674F571F2F9B20F02A6E9A`
- identification observed: PEQX / Resident Diagnostics 1.09, 1990-06-26

## M300-02 and M300-02F: BA013/16 and BA013/25

Olivetti documents BA013/16 as the 16 MHz M300-02 and BA013/25 as the 25 MHz
M300-02F. Their real chipset is VLSI TOPCAT: VL82C320 system controller and
VL82C331 bus controller. Video uses WD90C11C, WD90C64 and an ADV BT476 DAC.
The remaining board logic includes an 8042, 87C310 serial/parallel controller,
an Acer floppy controller and an Olivetti MSI hard-disk interface/buffer.

BluMach has a dedicated initial TOPCAT implementation for configuration
registers, shadowing, physical/logical bank mapping and EMS page registers.
The diagnostic-code-driven refresh helper is a recovery aid inferred from
firmware behaviour, not a documented electrical implementation. The existing
WD90C11 core supplies compatible video behaviour; its DAC path is not an exact
BT476 model. PC87310 and common IDE functionality currently aggregate the
87C310, Acer FDC and MSI roles.

The documented memory populations are exactly 2, 4 or 10 MiB: 2 MiB on the
board plus either a 2 or 8 MiB option. The 128 KiB logical firmware image is
mapped as one physical 27C010: OVC in the lower half and diagnostics/system
BIOS in the upper half. OVC is called at `E000:3000`; it is not a conventional
separate `C0000h` option ROM.

The emulator exposes two distinct machine identities sharing this hardware
core:

- `olivetti_m30002`: fixed 16 MHz BA013/16, R1.02 currently selectable;
- `olivetti_m30002f`: fixed 25 MHz BA013/25, R5.01 currently selectable.

Future revisions for the same board/speed belong in the existing BIOS selector,
not in additional machine entries. A change of board clock or commercial
machine identity receives its own entry and NVR namespace.

Known firmware metadata:

- M300-02 1.02: 131,072 bytes, internal date 04/28/92, SHA-256
  `5E3E0BF74741C7C68B81711E410B5900132C1F81D1F4240F683A97DDC3288177`
- M300-02F 5.01: 131,072 bytes, SHA-256
  `4BEC628E10388D656C571DEE2E8A99500C098D273F8B61D79A8E53B7744441E4`

The service guide lists 1.03 as the later BA013/16 service level. The recovered
1.02 dump is therefore useful and authentic, but does not close the search for
the final 1.03 revision.

Warm reset, an independent DOS EMM driver and a broader software set remain to
be tested.

PCS 11 and PCS 33 have independent product records under the PCS family. They
reuse the current BA013/TOPCAT emulation where supported, but are not aliases
of M300-02F; see `olivetti-pcs11.md` and `olivetti-pcs33.md`.

## M300-08 BA319/BA324/BA325

The real board uses a 20 MHz Intel 386SX, OPTi 82C283 memory/AT/data-bus
controller, separate 82C206 RTC/NVR/DMA/PIC/PIT support, 8042, 87310
serial/parallel controller, National floppy controller, MSI IDE logic and OAK
OTI067 video. OTI067 video memory is 256 KiB, expandable to 512 KiB.

BluMach models the 82C283 register and shadow behaviour and fixes the ISA/AT
bus at the documented 10 MHz. The 82C206 is not yet a distinct device; common
AT components provide those functions. PC87310 and common IDE aggregate the
documented I/O and storage support. The internal OTI067 uses a separate logical
EVC extraction even though the service guide describes one physical 27C010
system EPROM.

Documented RAM populations exposed by BluMach are 2, 4, 8, 10, 14 and 16 MiB.
The manual explicitly excludes 6 and 12 MiB and notes that the soldered 2 MiB
are lost when 16 MiB is installed.

Known firmware metadata:

- system BIOS logical image: 65,536 bytes, SHA-256
  `A9929E5195E74510C636B2C24E87FA1F7F0034DD64FD16EC2E947F070AE391B6`
- EVC logical image: 32,768 bytes, SHA-256
  `421A9737C890D48C44593A05218140145438297676247222B60264AD3FDDB40A`

## M300-15 BA320

The service guide identifies the original CPU as an Intel 386SX at 25 MHz and
records AMD 80386SX-25 as an alternative introduced without a board-level
change. The rest of BA320 closely follows the M300-08 architecture: OPTi
82C283, separate 82C206, 8042, 87310, Acer FDC, MSI IDE support and OAK OTI067.
Video memory is 512 KiB.

BluMach permits compatible 386SX vendors and fixes the ISA/AT bus at the
documented 8.33 MHz. As on M300-08, the 82C206 and discrete I/O/storage chips
are currently represented through common AT and compatible multifunction
devices rather than dedicated silicon models.

Documented RAM populations exposed by BluMach are 4, 8, 10, 14 and 16 MiB.
The manual explicitly excludes 6 and 12 MiB and notes that the soldered 2 MiB
are lost at 16 MiB.

Known firmware metadata:

- system BIOS logical image: 65,536 bytes, SHA-256
  `2E6E6BD47A1726D261A57531D20DF121F0651F76A0E62E83FBEA124F9194B4F6`
- EVC logical image: 32,768 bytes, SHA-256
  `421A9737C890D48C44593A05218140145438297676247222B60264AD3FDDB40A`

Olivetti records that BIOS 1.06 differs from M300-08 in machine identifiers and
in programming an 8.33 MHz instead of 10 MHz AT bus. BIOS 1.07 additionally
corrects early POD clock initialisation.

## Catalogued but not released

M300-30 and M300-30P prototypes reach diagnostics and DOS in the recovery tree,
but enabling VL82C486 shadowing loses 384 KiB through unresolved split-memory
relocation. They remain research records, not selectable emulations. M300-01,
-02, -04, -05, -10, -25/P500-E and -28/PCS44 are preserved as leads only; a
similar name is not sufficient evidence to reuse another M300 model.

## Sources

- [Olivetti Pocket Service Guide, M300 modular platform (chapter 10)](https://www.ardent-tool.com/Olivetti/Docs/service_guide/systems1/cap10.pdf)
- [Olivetti Pocket Service Guide, M300-08 (chapter 28)](https://www.ardent-tool.com/Olivetti/Docs/service_guide/systems1/cap28.pdf)
- [Olivetti Pocket Service Guide, M300-15 (chapter 29)](https://www.ardent-tool.com/Olivetti/Docs/service_guide/systems1/cap29.pdf)
- [Olivetti Pocket Service Guide, M300-02/M300-02F (chapter 40)](https://www.ardent-tool.com/Olivetti/Docs/service_guide/systems1/cap40.pdf)
- [M300-02 R1.02 preserved dump record](https://theretroweb.com/motherboard/bios/olivetti-m300-02-bios-1-02-6904790be6b23201031634.bin)

No firmware image is distributed by BluMach. Expected names, layouts, hashes
and observations are recorded for identification and preservation only.
