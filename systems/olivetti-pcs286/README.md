# Olivetti PCS 286 — contract scaffold

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

This directory is not an emulator implementation. It supplies draft headers
and CMake interface targets for the next test and component implementation
phase. Calling the declared factory currently fails to link; there is no
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
- `include/blumach/systems/olivetti_pcs286.h`: typed machine configuration and
  future runtime factory signatures.

Existing memory, floppy drives/media, PVGA1A and UART/SPP APIs remain reuse
candidates; their present implementation does not imply full PCS286 fidelity.
Build `blumach_pcs286_contract_check` to compile each header independently.
This is structural validation, not component behaviour testing.

The [P0 test foundation and agent boundaries](../../doc/architecture/pcs286-p0.md)
now supply a running generic bus/memory/clock oracle and compiled initial
acceptance tests. The five missing implementation targets are explicitly
reported as skipped, not passing emulation tests. No firmware has been run.
