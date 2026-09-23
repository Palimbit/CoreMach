# BluMach Portable — development build

This package contains the portable engine, a headless frontend and the Qt 6
desktop frontend. It is an experimental `0.2.0-dev` integration build, not a
stable release or a promise of compatibility with the inherited BluMach/86Box
product or its machine configurations.

The historical catalogue includes machines that are not yet emulated by this
engine. Currently, the Olivetti PCS 86 and M15 can be launched. Both remain
under validation; a successful POST or boot does not prove full hardware
fidelity.

## Getting started

1. Run `bin/BluMach-portable` (or `bin/BluMach-portable.exe` on Windows).
2. Choose PCS 86 or M15 in the catalogue, then use **Crear máquina…**.
3. Select a local resource folder or the required firmware files. The PCS 86
   needs two 32 KiB EPROM halves; the M15 needs a 64 KiB local BIOS image.
4. Optionally select a floppy image. For PCS 86 XTA, use only a disposable
   writable working image, never an original archival disk.
5. Test pause, reset, media operations and shutdown as well as boot.

Machine profiles are saved locally in `machine.blumach.json`. They do not
import 86Box configurations, and the schema may still change before a stable
release. Firmware and media bytes are not copied into profiles or the package.

No BIOS, ROM, floppy, hard-disk image or proprietary sales-flyer scan is
distributed with BluMach Portable. See `COPYING`, `FORK-NOTICE.md` and `AUTHORS`
for source and licensing information.
