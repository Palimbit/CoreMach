# CoreMach extraction record

Source repository: https://github.com/BluMach/BluMach

Source branch: `architecture/portable-engine`.
Immutable source commit: `a71e403189bd516c0f25c19014ee9748b2793d0d`,
the merge of PR211. Its tested feature head is
`3c69d6708d4e297d1b836382cb5343080d788f13`; all 17 source CI checks passed.

Destination: https://github.com/Palimbit/CoreMach (initially private).

## Method

The initial extraction was a direct descendant of the source merge commit.
Review subsequently found a historical MDA character ROM and binary build/state
artifacts in the inherited ancestors. A filtered candidate removes exactly the
eight paths in `provenance/history-filter.json`, only in the CoreMach copy.
`provenance/history-commit-map.txt` records the original and filtered commit IDs;
the component manifest keeps the original IDs and origin repository. Referenced
source path object IDs are verified unchanged. Retained authors, dates and
messages remain; artifact-only commits may be pruned. BluMach history is unchanged.
The owner explicitly authorized replacing published `main` on 2026-09-27.
Publication uses an explicit force-with-lease against the initial published
head, so concurrent changes cannot be silently overwritten.

The current tree retains `engine`, `runtime`, `components`, `systems`,
`platforms`, portable `apps`, engine/component tests, authored test data,
portable architecture documents and validation tools. Catalogue source,
permitted editorial illustrations and implementation sheets remain at their
existing paths for the optional launcher. Files beneath `src/qt/catalog` are
catalogue data, not the classic Qt frontend.

The new root CMake file builds only portable targets, defaults to C11/headless,
and enables C++17/Qt6 only when the launcher is requested. Package identity is
CoreMach; executable names and APIs retain their existing identifiers.
Classic CI, build files, platform packaging and implementation are absent from
the current tree. Production component and runtime files are copied byte for
byte from the source commit. This is an extraction, not a CPU rewrite.

The provenance manifest keeps all original immutable commits, paths, methods,
copyright holders and license declarations. GPL notices and authors remain.
Historical documentation is retained as evidence; its references to worktrees,
PRs and prior product milestones describe the source development history.
The current README and this record define the extracted product.

## Scope and future split

CoreMach owns the engine, CPU/device components, platform contracts and machine
composition. The optional Qt launcher is currently housed here to preserve a
working integrated test product. A later frontend repository can consume this
core once exported packages and API versioning have been established.
No stable ABI or complete Circle/MorphOS support is implied by extraction.
The PCS286 prototype uses provisional timing and incomplete hardware policies;
its remaining work is documented in the retained architecture records.

BluMach master and its classic release preparation are separate work. The
portable source branch remains recoverable in BluMach; no source cleanup is
performed there during this extraction.

## Validation

Rebuild both the engine-only and optional Qt6 configurations from this tree.
Run CTest, retained Python tests, catalogue validation, provenance audit and the
installed Windows package audit. Confirm all origin commits exist in a fresh
clone and compare portable source paths against the immutable extraction base.
No BIOS, ROM or guest media is required for these checks.

Local extraction validation on 2026-09-27:

- UCRT64/GCC engine-only Debug: 165 CTests passed, one known Headland skip.
- UCRT64/GCC Qt6 Release: 175 CTests passed, the same known skip.
- Python: 73 passed, three opt-in conformance skips, with authored boot-probe
  tests enabled. Without a probe path, 48 passed and 28 skipped.
- Catalogue: 32 products and five locales validated.
- Provenance: 70 components, 334 files, zero errors; all ten origin commits
  are reachable ancestors, so a full clone retains them.
- Windows install: 48 files, 80.7 MiB, package audit passed with a clean PATH,
  Qt platform plugin and required runtime DLLs; no firmware or guest media.
- Production engine/runtime/component/system/platform/frontend Git content
  has no differences from the immutable source commit.

Private-repository CI defaults to one Linux engine job if Actions is enabled.
Full Windows/macOS/Qt/package runs require manual full_matrix selection.
Artifact upload is separately opt-in and retained for one day. At extraction,
  Palimbit uses the Free organization plan and repository Actions is disabled;
this setting is retained. Local results do not claim new remote CI results.

History-cleaning candidate: inherited Git pack reduced from 262.16 MiB to
151.17 MiB before final metadata commit. The complete reachable path inventory
contains no .rom/.bin/.exe or guest-media paths after filtering. All 85 paths
referenced by the component provenance manifest have unchanged Git object IDs
compared with their original source commits. Original IDs stay in the manifest;
auditing and differential fixtures resolve their local equivalents through the
versioned map. Replacement of published main is authorized; the publication
result is recorded separately in the preservation worklog.

Candidate validation: all 85 origin path objects verified again against the
unfiltered source; no forbidden file extensions in the complete reachable
historical path log. IOC02/PIT/Headland differential tests: three passed.
Python validation: 50 passed, 28 optional/probe skips; two new mapping tests
verify local origin resolution and ambiguous-map rejection. Component audit:
70 components, 334 files, zero errors. Production files remain identical to
the extraction base, so the earlier full engine/Qt tests apply to their code.
The authorized force-update replaces only CoreMach's newly created branch;
expected published head is 65361d458ebbe2879613097bdb8a0f3584f1227a.
Removing reachable refs is not a guarantee of immediate server-side object
purging; GitHub may retain unreachable objects. Do not publish old ancestry
branches or tags when adopting this candidate.

Differential Headland, IOC02 and PIT tests export their original references
from the immutable ancestor commit into the build directory. Existing SHA-256
pins and extraction boundaries are unchanged; reference files are test-only.
CTest configurations therefore require Git and full history. An engine-only
source archive can be built with BUILD_TESTING=OFF. The obsolete Python test
for classic Qt's static-resource initialization is removed; the portable GUI's
catalogue loading remains covered by its CTest integration tests.
