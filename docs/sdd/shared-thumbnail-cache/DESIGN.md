# Design

`ThumbnailService` creates a provider request from the already opened `QFile` metadata and the local source URI. The request carries Files' revision when available, required pixel size, selected tier and raster/SVG kind. The provider owns cache path selection, PNG validation, metadata, permissions and atomic writes. Files maps provider stage callbacks onto its existing cancellation seam and checks cancellation after lookup and store.

`PreviewService` keeps its image inspection, SVG resource validation, worker cache and decode flow. Raster cache lookup permits standard external entries without Files markers. Existing Files entries remain reusable when their metadata and dimensions validate; rejected entries are replaced by a later successful decode. The existing selected tier is passed explicitly so a lookup can reuse a larger tier and storage retains the requested tier.

`task deps` installs the sibling provider before configuring Files. The isolated runtime staging script includes the same installed provider.
