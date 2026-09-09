# SDD Tasks — inspect-selection

- [x] T-001: Add `mode` field to DirectoryEntry for permission rendering
  - REQs: REQ-F-002, REQ-F-024
  - Check: DirectoryEntry gains `quint32 mode = 0;` field, populated by `readEntry()` via existing `::stat()` call; ModeRole added to enum and roleNames(); existing DirectoryModel tests pass unchanged.

- [x] T-002: Define PreviewError and PreviewErrorKind enum
  - REQs: REQ-F-016, REQ-F-023, REQ-F-024
  - Check: PreviewError plain struct (PreviewErrorKind kind, QString message) and PreviewErrorKind enum (None, PermissionDenied, BrokenSymlink, DecodeFailed, DecodeTimeout, Unsupported) exist in preview_service.h; PreviewErrorKind is Q_ENUM-registered on PreviewService for QML access.

- [x] T-003: DirectoryController integration with PreviewService ownership and state
  - REQs: REQ-F-001, REQ-F-002, REQ-F-010, REQ-F-013, REQ-F-014
  - Check: DirectoryController owns PreviewService member and exposes `preview` property (CONSTANT); adds `quickLookOpen` property (read-only, notifies changed); adds private slot `syncPreviewTarget()` connected to `changed()` signal; slot guards against redundant dispatch by comparing resolved path.

- [x] T-004: TextPreviewService namespace with binary detection and head-read
  - REQs: REQ-F-007, REQ-F-008, REQ-F-022, REQ-NF-003
  - Check: TextPreviewService::looksBinary(QByteArrayView sample) returns true for NUL byte or >50% non-printable in sniff; TextPreviewService::readHead(path, maxBytes) returns TextPreviewResult (content, totalSize, wasTruncated); text files under 64 KB are fully read, larger files read first 64 KB with truncation flag set.

- [x] T-005: ExifReader namespace wrapping libexif C API
  - REQs: REQ-F-006, REQ-C-001
  - Check: ExifReader::read(path, mimeType) returns ExifSummary with make/model/exposureTime/iso/focalLength fields; missing EXIF or tags populate as empty strings (no error); corrupted EXIF blocks are suppressed via `exif_data_set_log(nullptr)` call; PNG eXif chunk extractor scans chunk stream and feeds raw payload to libexif.

- [x] T-006: ThumbnailService cache key derivation, validation, and directory creation
  - REQs: REQ-F-017, REQ-F-018, REQ-F-020
  - Check: ThumbnailService::lookupOrDecode() computes cache key as lowercase hex MD5 of `QUrl::fromLocalFile(path).toString(QUrl::FullyEncoded)`; validates cached thumbnail's Thumb::MTime and Thumb::Size against fresh QFileInfo; creates `$XDG_CACHE_HOME/thumbnails/normal/` with 0700 permissions if missing; returns cached image if valid, nullptr if not.

- [x] T-007: ThumbnailService two-tier decode (cache thumbnail + full-resolution)
  - REQs: REQ-F-003, REQ-F-005, REQ-F-019, REQ-F-021, REQ-F-023, REQ-NF-003
  - Check: ThumbnailService decodes 128 px "normal" tier with `QImageReader::setScaledSize()` before `read()` (aspect ratio preserved, no distortion); encodes to PNG with Thumb::URI/Thumb::MTime/Thumb::Size tEXt chunks via `QSaveFile` (atomic, 0600 mode); provides separate `decodeScaled(path, effectiveSize)` for full-resolution tier; QImageReader errors are captured to QString for PreviewError delivery.

- [x] T-008: PreviewService worker thread, generation counter, and target setting
  - REQs: REQ-F-001, REQ-F-002, REQ-F-014, REQ-NF-001, REQ-NF-002
  - Check: PreviewService owns persistent QThread and worker QObject; `setTarget()` synchronously formats generic metadata on UI thread and emits changed() before dispatching; increments generation_ and replaces cancellation_ token; guards against redundant dispatch only when metadata snapshot and source revision are unchanged; handles empty/stat-failed targets via short-circuits (no worker dispatch needed).

- [x] T-009: PreviewService async dispatch, cancellation, and timeout
  - REQs: REQ-F-014, REQ-F-015, REQ-F-016, REQ-NF-001, REQ-NF-002
  - Check: `setTarget()` posts job carrying path, generation, cancel token, effectiveSize via QMetaObject::invokeMethod; worker checks cancel->load() before and between stages; starts single-shot 3000ms QTimer for REQ-F-016; applyResult() drops result if generation != current or timed_out_ flag is set (independent of generation check).

