# Olivetti PCS 286 — board foundation, not yet a runnable machine

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

This directory supplies draft machine contracts and real RAM/firmware backing
storage. It does not implement the complete board. Calling the declared machine
factory currently fails to link; there is no
fake-success implementation or runtime/frontend registration.

Read [the architecture, evidence and work packets](../../doc/architecture/pcs286-portable-contracts.md)
before implementing anything here. The canonical record is `olivetti/pcs286`;
PCS286S is a different platform.

Contract ownership:

- `components/cpu/80286/`: reusable Intel 286 interpreter boundary.
- `components/chipsets/headland/`: memory routing/register boundary.
- `components/chipsets/olivetti-ioc02/`: reusable IOC02 boundary.
- `components/pc/include/blumach/components/at_*.h`, `pit8254.h`, `rtc_at.h`,
  `kbc8042.h`, `keyboard_at.h`, `wd37c65.h`: AT devices and clock links.
- `components/video/include/blumach/components/ims_g171.h`: DAC boundary.
- `include/blumach/systems/pcs286_board.h`: board pins and ports 61h–63h.
- `include/blumach/systems/pcs286_memory.h`, `src/pcs286_memory.c`: implemented
  storage ownership and resolved-offset accesses, without physical decoding.
- `include/blumach/systems/olivetti_pcs286.h`: typed machine configuration and
  future runtime factory signatures.

Existing memory, floppy drives/media, PVGA1A and UART/SPP APIs remain reuse
candidates; their present implementation does not imply full PCS286 fidelity.
Build `blumach_pcs286_contract_check` to compile each header independently.
This is structural validation, not component behaviour testing.

The [P0 test foundation and agent boundaries](../../doc/architecture/pcs286-p0.md)
now supply a running generic bus/memory/clock oracle and compiled initial
acceptance tests. The partial 286 and AT interconnect run real tests; the
Headland, AT PIC and AT DMA gates still skip explicitly. No firmware has been run.

## Backing storage and synthetic composition

`blumach_pcs286_memory` owns private RAM and a copied 128-KiB ROM image, accepting
combined firmware or two 64-KiB low/high lanes. Construction publishes nothing
until complete and releases partial allocations on failure. Byte-wise access
does not depend on host alignment or endianness. The storage capacity is not a
board-population validation. Zero-filled new RAM is an emulator policy only.

The board supplies an already resolved region and offset. This helper does not
decide A20, ROM mirrors, RAM remapping, write-enable signals, open bus, straps,
registers or elapsed time. It preserves the supplied wait count; this must not
be mistaken for established zero-wait hardware. ROM writes return READ_ONLY,
leaving electrical ignored-write policy to the future board adapter. DEBUG is
read-only. CPU-only reset does not reconstruct or clear memory.

`pcs286-component.board-memory` runs an authored reset trampoline at FFFFF0h,
jumps into test RAM, loads SS/SP, pushes/pops and exercises a test I/O latch
through the actual 286 and AT components. Its linear windows and I/O latch are
test-only fixtures, **not a provisional PCS286 chipset in production**. Timing
remains UNKNOWN. The exact PCS286 reset map, Headland/IOC02, interrupt delivery,
clocked execution and runtime factory are still pending.
