# Drag and drop

Files exports local file URLs and accepts local file transfers in the listing viewport.
The sidebar, breadcrumb and preview are outside this contract. No shared provider API changes
are required. The clipboard register remains independent.

## Interaction and interfaces

A left-button DragHandler uses Qt's standard threshold and takes the pointer grab away from
row activation. A stable listing-owned proxy snapshots an encoded text/uri-list before
starting Qt's native drag. A selected Visual row exports the full selection; otherwise only
the source row is exported. Parent rows and editing placeholders are excluded. Finishing an
outgoing move never deletes sources: the receiving application performs the transfer.

DirectoryController exposes source collection, row destination resolution and submission.
The private FileTransfer helper validates whole batches, deduplicates paths, chooses actions
and queues existing TaskManager operations. FileDropArea receives native Qt events and
publishes position, hover state and action. QML maps viewport coordinates to listing rows.
Folders (including the parent row) receive transfers; files and blank space target the current
directory. Destinations are snapshotted when queued. TaskManager owns conflicts, cancellation,
errors and refresh notification; worker safeguards and symlink policy remain authoritative.

Copy is the default and Ctrl requests copy. Shift requests move. Ctrl+Shift, Alt/link and
unsupported requested actions are rejected. Empty, malformed, remote and mixed unsupported
payloads are rejected as a batch. Enter events remain tracked for rejection feedback and later
modifier changes; rejected drop events never enqueue work.

Normal and Visual modes allow transfers. Insert, Search, Quick Look, the finder popup, task
prompts, directory errors and shutdown block transfers. Background tasks may queue transfers.
Feedback clears on leave, drop and disabling. Edge hovering within 32 logical pixels scrolls
one compact row every 75 ms, clamps to the list extent and recomputes the target without
opening directories.

## Acceptance

Automated checks cover URL encoding, selection/exclusion, strict batches and action policy,
interaction guards, native event cleanup and real worker transfers. Existing task regressions
cover conflicts, cancellation, stale sources and self/descendant protection. Offscreen window
checks cover hit testing and edge scrolling. Run focused regressions, QML lint/import/metadata,
container acceptance and isolated runtime acceptance before publication.

Manual Wayland acceptance is required: drag both ways with another file manager and a
file-accepting application; check multiple items, Shift/Ctrl, cancellation, within-window and
between-window transfers, folder targeting and edge scrolling. Do not automate native pointer,
focus or window interaction.