- [x] T-010: PreviewService worker job dispatch logic (MIME sniff, branching, result marshalling)
  - REQs: REQ-F-002, REQ-F-003, REQ-F-006, REQ-F-007, REQ-F-022, REQ-F-023
  - Check: Worker opens file, reads first 8 KiB; runs QMimeDatabase::mimeTypeForFileNameAndData once for both MIME and binary classification; branches: image/* calls ThumbnailService + ExifReader; non-binary text calls TextPreviewService; binary non-image gets nothing further (no error); posts PreviewResult via QMetaObject::invokeMethod.

- [x] T-011: PreviewPane.qml docked sidebar component
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009
  - Check: PreviewPane is third child of Main.qml's row layout, always visible; binds to controller.preview properties (name, size, modified, permissions, mimeType, image, exif*, textContent, previewErrorKind/Message); displays placeholder when hasEntry=false; shows truncation notice when textTruncated=true; responds to preview pane resize via consumer-specific setRequestedSize() call.

- [x] T-012: PreviewImageItem custom paint item for aspect-fit rendering
  - REQs: REQ-F-003, REQ-F-004, REQ-F-005
  - Check: PreviewImageItem extends QQuickPaintedItem; Q_PROPERTY(QImage image) with setter; paint() calls fitRect(image.size(), item bounds) to compute destination rect preserving aspect ratio, then paints image into rect; multiplies item logical size by Screen.devicePixelRatio for setRequestedSize() dispatch to worker; used by both PreviewPane and QuickLookOverlay.

- [x] T-013: QuickLookOverlay.qml Popup component with Space/Escape dismissal and shortcut veto
  - REQs: REQ-F-010, REQ-F-011, REQ-F-012, REQ-C-006
  - Check: QuickLookOverlay is Popup, sized ~92% of window, centered, visible: controller.quickLookOpen; binds to same controller.preview properties as pane; forwards j/k/Escape to handleKey(); declares Keys.onShortcutOverride to veto window.leaveFullscreen() when Escape and quickLookOpen both true (the "real Qt Quick gotcha" from DESIGN.md); responds to pane/overlay size changes via consumer-specific setRequestedSize() calls.

- [x] T-014: DirectoryController Quick Look state and keyboard handling
  - REQs: REQ-F-010, REQ-F-011, REQ-F-012, REQ-F-013
  - Check: DirectoryController::handleKey() adds branch: if key==" " and cursor_row_ in valid range, toggle quickLookOpen_ and emit changed(); if cursor_row_<0 or rowCount()==0, return true (consumed but no-op); if key=="Escape" and quickLookOpen_, close overlay and emit changed(); j/k keyboard handling unchanged (relies on existing syncPreviewTarget for live updates per REQ-F-011).

- [x] T-015: CMakeLists.txt updates for libexif dependency and Qt6::Svg support
  - REQs: REQ-C-001, REQ-F-021
  - Check: Root CMakeLists.txt calls `find_package(PkgConfig REQUIRED)` and `pkg_check_modules(EXIF REQUIRED IMPORTED_TARGET libexif)`; `find_package(Qt6)` includes Svg component; apps/files/CMakeLists.txt target_link_libraries adds PkgConfig::EXIF and Qt6::Svg; libexif .h include paths are correct; project builds without missing-dependency errors.

- [x] T-016: Create test fixtures for preview validation
  - REQs: REQ-F-003, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-021, REQ-F-022, REQ-F-023, REQ-F-024
  - Check: Fixture directory tests/fixtures/ contains: jpeg_with_exif.jpg (complete EXIF tags); jpeg_no_exif.jpg (EXIF stripped); png_with_exif.png (PNG eXif chunk); corrupt_jpeg.jpg (truncated mid-stream); small.txt (~10 KB UTF-8); large.txt (~200 KB with truncation marker); utf8_multilang.txt (emoji, CJK, Latin diacritics); random.bin or /bin/true (binary non-image); broken_link symlink; sample images in PNG, WebP, GIF, BMP, SVG, TIFF formats.

- [x] T-017: ThumbnailService unit tests (cache operations, format detection, MD5 key derivation)
  - REQs: REQ-F-017, REQ-F-018, REQ-F-019, REQ-F-020
  - Check: Test suite uses QTemporaryDir for fake $XDG_CACHE_HOME; verifies cache key matches freedesktop MD5-of-URI; validates mtime/size invalidation (modify source, cache is ignored); confirms 128 px bounding-box decode preserves aspect ratio; tests directory creation with correct permissions; passes on fresh system with no prior cache.

- [x] T-018: ExifReader unit tests (tag extraction, missing/corrupted EXIF handling)
  - REQs: REQ-F-006, REQ-F-023
  - Check: Test suite with jpeg_with_exif.jpg fixture confirms all five fields (make, model, exposureTime, iso, focalLength) are extracted correctly; jpeg_no_exif.jpg returns ExifSummary.present=false; corrupted-EXIF fixture returns partial fields without crash; PNG eXif extraction works via png_with_exif.png fixture; missing individual tags leave field empty, not error.

- [x] T-019: TextPreviewService unit tests (binary detection, head-read, truncation)
  - REQs: REQ-F-007, REQ-F-008, REQ-F-022
  - Check: looksBinary() returns true for random.bin and false for small.txt; readHead(small.txt, 65536) returns full content; readHead(large.txt, 65536) returns first 64 KB with wasTruncated=true and totalSize set; utf8_multilang.txt is not false-positive as binary; NUL byte in first 8 KiB is detected.

- [x] T-020: PreviewService unit tests (target setting, async dispatch, result handling)
  - REQs: REQ-F-001, REQ-F-014, REQ-F-015, REQ-F-016
  - Check: setTarget() on valid entry increments generation, updates properties synchronously, and dispatches worker job; rapid setTarget() calls (simulating j/k presses) cancel prior jobs; applyResult() drops results from prior generations; timeout fires at 3 seconds and sets DecodeTimeout error; subsequent result from same generation after timeout is not applied.

- [x] T-021: DirectoryController keyboard handler tests (Space, Escape, Quick Look state)
  - REQs: REQ-F-010, REQ-F-012, REQ-F-013
  - Check: handleKey(" ") toggles quickLookOpen when cursor_row_ valid; handleKey(" ") on empty directory (rowCount==0 or cursor_row_<0) is consumed but does not open overlay; handleKey("Escape") closes Quick Look if open, returns false if closed (letting window-level shortcut handle fullscreen); QML Keys.onShortcutOverride veto gate prevents window.leaveFullscreen() while overlay owns Escape.

- [x] T-022: Integration tests (full preview pane workflow, cursor move, decode, display)
  - REQs: REQ-F-001, REQ-F-003, REQ-F-007, REQ-F-009, REQ-F-010, REQ-F-011, REQ-NF-002, REQ-NF-004
  - Check: Load directory with mixed file types; cursorRow moves via j/k; preview pane updates within 100ms for cached entries, <500ms for uncached; image preview displays scaled correctly without distortion; text preview shows content and truncation notice for large files; Quick Look opens on Space, displays same preview as pane, updates live on j/k, closes on Space/Escape; startup latency <500ms for first preview.

- [x] T-023: Non-preemptible decode mitigation validation (QImageReader::setAllocationLimit + size check)
  - REQs: REQ-NF-001, REQ-NF-003
  - Check: QImageReader::setAllocationLimit() called process-wide to bound memory per decode; worker checks QImageReader::size() before calling read() and fails fast if allocation budget exceeded; pathological inputs (decompression bomb, >100 MB TIFF) timeout or fail within 3 seconds rather than blocking worker indefinitely; subsequent preview requests are not delayed.

- [x] T-024: Concurrent rapid-paging stress test with large-file fixture
  - REQs: REQ-F-015, REQ-NF-003
  - Check: Create fixture directory with 100+ large images (>5 MB each); simulate rapid j presses (10+ presses/second) through folder; verify preview pane shows only current entry's image (no cross-entry jumble); memory usage remains stable (<500 MB peak despite large queue); one outstanding-job discipline prevents queue buildup; Frame rate remains ≥30 FPS during rapid paging.

- [ ] T-025: Review regression coverage and delivery checks
  - REQs: REQ-C-001–006, REQ-F-001–024, REQ-NF-001–004
  - Check: DirectoryListing.qml has no thumbnail bindings (REQ-C-002); no multi-entry preview code (REQ-C-003); fullscreen window state toggle during session preserves preview pane proportion (REQ-C-005); code review confirms PreviewService mirrors DirectoryModel's thread-safety pattern (REQ-C-004); CMake dependency check, header formatting, build, tidy, QML lint, licensing, install; populated and error-state screenshots; functional tests in both locales as non-root user; watcher create/delete/rename during preview; symlink and permission-denied state rendering; cached vs uncached latency profiling on target system.

## Approved review remediation

- [x] R1 Keyboard routing and real-window external-open regression (REQ-R-001).
- [x] R2 Verified regular-file descriptor and read errors, FIFO child tests (REQ-R-002).
- [x] R3 Bounded EXIF extraction and malformed/large/cancellation tests, delegated to Spark, completed/reviewed locally after its usage limit (REQ-R-003).
- [x] R4 Progressive stages, sticky timeout and replaceable pending scheduling (REQ-R-004/005).
- [x] R5 Worker LRU, revision validation, consumer sizing and gradual resize tests (REQ-R-005/006).
- [x] R6 Selected-file watch refresh and metadata formatting (REQ-R-007/008).
- [x] R7 Required build/quality checks and native acceptance evidence recorded in VERIFICATION.md.
- [ ] R8 Live cross-monitor DPR migration acceptance (REQ-R-006); static 1×/1.5× native runs pass, but no live monitor migration was performed.
