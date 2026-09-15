# Building BluMach

BluMach supports 64-bit hosts and Qt 6. CMake 3.21 or newer and Ninja are
required. The GitHub Actions workflows are the canonical, executable lists of
platform packages:

- [Windows with MSYS2 UCRT64](../.github/workflows/cmake_windows_msys2.yml)
- [Linux](../.github/workflows/cmake_linux.yml)
- [macOS](../.github/workflows/cmake_macos.yml)

After installing the dependencies for the host, a typical Qt 6 development
build is:

```text
cmake -S . -B build --preset development -DNEW_DYNAREC=ON -DQT=ON -DSTATIC_BUILD=OFF
cmake --build build
ctest --test-dir build --output-on-failure
cmake --install build
```

Use the `regular` preset for a release build. Qt 5 and 32-bit hosts are not
supported BluMach configurations.

On Windows, configure and build inside the MSYS2 UCRT64 environment. Launch the
result through `tools/run-blumach-local.ps1`; the launcher supplies the UCRT64
runtime and accepts `-VmPath`, `-LogPath` and the workspace ROM directory.

Installed artifacts can be checked without firmware using:

```text
python tools/package_audit.py --help
```

The private `0.1.0-rc.1` reference build has a stricter Windows wrapper which
builds, tests, installs, audits and produces a file-by-file manifest plus an
archive SHA-256 from a clean commit:

```text
powershell -ExecutionPolicy Bypass -File tools/build-private-rc.ps1
```

See [the private RC validation record](releases/0.1.0-rc.1.md) for the evidence
that must be captured before portable-engine work begins.

Never add a local ROM directory, guest media or restricted research material to
a build artifact or commit.
