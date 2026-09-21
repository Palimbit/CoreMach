# BluMach Portable 0.0.1-dev.1 — local test build

This is a **private development preview**, not a public release or a promise
of machine compatibility. It packages the new portable engine, headless tool
and Qt 6 launcher for Windows. The inherited BluMach product retains its
separate `0.1.0-rc.1` identity; this preview does not read its VM files.

## What can be tested

- The launcher opens on a two-machine catalogue: Olivetti PCS 86 and Olivetti
  M15. Each card shows the existing localized historical sheet, technical
  notes and source links. Only machines registered by the portable frontend
  are offered.
- Selecting **Configure and start** opens one form for local firmware and
  optional media. The firmware is never bundled. Floppy images are opened
  read-only. The optional PCS 86 XTA hard-disk input is a *writable working
  image*: use a copy, never a preserved original.
- The machine view supports pause/resume, reset, stop, floppy insertion and
  ejection, frame capture and a return to the catalogue. Returning from a
  running machine asks before stopping it.
- The bundled command-line tool can list and describe both machine IDs.

The PCS 86 needs its two 32 KiB EPROM halves. The M15 needs a local 64 KiB
BIOS image. A boot disk is optional for opening either machine, but needed
to test a boot path. No firmware, BIOS, ROM, disk or software image is in
the archive or repository.

## Windows local test

1. Unpack `BluMach-Portable-0.0.1-dev.1-Windows-*.zip` into a new folder.
2. Run `bin/BluMach-portable.exe` on a supported Windows machine. Qt 6 and
   the required compiler runtime libraries are included.
3. Select a machine, read its sheet, and choose local assets you are entitled
   to use. For the PCS 86, keep any XTA disk image as a disposable working
   copy.
4. Record whether POST, video, keyboard, media operations, pause, reset,
   stop and window close behave correctly. A successful boot alone does not
   validate the machine.

The portable executable reports `0.0.1-dev.1` with `--version`. The package
audit checks both machine IDs, the Qt runtime, package size, version and
absence of firmware/media. No installer, auto-updater or public download is
provided by this preview.

## Known boundaries

- The M15 uses a provisional Intel 8088 model to approximate an 80C88. Its
  LCD response, high character set, full keyboard protocol and several
  board-specific functions remain incomplete.
- The PCS 86 portable engine has a focused acceptance profile, not full
  parity with the inherited emulator. Test the shipped default and the
  strict reference profile separately.
- Machine profiles are not yet saved as `*.blumach.json`; the launcher binds
  chosen local files for the current session only. Do not treat this build
  as a migration tool or stable VM format.
- The packaged launcher is still subject to interactive testing, especially
  GUI lifecycle and real-time pacing on host systems other than this build
  machine.
