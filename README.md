# BluMach

**Documented preservation and emulation of distinctive historical PCs.**

[![License: GPL-2.0-or-later](https://img.shields.io/badge/license-GPL--2.0--or--later-blue.svg)](COPYING)
![Project status: active development](https://img.shields.io/badge/status-active%20development-orange.svg)

BluMach is a preservation-focused fork of
[86Box](https://github.com/86Box/86Box). It combines 86Box's accurate,
low-level x86 emulation with a curated historical catalogue, explicit evidence
and reproducible machine configurations.

BluMach is an independent project and is not an official 86Box build. Existing
86Box copyright notices and authorship are preserved. See
[FORK-NOTICE.md](FORK-NOTICE.md) for provenance and redistribution details.

> [!IMPORTANT]
> BluMach is in active development and does not yet publish official binary
> releases. The repository does not distribute ROMs, operating systems or other
> proprietary machine software.

## What makes BluMach different

- A built-in **Collection** organised by manufacturer and family.
- Historical sheets that separate known hardware from the current emulator
  implementation.
- Visible preservation states and evidence, including known approximations and
  unresolved questions.
- Reproducible profiles for machines that have enough firmware and validation
  evidence to be created safely.
- A unified Qt manager for exploring the catalogue and running local machines.
- Continued access to the broad processor, bus and peripheral emulation inherited
  from 86Box.

Catalogue illustrations are editorial concept images or board recreations, not
documentary photographs. Their purpose and provenance are recorded in the
[asset notes](src/qt/catalog/images/README.md).

## Historical collection

The current catalogue contains 21 product entries across six families from
three manufacturers:

| Manufacturer | Families | Public machine notes |
| --- | --- | --- |
| Olivetti | Prodest, PCS, M300 and PCS 4x/C | [Prodest PC 1](doc/machines/olivetti-prodest-pc1.md), [PCS family](doc/machines/olivetti-pcs-family.md), [M300 family](doc/machines/olivetti-m300-family.md), [PCS 46/C](doc/machines/olivetti-pcs46c.md) |
| Triumph-Adler | Dario | Catalogue research in progress |
| TriGem | SX386 | [SX386M](doc/machines/trigem-sx386m.md) |

Every catalogue entry carries one of these preservation states:

| State | Meaning |
| --- | --- |
| **Validated** | A documented configuration has passed the project's current validation checks. |
| **Partial** | The machine is usable, with known approximations or incomplete validation. |
| **Experimental** | An early implementation exists but needs more evidence or testing. |
| **Research** | The historical record is being developed and no usable profile is offered. |
| **Not bootable** | The product is identified, but BluMach cannot currently boot it faithfully. |

The state applies to the current BluMach implementation, not to the historical
importance or completeness of the surviving physical machine.

## Evidence and fidelity

BluMach distinguishes between claims that are:

- **documented** in contemporary or manufacturer material;
- **observed** in hardware, firmware or a repeatable emulator test;
- **inferred** from related systems or incomplete evidence; or
- **hypothetical** and retained only as a research lead.

Machine notes describe the latest known result and remaining approximations.
They should not present a plausible inference as verified hardware fact.

## Building and running

BluMach currently targets source builds. It retains the CMake build system and
dependencies of 86Box; the repository workflows provide the exact configurations
used for [Windows/MSYS2](.github/workflows/cmake_windows_msys2.yml),
[Linux](.github/workflows/cmake_linux.yml) and
[macOS](.github/workflows/cmake_macos.yml).

The [upstream 86Box build guide](https://86box.readthedocs.io/en/latest/dev/buildguide.html)
is a useful dependency reference, but its release paths, branding and support
channels do not describe BluMach.

To run an emulated machine, provide a local ROM directory containing firmware
you are legally entitled to use. Firmware availability in a preservation record
does not imply permission to redistribute it.

## Contributing

Contributions are welcome through pull requests. Historical-machine changes
must cite their evidence, state their limitations and keep firmware or other
restricted assets out of Git. See [CONTRIBUTING.md](CONTRIBUTING.md) for the
project checklist.

## Compatibility with 86Box

Internal executable names, configuration files, machine identifiers and source
paths may continue to use `86Box` where changing them would break compatibility.
This is an implementation detail and does not imply endorsement by the upstream
project.

Where a problem is reproducible in an unmodified 86Box build, consult the
[upstream project](https://github.com/86Box/86Box). BluMach-specific changes
should be discussed in a BluMach pull request until a dedicated public support
channel is established.

## License and provenance

BluMach is distributed under the
[GNU General Public License, version 2 or later](COPYING), consistently with its
86Box base. Optional third-party components remain under their respective
licenses.

The GPL covers the emulator source and the BluMach-authored assets explicitly
distributed under it. ROM images, operating systems, proprietary documentation
and reference photographs are separate works and are not automatically covered
by the emulator's license.

See [FORK-NOTICE.md](FORK-NOTICE.md) and the source-file headers for authorship
and redistribution information.
