# Catalogue maintenance

The historical catalogue is validated with:

```sh
python3 -B tools/catalog_builder.py check
```

The audit runs automatically for pull requests that change catalogue data,
locale packs, their Qt resource declarations or the auditor itself.

## Enforced contract

- `catalog.json` uses the supported schema and contains unique manufacturer,
  family, platform, product, facet and filter-profile identifiers.
- Entity references, family ownership, statuses, facets and HTTP(S) source URLs
  are valid.
- Products without an emulator platform are limited to `research` and
  `not_bootable` records.
- Every translation reference used by the catalogue exists in `en.json`.
- Every locale has the same key set as English and preserves Qt placeholders
  such as `%1`, `%2` and `%n`.
- JSON object keys are unique. Duplicate keys are rejected instead of relying
  on a parser's last-value-wins behavior.
- Every locale pack is registered in the `/blumach/catalog` Qt resource group.

English remains the runtime fallback, but fallback is for unsupported locales,
not a substitute for incomplete checked-in translations.

## Source layout

Manufacturers, families and machines are independent bundles below
`src/qt/catalog/source`. Each bundle contains its metadata and one locale file
per supported language. Global facets and shared interface strings live in the
`common` bundle.

For example, a machine is maintained in:

```text
src/qt/catalog/source/machines/amstrad/amstrad-pc5286/
  machine.json
  locales/en.json
  locales/es.json
  locales/fr.json
  locales/it.json
  locales/pt.json
```

`tools/catalog_builder.py` combines these sources deterministically during
CMake configuration and whenever a shard changes. It produces `catalog.json`,
`locales/*.json` and a QRC file in the build directory. Their resource aliases
remain `:/blumach/catalog/catalog.json` and
`:/blumach/catalog/locales/<language>.json`, so the runtime loader is unchanged.

Generated monoliths are build artifacts and must not be committed.

## Adding or editing an entry

Keep each translation key in exactly one bundle. Cross-machine references may
consume another machine's key, but must not copy it. Every bundle must provide
the same local key set in `en`, `es`, `fr`, `it` and `pt`; the audit rejects a
translation accidentally placed under another entry.

The numeric `order` field controls catalogue presentation independently from
file names. Existing values use gaps of ten so entries can normally be inserted
without renumbering unrelated bundles. A runnable machine keeps its exclusive
platform metadata in the same `machine.json`, together with `platform_order`.

After changing any bundle, run the audit command above. A normal CMake build
then notices the shard change, regenerates the runtime packs and invokes
AUTORCC only when their content has changed.
