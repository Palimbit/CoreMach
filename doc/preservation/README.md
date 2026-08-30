# Hardware preservation records

This directory records the evidence used by BluMach when implementing historical
machines. It is deliberately separate from emulator source code and from the
runtime ROM directory.

## Repository policy

- Do not commit BIOS, option ROM, keyboard-controller firmware, disk images,
  executable software, archives containing them, or proprietary binaries.
- Firmware may be identified by original filename, size, revision, date,
  SHA-256 digest, byte layout and provenance. A digest is identification data,
  not a redistribution of the copyrighted work.
- A scanned brochure or manual is committed only when its licence or public
  domain status clearly permits redistribution. Otherwise record the title,
  edition, language, holding institution, source URL and local verification
  hash without copying the file.
- Distinguish a proven OEM identity from a probable rebrand and from a visual
  resemblance. Shared cases or badges do not prove shared firmware.
- A machine that lacks required firmware may be documented, but it must not be
  presented as a working implementation.
- Local test media and software are outside the scope of this first catalogue.
  The reserved `software/` directory explains how a later catalogue can be
  added without mixing it with hardware records.

## Layout

| Directory | Purpose |
|---|---|
| `machines/` | One record per physical model or board revision. |
| `equivalences/` | OEM, rebadge and clone relationships, with confidence. |
| `publications/` | Advertising, brochures, manuals and technical literature. |
| `firmware/` | BIOS metadata, expected filenames, layouts and SHA-256 hashes. |
| `sources/` | Source register and evidence-quality rules. |
| `software/` | Reserved structure; no software catalogue yet. |
| `templates/` | Templates for adding records consistently. |

## Evidence levels

| Level | Meaning |
|---|---|
| `confirmed` | Primary documentation, matching board/ROM evidence, or byte-level comparison. |
| `strong` | Multiple independent contemporary or physical sources agree. |
| `probable` | Credible secondary evidence exists, but a primary link is missing. |
| `reported` | A collector report, advertisement or photograph has not been independently verified. |
| `rejected` | Available evidence contradicts the proposed identity. |

Implementation status and identity confidence are independent. A model can be
well documented but not emulated, or emulated using compatible firmware while
its exact commercial badge remains uncertain.
