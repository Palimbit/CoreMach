# CoreMach

CoreMach is the portable emulator engine extracted from BluMach. It contains
instance-owned CPU and device state, rational virtual time, generic buses,
machine systems, a runtime API and deterministic tests. The optional BluMach
Qt 6 launcher and headless frontend are included as consumers of the engine.

This is an experimental **0.2.0-dev** tree. The PCS 86 and M15 are under
validation; PCS 286 is an experimental system that boots local BIOS 1.42 and
MS-DOS 3.30a in the validated configuration. POST and boot do not establish
complete hardware fidelity. Nominal CPU timings include documented estimates
and explicit fallbacks; this is not cycle-exact emulation.

The historical catalogue contains additional machines which are not implemented
by this engine. Availability comes from the runtime registry.

## Build

Engine, headless frontend and tests require C11, CMake 3.21+ and a build tool:

Use a full Git clone for differential tests and provenance checks. Tests export
pinned historical references from Git into the build directory. Source archives
can build the engine with `-DBUILD_TESTING=OFF`.

```sh
cmake -S . -B build/engine -DBUILD_TESTING=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build/engine --config Debug --parallel
ctest --test-dir build/engine -C Debug --output-on-failure
```

For the optional launcher, install Qt 6 and Python 3, then configure with
`-DBLUMACH_BUILD_PORTABLE_QT=ON`. The headless engine does not require Qt,
SDL, OpenAL or the classic emulator. Windows local GUI builds use MSYS2 UCRT64;
launch them with `tools/run-blumach-portable.ps1`. See [launcher instructions](README-Portable.md).

CPU/library targets, C headers, executable names and profile keys keep their
existing `blumach` names in this extraction to avoid changing integration
behavior. The build project and archive are named CoreMach.

## Validation and provenance

```sh
python -B tools/provenance_audit.py
python -B tools/catalog_builder.py check
python -B -m unittest discover -s tools/tests
```

Firmware and guest media are local inputs and are not shipped. Synthetic
fixtures are authored tests. External SingleStepTests data is opt-in and stays
outside the repository. See [extraction record](doc/architecture/coremach-extraction.md),
[component provenance](provenance/components.json), [COPYING](COPYING),
[fork notice](FORK-NOTICE.md) and [AUTHORS](AUTHORS).

Source history and authorship are retained with a versioned commit map.
Historical firmware, NVR bytes, large executable artifacts and an IDE binary
are removed from CoreMach's ancestors. Original BluMach commit IDs remain in
the provenance manifest and resolve to verified filtered commits locally.
Classic implementation files are absent from the current tree.
