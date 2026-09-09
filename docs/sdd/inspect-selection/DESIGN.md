# Inspect-selection design

The approved remediation plan supersedes the earlier prototype design. One PreviewService owned by DirectoryController supplies metadata, image, five EXIF fields and text to the pane and Quick Look. Inspection remains single-entry; pane sizing remains session-only.

## Keyboard routing (REQ-R-001)

Normalize Space/Escape/Return/Enter by Qt key code in shared QML routing. Both listing and focused delegates consume Space before inherited button handling, including release and repeat. Shortcut overrides only accept. Popup handlers live on focused content and selectable text forwards to that content. Dismissal restores listing focus; fullscreen Escape remains available when popup is closed. Controller clears counts/chords on toggles and closes an empty selection.

## Verified source and metadata (REQ-R-002/003/007/008)

Model mode classifies special entries without opening them. Worker uses nonblocking close-on-exec open and fstat to accept regular descriptors only. All decoding uses this one seekable source; symlinks to regular files work. Helpers propagate read failures. EXIF scans JPEG APP1/PNG eXIf framing, seeks past payloads, caps payload at 1 MiB and records at 4,096, checks cancellation, then feeds only extracted EXIF to libexif. Malformed EXIF omits fields.

Controller supplies metadata snapshots plus a watcher revision; selected regular-file and directory watches refresh unchanged paths and reattach after replacement. Cache identity includes descriptor device/inode, size, nanosecond mtime/ctime. Metadata uses binary units and exact bytes, locale timestamps with ISO fallback, explicit unavailable values.

## Scheduling and caches (REQ-R-004/005)

One persistent QObject on one QThread executes one active job. UI retains one replaceable pending request; completion starts only the latest. Cancellation tokens stop superseded stages. Generation and timeout guards reject every stale result. Thumbnail intermediate delivery keeps busy/deadline active; final delivery supplies full image and EXIF. Timeout clears specialized content and cancels work while retaining metadata and notice. Shutdown cancels and drops pending work.

Worker-owned full decode LRU retains at most two entries and 64 MiB, keyed by descriptor identity/revision and requested dimensions; larger results display without retention. Disk cache stays freedesktop normal 128px, with identity validation in addition to URI/mtime/size. Qt allocation/header checks bound decode memory; read() cannot be hard-preempted.

## Consumer sizing (REQ-R-006)

setRequestedSize(PreviewConsumer, QSize) records Pane and QuickLook separately. Controller sets Quick Look active state. Views report actual image areas at completion, resize and DPR changes. A 150ms debounce compares aspect-fitted requested pixels with retained image or in-flight dispatched size, not the last resize event. Inactive reports cannot override active sizing.

## Verification

Focused helper/service tests cover bounded IO, special-file replacement, progressive/stale/timeout results, pending replacement, LRU, resize and refresh. Real QQuickWindow tests intercept file URLs and exercise delegate/text focus, fullscreen and focus restoration. Blocking-risk tests use child processes with hard timeouts. Native performance and rendering evidence lives under build/; unavailable acceptance remains explicitly pending in VERIFICATION.md.
