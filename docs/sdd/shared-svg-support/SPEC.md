# Shared SVG support — holonight-files

Approved scope: user-supplied implementation plan, 2026-09-24. Work package SVG-003.
Baseline: `98936ee55e4e70fdf8236e7c5e8685dda1eac330`.

## Requirements

- When MIME information or the suffix identifies an SVG candidate, Files shall explicitly validate SVG content
  before generic raster decoding and without relying on the SVG image-reader plugin.
- Files shall use its verified descriptor and existing worker, a 10 MiB input limit, static self-contained resource
  policy and existing output-byte limits. Validation shall precede every thumbnail cache lookup.
- Preview state shall retain vector/image kind and document geometry separately from rendered pixels and skip EXIF.
- Required-size/dispatch/adequacy/resize logic shall support SVG enlargement with existing DPR and debounce behavior.
- Thumbnail storage shall reuse PNG tiers, atomic writes and revision checks with an SVG rendering-policy marker.
- Transparency and preview/Quick Look presentation shall be preserved; unsupported resources shall have a translated
  explanation and ordinary opening in Viewer shall remain available.

See [design](DESIGN.md) and [tasks](TASKS.md).
