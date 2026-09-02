# BluMach catalogue creation profiles

BluMach uses one common creation dialog for every runnable historical-machine
sheet. The dialogue is data-driven: model-specific choices and defaults belong
in the product's `creation` object in `src/qt/catalog/catalog.json`, not in C++
conditionals keyed by product ID.

## Fallback behaviour

`creation` is optional. If a runnable catalogue product has no creation data,
BluMach still creates it with its emulator machine ID and the emulator's native
defaults. The dialogue tells the user that the sheet has no historical profile
yet and that the resulting machine can be configured afterwards.

Missing catalogue data must therefore never make an otherwise runnable machine
unavailable. Creation is blocked only when the product has no usable emulator
platform, required firmware is unavailable, or the selected choice is explicitly
marked `unavailable`.

## Schema

The optional `creation` object supports:

- `status`: overall evidence state (`validated`, `documented` or
  `experimental`).
- `directory_prefix`: stable prefix for the generated VM directory.
- `facts`: read-only rows containing `label_key` and `value_key` locale keys.
- `fields`: selectable rows. Each field has an `id`, `label_key`, default choice
  ID and a `choices` array.
- `choices`: each choice has an `id`, `label_key`, evidence `status` and zero or
  more configuration assignments in `set`.
- `configuration`: base INI sections expressed as section names and value maps.
- `generated_files`: sparse blank files created inside the VM directory after
  the profile is saved, for example a historically sized hard-disk image.
- `note_keys`: localized explanatory or limitation notes.

An assignment is an object with `section`, `key` and scalar `value`. Choice
assignments override base configuration values. BluMach always writes the
platform's emulator machine ID into `[Machine] machine`, so catalogue data does
not duplicate that mapping.

## Evidence rules

Creation choices should reflect documented sales configurations, documented
hardware options or configurations actually validated in BluMach. Their status
must state which kind of evidence exists. An inferred setting may be offered as
`experimental`; an unresolved option should be visible as `unavailable`, never
silently substituted with a convenient approximation.

The product sheet remains the user-facing source for history, specifications,
known limitations and sources. The creation profile only translates the subset
needed to instantiate the machine and its media.
