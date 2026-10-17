# Shared file UI controls

Files composes shared breadcrumbs, detailed columns, preview frames, metadata and
labeled hints while retaining its navigation and preview coordination. Breadcrumb
indices resolve through BreadcrumbAdapter and enter open() only when the existing
sidebar interaction guard allows navigation. History, prompt, mode and Quick Look
ownership remain in DirectoryController.

Detailed rows retain filename rendering, search runs, symlink style, external hybrid
line-number gutter and overlay editor. Shared column sizing governs the row/header.
Preview framing contains existing thumbnail transition rendering. Quick Look retains
its pinned service identity, image/text/Markdown presentation and focus return.
Metadata values retain application formatting. Native interaction acceptance covers
Vim editing, history, line numbers, narrow layouts and Quick Look keyboard return.

Verification results and compatible provider revisions are recorded in the umbrella
[initiative ledger](../../../../docs/initiatives/shared-file-ui-controls/TASKS.md).
