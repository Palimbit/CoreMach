# BluMach catalog illustrations

Author and copyright: rtzor, 2026. Distributed under
`GPL-2.0-or-later` as part of BluMach.

These images are generated editorial recreations, not documentary photographs.
The UI identifies them as **Concept illustration** or **Board recreation**.

Generated with the built-in OpenAI image tool on 2026-08-31 and 2026-09-01 from
locally held research references. The reference images are not distributed with
BluMach.

## Shared art direction

Create a precise, semi-realistic museum-catalog illustration on a warm off-white
background with a subtle blueprint grid. Preserve the reference object's physical
design, proportions, major components and period materials. Use soft neutral studio
lighting, restrained color and a landscape 3:2 composition. Do not add readable
branding, invented text, watermarks, people or unrelated devices.

## Initial asset-specific prompts

- `olivetti-pcs86.jpg`: complete compact PCS 86 system, matching CRT and low
  horizontal chassis; preserve front vent and drive-bay arrangement.
- `olivetti-pcs286.jpg`: complete PCS 286 system with square CRT, keyboard and
  right-side floppy drive.
- `olivetti-pcs386sx.jpg`: PCS 386SX system unit only; preserve the long lower
  ventilation grille, blank bay and right-side 3.5-inch drive.
- `olivetti-pcs286s-board.jpg`: top-down TI/OLIMCU16 motherboard recreation;
  preserve board outline, major chip placement, memory bank, slots and connectors.

These initial and intermediate variants are retained in the local design archive
outside the source tree. The bundled final derivatives are 900 × 600 JPEG files at
quality 90–92 to keep the executable size reasonable.

## Branded revisions

The `*-branded.jpg` revisions were produced with the built-in OpenAI image editor
using each unbranded recreation as the edit target and a verified photograph of
the corresponding real machine as the badge reference. Only the recessed front
badge was changed:

- `olivetti-pcs86-branded.jpg`: exact text `olivetti PCS 86`, including the small
  boxed model number.
- `olivetti-pcs286-branded.jpg`: exact text `olivetti PCS 286`, including the small
  boxed model number.
- `olivetti-pcs386sx-branded.jpg`: exact text `olivetti PCS 386 SX`, including the
  pale blue-green model block.

The original unbranded recreations and the superseded PCS 86 and PCS 286 branded
revisions are retained in the local design archive. The PCS 386SX branded revision
remains bundled under its stable catalog resource name; the PCS 86 and PCS 286
aliases use the shared-family revisions below.

## Shared PCS family revisions

The `*-family-v2.jpg` revisions for PCS 86 and PCS 286 intentionally reuse the
approved PCS 386SX chassis, composition, lighting and blueprint background as a
common visual identity for the PCS family. They were produced in precise-object
edit mode with the built-in OpenAI image editor. The real-machine photographs
were used only as documentary references for the front badge:

- `olivetti-pcs86-family-v2.jpg`: replace only the front badge with exact text
  `olivetti PCS 86`, preserving the small boxed model-number treatment.
- `olivetti-pcs286-family-v2.jpg`: replace only the front badge with exact text
  `olivetti PCS 286`, preserving the small boxed model-number treatment.

Both prompts explicitly lock the PCS 386SX chassis, panel seams, blank central
bay, right floppy drive, lower grille, perspective, material, lighting, shadows,
background and crop. They also prohibit monitors, keyboards, cables, additional
drives, watermarks and any other text. The stable PCS 86 and PCS 286 resource
names now bundle the corrected v3 revisions described below.

### Badge correction (v3)

The `*-family-v3.jpg` revisions correct the small model-number suffix after
comparison with the documentary photographs. The `86` and `286` no longer have
a filled rectangular background. Each number is printed directly on the beige
badge and framed only by one short horizontal line above and another below, with
no vertical side lines. This was a precise-object edit over the v2 images; all
other visual elements were explicitly locked. The stable catalog aliases now
bundle the v3 revisions, while v2 is retained in the local design archive.

## Triumph-Adler Dario family

The `ta-dario*-family-v1.jpg` assets reuse the approved PCS 386SX chassis as the
shared family illustration and replace only its front badge. They were produced
with the built-in OpenAI image editor in precise-object-edit mode. A photographed
TA Dario 286 front panel was used as the identity reference: italic cobalt-blue
`TA`, fine horizontal speed lines and the rounded blue `Dario 286` wordmark.

- `ta-dario286-family-v1.jpg`: documented badge text `Dario 286`.
- `ta-dario286s-family-v1.jpg`: inferred family badge text `Dario 286S`; no
  model-specific front photograph was located, so this remains a concept
  illustration rather than direct documentary evidence.
- `ta-dario386sx-family-v1.jpg`: documented product name `Dario 386SX`, using the
  same observed Dario badge system.

Research references: the photographed Dario 286 listing on
https://www.ebay.de/itm/147497720176, the preserved Dario 286 catalog record at
https://victoriancollections.net.au/items/5ac6fbae21ea6b0ac0af74d5, the Dario
386SX museum record at https://oldcomputers.it/ and the contemporary 9 November
1990 product report at https://www.computerwoche.de/article/2760939/ta-pcs-aus-rein-deutscher-fertigung.html.
The reference photographs are not distributed with BluMach. All prompts lock the
chassis, drives, grille, perspective, lighting and background and permit changes
only inside the recessed front badge.
