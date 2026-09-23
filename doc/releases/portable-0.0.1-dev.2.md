# BluMach Portable 0.0.1-dev.2 — private local preview

This is a private test build, not a public release or a compatibility promise.
It packages the portable engine, headless tool and Qt 6 desktop frontend. It
does not import configurations from the inherited BluMach/86Box product.

## What changed since dev.1

- The launcher displays all 32 historical catalogue sheets, organized by
  manufacturer and family, with search, history, technical notes and sources.
  Only the PCS 86 and M15 are currently launchable in the portable engine;
  every other machine is visibly marked as pending.
- A machine can be named and saved locally. The launcher lists saved machines
  and lets the user reopen the configuration form to edit paths before starting.
- Saved configurations use `machine.blumach.json` with schema
  `blumach-machine-config-v1` in the user's application-data directory. Asset
  paths are stored relative to each machine directory where possible; files
  on another Windows drive require an absolute `file:` reference. Firmware and media
  **bytes are never copied into the configuration or package**. Moving the
  original files may invalidate a saved machine.
- Battery-backed state is scoped to each saved machine instead of being shared
  by all instances of the same model. Unsaved test sessions keep the existing
  model-level state behavior.

## Windows local test

1. Extract `BluMach-Portable-0.0.1-dev.2-Windows-*.zip` into a new folder.
2. Run `bin/BluMach-portable.exe` and explore the catalogue. Try searching for
   a machine that is not yet portable; its start button must remain disabled.
3. Choose PCS 86 or M15, enter a name and select local firmware. Optionally
   select a floppy image. For PCS 86 XTA, provide only a disposable writable
   *working copy* of any hard-disk image, never a preserved original.
4. Start, stop and reopen the saved machine. Confirm that the name and local
   paths reappear. If an asset has moved, choose its new location in the form.
5. Test POST, video, keyboard, media, pause, reset and close separately; a
   successful boot does not validate the full machine.

No BIOS, ROM, floppy or hard-disk image is distributed. The M15 still uses a
provisional 8088 model for its 80C88, and both portable machines remain
development targets. The JSON schema is introduced for local testing and may
change before a stable release; do not treat it as an import/migration format.
