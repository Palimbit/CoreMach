# Catalogue brand marks: initial asset intake

This is a source inventory, not an asset bundle.  BluMach's default catalogue
does not contain, download, or reference third-party logos.  Historical marks
are loaded only from a user-selected local visual package.

The collection uses a brand mark only on a manufacturer row.  A family mark is
an optional, separate asset and must have contemporary evidence.  Product rows
use BluMach's form-factor icon (desktop, laptop, portable, or all-in-one), not
a repeated brand or family badge.

## Selection rules

1. Prefer an SVG whose stated period overlaps the catalogue product dates.
2. Preserve the original source URL, author, licence assertion and file hash
   before using it.  A Commons file page is a lead and provenance record, not
   blanket clearance.
3. `PD-textlogo` addresses copyright in the vector/text expression; it does
   not waive trademark rights.  The application may identify the historical
   manufacturer, but must not suggest sponsorship or affiliation.
4. Do not derive family logos from product names.  In particular, `PCS`,
   `T-series`, `Dario`, and `SX386` remain text labels until a documented
   period family mark is found.
5. Treat a logo whose stated period does not overlap the machines as rejected,
   even if its visual design is attractive.

## Candidates

| Manufacturer | Candidate | Period and catalogue fit | Reported rights / provenance | Decision |
| --- | --- | --- | --- | --- |
| Olivetti | [Olivetti logo (1971–2009).svg](https://commons.wikimedia.org/wiki/File:Olivetti_logo_(1971-2009).svg) | Exact overlap with the catalogue's 1986–1993 M, PCS and Prodest products. | Commons identifies Olivetti S.p.A. as author and `olivetti.it` as source; it declares `PD-textlogo` and warns that trademark rights remain. | Accepted on 2026-09-10 for the optional local package, after shared-record intake. |
| Amstrad | [Amstrad logo 1980s.svg](https://commons.wikimedia.org/wiki/File:Amstrad_logo_1980s.svg) | Exact era for PC1512 (1986) and PC5286 (1991). | The description calls it an 1980s Amstrad plc logo and marks it `PD-textlogo` plus trademark. | Accepted on 2026-09-10 for the optional local package, after shared-record intake. |
| Toshiba | [Toshiba logo.svg](https://commons.wikimedia.org/wiki/File:Toshiba_logo.svg) | The red wordmark matches the T-series era (1986–1991). Toshiba's own history places a replacement wordmark shortly after the 1984 company rename, and its 1987 T3200 advertising uses the red wordmark. | Commons identifies Toshiba Corporation, `PD-textlogo` and trademark status. [Toshiba's corporate chronology](https://www.global.toshiba/ww/outline/corporate/history/chronology.html) independently establishes the 1984 rename and subsequent new logo. | Candidate ready for the same intake review as Olivetti and Amstrad. |
| Triumph-Adler | [Triumph-Adler logo.svg](https://commons.wikimedia.org/wiki/File:Triumph-Adler_logo.svg) | The catalogue Dario products are 1990–1992. A historical source describes the 1980 corporate design as a slanted orange `TA`, contemporary with the Dario period, but the Commons asset itself has no usable date range. | The page is a conversion of a Triumph-Adler PDF and labels the mark `PD-textlogo`; [the historic-computing source](https://www.horniger.de/computer/ta/index_d.html) still needs corroboration from a dated corporate asset. | Hold: period/design is plausible, but not yet strong enough to distribute. |
| TriGem | [TriGem logo.svg](https://commons.wikimedia.org/wiki/File:TriGem_logo.svg) | The only vector candidate is stated as 2000; SX386M is a 1991 product. | It is a vectorisation traced from a 2002 annual report and labels the mark `PD-textlogo` plus trademark. | Do not use: period mismatch.  Locate a 1990–1992 primary source. |

## Controlled intake for an accepted candidate

When a candidate has passed the above review, acquire it once into
`library/_inbox/<YYYY-MM-DD>-<source>/`, recording the direct download URL,
access date, byte size and SHA-256.  Then assign it to the manufacturer's
shared record with a stable asset ID, period, author/source, licence assertion,
attribution, trademark note and distribution status.  Only an asset with a
confirmed permitted distribution status may be considered for a separately
distributed package.  It is never copied into the Git worktree as a runtime
resource.

The implementation remains complete without a visual package: a manufacturer
name is always the fallback, and the existing form-factor icons remain the
model-level visual language.

## Runtime derivatives

The UCRT64 Qt6 image loader used by the local build has no SVG image plugin, so
a visual package accepts raster PNG, JPEG and WebP only.  The initial package
uses lossless PNG renders at a height of 224 px, generated with
`rsvg-convert --height 224 --keep-aspect-ratio`.  The delegate scales them at
the display's physical pixel ratio before painting, avoiding a second high-DPI
enlargement.  Their immutable SVG originals and the recipe are recorded in each
manufacturer's shared record.

## Local package contract

Select a folder containing a `skin.json` from **Preferences → Collection
appearance**.  Version 1 uses the following small manifest shape:

```json
{
  "schema": "blumach-catalog-skin-v1",
  "id": "historical-brand-marks",
  "name": "Historical brand marks",
  "manufacturer_marks": {
    "olivetti": { "asset": "brands/olivetti-1971-2009-h224.png" }
  }
}
```

`id` is lowercase ASCII with hyphens.  Asset paths are relative to the package
folder.  The loader rejects absolute or parent paths, files escaping through a
symbolic link, files larger than 2 MiB, unsupported formats, unreadable images,
and images above 4096 pixels in either dimension.  A malformed mark is ignored;
a malformed manifest leaves the neutral catalogue intact.  `background` is an
optional colour for a mark that needs contrast.

For later expansion, the manifest is intentionally component-oriented: today
it loads `manufacturer_marks`; family marks and other visual components need
their own documented evidence and a future schema addition.  A local
reconstruction made from an owner-provided or uncertain source is `local-only`;
it must not enter the Git worktree or a public package without explicit rights.
