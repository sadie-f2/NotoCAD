# Hershey fonts

Vendored, unmodified, from the classic Usenet Font Consortium distribution.

## Why these files are here

`CLAUDE.md` defers TEXT rendering until the font question is settled, and
`SF_todo.md` settles it: R12's stroke fonts — `romans`, `romand`, `italicc` — are
descended from the Hershey set, and a Hershey glyph is literally a list of
polylines, which is exactly what `Entity::draw()` already emits. Bundling Hershey
is not an approximation of what R12 did; it is a reimplementation of the same
thing from the same ancestry, and it keeps the core headless — no Qt fonts, so
DXF-written TEXT and screen TEXT come from one source.

## What maps to what

| File | Hershey name | R12 equivalent |
|---|---|---|
| `rowmans.jhf` | Roman Simplex | `romans` — the workhorse |
| `rowmand.jhf` | Roman Duplex | `romand` |
| `timesi.jhf` | Times Italic | `italicc` |

Each file holds 96 glyphs, one per line, covering ASCII 32–127 in order.

## Format

Per line: five characters of glyph number, three of vertex count, then coordinate
pairs. Each coordinate is a single character offset from `'R'` (ASCII 82). The
first pair is not a point — it is the left and right side bearing, which is where
character advance comes from. The pair `" R"` is a pen-up: start a new polyline.

## Licence — conditions that travel with the data

`HERSHEY-NOTICE.txt` is the original notice, verbatim and unmodified. **It must be
distributed with the font data**, which is why it lives here in the repository
rather than only in the built binary. Its terms:

> This distribution of the Hershey Fonts may be used by anyone for any purpose,
> commercial or otherwise, providing that:
>
> 1. The following acknowledgements must be distributed with the font data:
>    - The Hershey Fonts were originally created by Dr. A. V. Hershey while
>      working at the U. S. National Bureau of Standards.
>    - The format of the Font data in this distribution was originally created by
>      James Hurt, Cognition, Inc., 900 Technology Park Drive, Billerica, MA 01821
> 2. The font data in this distribution may be converted into any other format
>    *EXCEPT* the format distributed by the U.S. NTIS (which organization holds
>    the rights to the distribution and use of the font data in that particular
>    format).

Both conditions are satisfiable and are satisfied. Condition 1 is attribution,
which BSD-3 already does structurally — but note it attaches to *distributing the
data*, not to shipping a binary, so it travels with the source tree. Condition 2
concerns a format this project has no reason to emit; converting the data into
NotoCAD's own tables is explicitly permitted.

This is compatible with the project's BSD-3 licence and requires no compile-time
module boundary of the sort DWG and Qt need — nothing here is copyleft. See
[Fedora's licensing wiki](https://fedoraproject.org/wiki/Licensing:HersheyFontLicense),
which documents the same terms.

## Provenance

Retrieved 2026-07-29 from `kamalmostafa/hershey-fonts`, which redistributes the
original Usenet Font Consortium files unaltered. The `.jhf` files and
`HERSHEY-NOTICE.txt` here are byte-for-byte as received; do not edit them. Derived
tables belong in `src/core/`, generated from these.
