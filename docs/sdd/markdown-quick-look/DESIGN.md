# Design

Extend TextPreviewResult with decoded source before TextLines splitting. PreviewService exposes Markdown eligibility, source, base URL and authorized roots while retaining the existing worker and generation/revision guards. Markdown remains part of the capped text-loading pipeline and does not affect passive browsing.

QuickLookPresentationModel adds a Markdown kind and computes bounded vertical scroll targets from viewport/content/line metrics. The overlay creates MarkdownView inside a clipped vertical Flickable through an active-only Loader. It supplies public MarkdownStyle and MarkdownResourcePolicy objects; there is no link activation handler. The view and resources disappear on close. Refresh retains the previous rendered source and authorized roots while the worker reads. It captures actual scroll position at the start of each refresh, retains that target across intermediate layout changes, and clamps to the new viewport/content bounds. A later refresh therefore starts from the clamped position.

The manifest installs qmarkdown as a separate shared provider with examples/tests/benchmarks disabled. All Files application targets link QMarkdown::QMarkdown and use the installed import path. CI pins the approved revision; runtime staging installs the complete provider payload.
