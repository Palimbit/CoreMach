# BluMach catalogue filter facets

The collection browser derives its advanced filters from `filter_facets` in
`src/qt/catalog/source/common/catalog.json`. Each definition supplies a stable
facet ID, a localized label key and the controlled values that may be assigned
to a product.
This makes future form factors, such as laptops and transportables, discoverable
before the corresponding historical sheet is added.

Each product may declare a `facets` object in its machine bundle. Its keys are
facet IDs and its values are arrays of stable value IDs. These are
characteristics common to the whole product identity, for example:

```json
"facets": {
  "form_factor": ["laptop"],
  "mobility": ["portable"],
  "cpu_generation": ["x86_386sx"],
  "video": ["vga"],
  "storage_interface": ["floppy", "ide"]
}
```

Facets are complementary, not alternatives: a graphics standard such as VGA or
EGA is independent from display technology (CRT, LCD or plasma) and display
colour (colour, monochrome or amber). Do not collapse them into one value.

## Commercial configurations

When a product was sold with materially different display, storage or other
filterable configurations, keep its common properties in `facets` and add one
`filter_profiles` entry per documented configuration. A profile supplies only
the values specific to that configuration:

```json
"filter_profiles": [
  {"id": "vga-plasma", "facets": {"video": ["vga"], "display_technology": ["plasma"]}},
  {"id": "ega-amber", "facets": {"video": ["ega"], "display_technology": ["plasma"], "display_color": ["amber"]}}
]
```

Facet filters combine with each other, the text search and the preservation
state filter. For a product with `filter_profiles`, all selected characteristics
must be available together in at least one documented profile; values from two
different variants must never be combined into a fictional configuration. The
same common classification is also visible in the product sheet. New facet
values require localized labels in every catalogue locale.

Classifications must be supported by the product's documented identity or
hardware record, and each profile by the documentation for that variant. Omit
an unknown property instead of deriving it from an emulator default or treating
a plausible relationship as a fact.
