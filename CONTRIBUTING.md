# Contributing to BluMach

BluMach welcomes complete, reviewable pull requests that improve historical
research, machine fidelity, validation, documentation or the user interface.

## Historical machines

A machine addition or correction should:

- identify the manufacturer, family and exact product variant;
- cite manufacturer material, contemporary publications, observed hardware or
  other relevant evidence;
- distinguish documented, observed, inferred and hypothetical claims;
- describe current approximations and unresolved questions;
- include a usable emulator configuration only when firmware and implementation
  evidence support it;
- keep the catalogue, generated configuration and Configure dialog consistent;
- update English and Spanish catalogue text when user-visible content changes;
  and
- include repeatable validation results for any claimed working behaviour.

Do not commit ROMs, operating systems, proprietary software, restricted
documents or reference photographs. A locally preserved asset is not necessarily
licensed for public redistribution.

## Code and interface changes

- Preserve existing source-file copyright and authorship notices.
- Follow the repository's formatting and established implementation patterns.
- Avoid product-specific GUI behaviour when the same result can be represented
  declaratively in the catalogue.
- Keep new interface text translatable.
- Test light and dark palettes where appearance changes.
- Do not introduce new compiler warnings.
- Submit finished functionality rather than asking maintainers to complete it.

## Pull requests

Write the pull request in English and include:

- a concise summary of the change;
- the evidence or design rationale;
- the exact configurations and platforms tested;
- known limitations or failed tests;
- confirmation that no restricted firmware or local-only material is included;
  and
- links to related documentation or prior work.

If a change also applies to unmodified 86Box, say so explicitly. Upstreamable
changes can then be evaluated separately without presenting BluMach as an
official upstream build.
