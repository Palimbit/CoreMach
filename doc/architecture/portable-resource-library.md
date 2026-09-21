# Local resource discovery for BluMach Portable (proposal)

Status: design only. No automatic scanning or binding is implemented in
`0.0.1-dev.2`. Firmware and guest media remain user-supplied and never enter
the BluMach repository or release package.

## User outcome

The user chooses one local **resource folder** in BluMach settings. While
creating a machine, BluMach scans that folder and fills compatible resources
before showing a single creation form. If a required resource is missing, the
form says precisely which role, size and known identity are expected, offers
**Open expected folder** and **Choose file**, and never pretends the machine is
ready. An optional disk remains visible and changeable even when preselected.

The resource folder is not a ROM pack or a new machine-configuration format.
Machine profiles continue to save references to local paths, not bytes.

## Scope and ownership

- Keep the engine and machine definitions unaware of host directories, Qt,
  archives, path separators and operating-system file dialogs.
- The existing frontend asset requirements own roles, mandatory/optional
  status, sizes and access modes. Add a small versioned **recognition table**
  beside those descriptors for known SHA-256 identities, display names and a
  preferred logical subdirectory. It contains metadata, never firmware bytes.
- The desktop application owns folder selection, bounded scanning, hashing,
  UI state and the remembered root. Headless continues to accept explicit
  bindings; a later `--resources-dir` can reuse the recognition metadata.
- A saved machine's explicit asset choice wins. If that path is missing or its
  content changed, show an error and ask the user; do not silently substitute
  another disk or firmware. For a newly created machine, a unique exact hash
  match may fill the slot automatically.
- Resource discovery must not depend on the legacy emulator's ROM directory,
  its filenames or its configuration format.

## Recognition rules for the first two machines

| Machine | Role | Recognition | Initial action |
| --- | --- | --- | --- |
| PCS 86 | `firmware-even` | Known 32 KiB EPROM SHA-256 | Fill on exact match; required |
| PCS 86 | `firmware-odd` | Known 32 KiB EPROM SHA-256 | Fill on exact match; required |
| PCS 86 | `floppy-0` | Known 720 KiB system disk | Suggest as visible default; optional |
| PCS 86 | `hard-disk-0` | 21,411,840-byte XTA *working image* | Never auto-attach; explicit choice only |
| M15 | `firmware` | Known 64 KiB BIOS SHA-256 | Fill on exact match; required |
| M15 | `floppy-0` | Known 720 KiB tutorial disk | Suggest as visible default; optional |

Additional known PCS 86 Customer and Tutorial disks should appear as
alternatives, not be mistaken for the system disk. Filenames alone, size alone
and a generic 86Box ROM set are insufficient to establish identity. A
non-matching but plausibly sized manual input may be offered as an explicitly
unverified advanced choice; it must never be silently auto-selected. Firmware
identity checks at the UI boundary should not be confused with full emulator
compatibility validation.

The PCS 86's expected 1.09 EPROM hashes are already exposed by
`bm_pcs86_expected_firmware()`. The current Qt file input reads by size and
passes no SHA-256 into the blob binding, so the proposed scan must actually
hash candidates before labeling them recognized. The M15's known BIOS and
disk hashes currently live in the canonical local machine record; publishing
hash metadata does not publish their bytes or grant redistribution rights.

## Folder layout and missing-resource experience

The first test folder uses this human-readable convention:

```text
resources/
  olivetti/
    pcs86/
      firmware/
      floppies/
      hard-disks/working/
    m15/
      firmware/
      floppies/
```

Recognition is content-based, so users may keep other filenames or nested
folders. The displayed path is a suggestion, not a parser contract. BluMach
may create **empty directories and an explanatory README** after the user
chooses a root, but must never download, unpack, copy or redistribute ROMs on
its own. If the root is absent or inaccessible, show **Choose resource folder**.
If a specific role is missing, show its logical folder and an **Open folder**
button; create that empty directory only after the user chooses the root.
Do not open an arbitrary executable file when the user expects a directory.

Example missing-resource copy:

> PCS 86 needs the even 32 KiB BIOS EPROM. Put your own compatible file in
> `olivetti/pcs86/firmware/`, then rescan, or choose the file directly.
> BluMach does not supply this BIOS.

On desktop, Qt provides `QFileDialog::getExistingDirectory()` for selecting the
root, `QDesktopServices::openUrl(QUrl::fromLocalFile(folder))` for opening a
directory, and `QCryptographicHash` for SHA-256. The indexer should scan in a
worker with progress/cancel, skip symlinks and archives initially, impose a
bounded file count/size, and not recursively traverse the user's whole drive.
Rescan on demand or when creating a machine; a filesystem watcher is not
required for the first iteration. Any hash cache is only a speed aid and must
be invalidated by path, size and modification time; verify the selected BIOS
again before launch. Writable hard disks need explicit working-copy semantics,
not content hashes after use.

## Interface sequence

1. Settings: choose or change the resource root, with an explicit scan button.
2. Scan: classify exact matches as `recognized`, same-size candidates as
   `unverified`, and missing roles as `missing`. Report duplicates and errors.
3. Creation form: show the selected resources and their origin/identity beside
   machine-specific choices. Required exact matches are filled; optional
   recommended disks are suggested, not hidden. No auto-attachment of writable
   media.
4. Save a machine: keep a resource identity and its resolved local path, so a
   moved file can be relocated without guessing. When re-opening, an explicit
   choice remains authoritative. No migration from 86Box VM files is implied.
5. Launch: validate required files, sizes and recognized firmware identities
   before handing blobs or streams to the frontend. The engine still receives
   only typed inputs, not the resource folder.

## Delivery slices and acceptance

1. Metadata and resolver tests: known hashes, wrong-half EPROM, renamed file,
   duplicate identical file, same-size wrong file, missing root, symlink and
   cross-volume paths.
2. Qt root setting and scan/report UI: no change to machine creation yet.
3. Prefill and missing-resource guidance in the existing single modal form.
4. Saved-profile relocation and end-to-end tests with PCS 86 and M15; ensure
   firmware/disks are absent from Git and release archives.

Keep the local test folder outside Git. Its M15 tutorial image has unknown
distribution rights; the feature must work without shipping that image.

## Useful precedents, not dependencies

- [MAME software lists](https://docs.mamedev.org/contributing/softlist.html)
  separate identified media from the emulated machine and retain hashes and
  provenance. Its [asset-search behavior](https://docs.mamedev.org/usingmame/assetsearch.html)
  distinguishes missing system ROMs from optional software media. BluMach
  should adopt the clarity of those diagnostics without importing MAME's ROM
  directory rules or archive format.
- [Libretro's core documentation pattern](https://docs.libretro.com/meta/core-template/)
  states required and optional firmware in a frontend-owned system directory.
  BluMach should likewise keep folder selection in the application layer, not
  in CPU or machine timing code.
- Qt 6 offers the necessary desktop primitives:
  [folder selection](https://doc.qt.io/qt-6/qfiledialog.html),
  [opening a local directory](https://doc.qt.io/qt-6/qdesktopservices.html) and
  [SHA-256 hashing](https://doc.qt.io/qt-6/qcryptographichash.html).
