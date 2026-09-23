# PCS286: independent real-mode hardware comparison

<!-- SPDX-License-Identifier: GPL-2.0-or-later -->

This is a development-only comparator, not a new CPU, firmware loader or timing
model. It calls the public 286 state/step contracts. No production target links
the parser or probe, and ordinary builds never download external data.

## Source and provenance

[SingleStepTests/80286](https://github.com/SingleStepTests/80286), Daniel Balsom,
MIT, immutable commit `37c73caf53dcd22d3dd369ff09305d13d117a4fe` (README release
1.1.0). Captured CPU: Harris N80C286-12 (L4252050), not every Intel stepping.
The upstream metadata.json still calls itself 1.0.0; the commit and SHA-256
lock, not that stale label, identify the inputs. The changelog is not used as
a revocation list: the actual pinned revocation_list.txt controls exclusions.

`tests/data/sst286-lock.json` records exact files and SHA-256, including source
license, metadata, revocations and the consulted converter. The external gzip
vectors remain outside Git. `tools/sst286.py` is a **derived rewrite** of the
decoding approach consulted in upstream `tools/moo2json.py` at that commit,
with explicit bounds/count checks and a new comparison/transport layer. Its
MIT notice retains Daniel Balsom and adds BluMach contributors. The test-only
C probe and miniature self-test records are authored for BluMach, GPL-2.0-or-later.
This does not make any new claim about the CPU core's existing provenance.

## Reproduce

Build engine-only tests as usual. The resulting test executable is
`tests/components/pcs286/pcs286-sst-probe` (with `.exe` on Windows and a
Debug/Release subdirectory for multi-configuration generators).

Obtain the exact upstream commit in a separate directory. Disable Git newline
conversion (`core.autocrlf=false`) for that checkout: SHA-256 checks the original
bytes, not normalized text. For a minimal download use Git's blob filtering
and sparse checkout for the paths in the lock's `sha256` object. Alternatively
download those paths from `raw.githubusercontent.com/SingleStepTests/80286/`
followed by the pinned commit and path, retaining directory layout. Do not
run upstream scripts; the BluMach comparator parses the data offline.

```text
python tools/sst286.py --root <external-checkout> --probe <test-executable> --report <report.json>
```

Optional CMake cache `BLUMACH_SST286_DATA_DIR=<external-checkout>` registers
`pcs286-component.sst-functional-subset`. Missing or changed inputs fail; no
fallback download or moving branch is used. By default only the adapter's
authored self-tests run in CTest, including a real probe round trip. Without
Python that adapter gate is explicitly skipped, not reported as successful.

The CLI returns 1 for discrepancies and 2 for transport/evidence errors;
77 means no supported case was compared successfully. Exit 0 means the
selected supported subset matched, **not** complete CPU conformance. Add
`--require-complete` to fail on any pending or unselected case as well.
`--limit N` is only a development sample and counts its remaining cases as
not_selected. Reports contain input, adapter and executable SHA-256 hashes,
per-file totals and identities/reasons for every pending/revoked/failed case.

## Comparison boundary

- Explicit initial segment selectors become ordinary real-mode hidden caches;
  this is not the high reset-vector cache. RAM addresses remain 24-bit, without
  a motherboard A20 mask. Reads of unspecified RAM are evidence/transport
  errors, not silently fabricated zeroes.
- Run one instruction. Subtract the capture's terminating HLT from expected
  IP, wrapping at 16 bits. No HLT/NMI collection sequence is emulated or
  certified, and the core is not changed to accommodate this transport.
- Merge sparse final registers/RAM over initial state. Compare every register
  and all reported memory changes, including unrequested writes.
- Use only pinned upstream register/flags masks for undefined state. Do not
  discard a discrepancy by inventing new masks or editing vectors.
- Cases declaring guest exceptions and unimplemented LOCK/REP prefixes are
  counted as pending before execution. For a selected supported instruction,
  an unexpected UNSUPPORTED return is a discrepancy, not another exclusion.
- Record revocations separately. They are not passing cases. CPU timing,
  physical bus activity, protected mode, interrupt acceptance and the PCS286
  chipset/board are outside this comparison. Existing synthetic tests remain.

## Initial selection and observed results, 2026-09-23

Eleven files: 00/01 ADD, 31 XOR, 54 PUSH SP, 60/61 PUSHA/POPA, 8B MOV,
90 NOP, C8/C9 ENTER/LEAVE, E8 near CALL. This is a deliberately small first
integration, **not** the full upstream corpus or all implemented instructions.

Across 51,000 captured cases: **49,076 matched, zero discrepancies, 1,923
pending and one revoked**. These counts do not establish cycle accuracy or
hardware equivalence for untested states. Expand the pinned selection and
its explicit eligibility rules as CPU coverage grows.

Sources: upstream README, LICENSE, metadata, revocations and CHANGELOG at the
pinned commit; [MOO format specification](https://github.com/dbalsom/moo/blob/main/doc/moo_format_v1.md).
