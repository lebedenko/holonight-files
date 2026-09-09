# Stage 2: Inspect a Selection — SPEC.md

## Overview

Stage 2 implements single-entry inspection via a persistent docked preview pane and an optional Quick Look overlay. The preview pane displays file metadata (generic for all file types) and specialized previews for images and text files, with graceful fallback to metadata-only when preview is unavailable or unsupported.

This stage **does not introduce multi-selection** (that is Stage 3, VISUAL mode). It operates on the single `cursorRow` entry that is always implicitly "selected" in NORMAL mode.

---

## Functional Requirements

### Preview Pane (Docked Sidebar)

#### REQ-F-001: Preview Pane Visibility
**Ubiquitous:** The system shall display a preview pane docked to the right of the directory listing at all times. The preview pane shall update reactively whenever `cursorRow` changes, or when the cursor points to a different entry.

**Acceptance Criterion:** In any directory view, the preview pane is visible and occupies a fixed or proportional right-hand region; when `j` or `k` moves `cursorRow` to a new entry, the preview pane contents refresh within 100ms (perceived instant) to show the new entry's preview.

#### REQ-F-002: Generic Metadata (All Files)
**Ubiquitous:** The system shall display the following metadata for every file entry, regardless of type or whether a specialized preview is available:
- File name (basename)
- File size (human-readable: KiB, MiB, GiB, with exact byte count in parentheses)
- Last-modified timestamp (human-readable locale-aware format with full ISO timestamp as fallback)
- Permissions (Unix mode, e.g., `-rw-r--r--`, with symbolic rwx breakdown)
- MIME type (from freedesktop MIME database or libmagic inference)

**Acceptance Criterion:** Every file entry, including symlinks, directories, and special files, displays all five metadata fields in the preview pane. A fixture file's true byte count is shown; a time zone of UTC is used in the test environment; MIME type matches freedesktop standards (e.g., `image/jpeg`, `text/plain`, `application/x-executable`).

#### REQ-F-003: Image Preview
**Conditional:** Where the file is a recognized image format (JPEG, PNG, WebP, GIF, BMP, SVG, or other common formats the system detects), the system shall display a scaled image preview in the preview pane alongside the generic metadata.

**Acceptance Criterion:** A test JPEG and PNG file display a visible scaled image in the preview pane within 500ms of `cursorRow` landing on it; the image is scaled to fit the preview pane without distortion (aspect ratio preserved); a non-image file (e.g., .txt) shows no image preview.

#### REQ-F-004: Image Scaling and Aspect Ratio
**Event-driven:** When the preview pane is resized (e.g., user drags the divider, or window is resized), the system shall rescale the displayed image to fit the new available space while preserving the original aspect ratio.

**Acceptance Criterion:** A test image is displayed; window is resized horizontally and vertically; the image remains undistorted and fills the available preview area without letterbox gaps (when possible given aspect ratio mismatch).

#### REQ-F-005: High-DPI Image Rendering
**Conditional:** Where the system's display has a non-integer `devicePixelRatio` (e.g., 1.25x, 1.5x fractional scaling), the system shall request image decode at the scaled pixel dimensions to maintain rendering crispness (not upscale a smaller fixed-size decode).

**Acceptance Criterion:** On a test system with 1.5x scaling, an image preview does not appear blurry or pixelated when compared to a 1.0x baseline; pixel dimensions of the decoded image match the scaled rendering area (within ±1 pixel rounding).

#### REQ-F-006: EXIF Metadata for Images
**Conditional:** Where an image file contains EXIF metadata and the file is a JPEG or other format supporting embedded EXIF, the system shall extract and display:
- Camera make (e.g., "Canon")
- Camera model (e.g., "Canon EOS 5D Mark IV")
- Exposure time (e.g., "1/250s")
- ISO (e.g., "400")
- Focal length (e.g., "50mm")

These fields shall be displayed alongside generic metadata and the image preview, labeled and in human-readable format.

**Acceptance Criterion:** A test JPEG with EXIF data displays all five fields with correct values extracted from the file's EXIF tags (IFD0/IFD1 standard locations); a JPEG without EXIF data shows generic metadata only (no error, no empty EXIF section).

#### REQ-F-007: Text File Preview
**Conditional:** Where the file is detected as a text file (via magic byte sniff: no NUL bytes, low ratio of non-printable bytes in the first 8 KB), the system shall display the text content in the preview pane.

**Acceptance Criterion:** A `.txt` file displays its content up to 64 KB in a scrollable text view; a file larger than 64 KB shows only the first 64 KB with a "truncated — file is larger" notice; binary file (containing NUL byte or >50% non-printable bytes in first 8 KB) shows no text preview, only generic metadata.

#### REQ-F-008: Text File Size Limit Notice
**Event-driven:** When a text file is larger than 64 KB, the system shall display a visible notice in the preview pane indicating the file is truncated.

**Acceptance Criterion:** A 100 KB plain text file shows the first 64 KB of content and a clear notice (e.g., "Truncated — file is larger (100 KiB total)"); user can scroll within the 64 KB preview but not access the remainder from this view.

#### REQ-F-009: Empty or No-Entry Placeholder
**State-driven:** While the directory contains no entries, OR while `cursorRow == -1` (no entry is currently under the cursor), the system shall display an explicit placeholder message in the preview pane (e.g., "No files to preview" or "Empty directory").

**Acceptance Criterion:** An empty directory shows a placeholder instead of a blank/stale preview; after deleting all files in a directory, the placeholder appears; after using a filter that matches zero entries, the placeholder appears.

#### REQ-F-010: Quick Look Overlay (Space Trigger)
**Event-driven:** When the user presses `Space` while in NORMAL mode with a valid entry under the cursor, the system shall open a Quick Look overlay displaying the same preview content (image or text) as the preview pane, in a larger, centered, fullscreen or nearly-fullscreen window.

**Acceptance Criterion:** Pressing `Space` with `cursorRow` on a file opens an overlay; overlay displays the same image/text as the sidebar preview (using the same decoder, same cached thumbnail data); pressing `Space` or `Esc` closes the overlay and returns focus to the directory listing.

#### REQ-F-011: Quick Look Live Update on Cursor Movement
**State-driven:** While the Quick Look overlay is open and the user presses `j` or `k`, the system shall move `cursorRow` to the next/previous entry and live-update the overlay to show the newly-cursored file's preview, without closing the overlay.

**Acceptance Criterion:** Quick Look open on file A; user presses `j`; `cursorRow` increments; overlay displays preview of file B within 200ms; overlay remains open and no re-trigger via `Space` is needed.

#### REQ-F-012: Quick Look Dismissal
**Event-driven:** When the user presses `Space` or `Esc` while the Quick Look overlay is open, the system shall close the overlay and return keyboard focus to the directory listing in NORMAL mode.

**Acceptance Criterion:** Quick Look open; `Space` or `Esc` closes it; subsequent keypresses (e.g., `j`, `k`, `:`) are interpreted by the listing, not the overlay; no UI elements from the overlay remain visible.

#### REQ-F-013: Quick Look When No Valid Entry
**Conditional:** Where the cursor is on an invalid entry (e.g., `cursorRow == -1` or the entry no longer exists), and the user presses `Space`, the system shall NOT open the Quick Look overlay and shall remain in NORMAL mode.

**Acceptance Criterion:** `Space` in empty directory does nothing; `Space` with `cursorRow == -1` does nothing; no error message or overlay appears.

### Asynchronous Preview Decoding

#### REQ-F-014: Worker Thread Decoding
**Ubiquitous:** All preview decoding (image and text) shall be performed asynchronously on a dedicated worker thread, following the same pattern as `DirectoryModel`'s async file listing. The UI thread shall never block waiting for image decode or text read.

**Acceptance Criterion:** The UI remains responsive (frame rate ≥30 FPS) while a large image is being decoded in the preview pane; no "spinning wheel" or frozen window occurs during decode.

#### REQ-F-015: Cancellation of Stale Decodes
**Event-driven:** When `cursorRow` changes before a prior decode operation completes, the system shall cancel the stale decode operation (if cancellable) and discard its result, then start a new decode for the newly-cursored entry.

**Acceptance Criterion:** Rapidly press `j` to move through a folder of large images; preview pane does not show a jumbled mix of images from different entries; only the current entry's preview is displayed.

#### REQ-F-016: Decode Timeout and Fallback
**Unwanted Behaviour:** If image or text decode takes longer than 3 seconds, or fails with an error, the system shall cancel the operation and fall back to generic metadata only, displaying a brief "preview unavailable" message (e.g., "Cannot decode image") alongside the generic metadata.

**Acceptance Criterion:** A corrupt JPEG that triggers decode failure within 3 seconds shows generic metadata + "Cannot decode image" notice; user is not left waiting indefinitely; a large (>10 MB) valid image that decodes slowly still displays within 3 seconds or falls back gracefully.

### Thumbnail Caching

#### REQ-F-017: Freedesktop Thumbnail Cache Integration
**Ubiquitous:** The system shall use the freedesktop Thumbnail Managing Standard (version 0.8.3) to cache decoded image thumbnails in `$XDG_CACHE_HOME/thumbnails/` (or `~/.cache/thumbnails/` if `$XDG_CACHE_HOME` is unset).

**Acceptance Criterion:** After previewing an image, a thumbnail file exists in `$XDG_CACHE_HOME/thumbnails/normal/` (MD5-of-URI-keyed PNG file); running a secondary instance of the app on the same image reuses the cached thumbnail instead of decoding again (verified by profiling or by mtime check on the cache file).

#### REQ-F-018: Thumbnail Validation (mtime and Size)
**Event-driven:** When retrieving a cached thumbnail, the system shall validate the thumbnail's mtime and original file size against the source image file. If either has changed, the system shall discard the cache entry and perform a fresh decode.

**Acceptance Criterion:** A cached thumbnail is marked valid; the source image is modified (mtime updated); next preview request re-decodes the image and updates the cache; a test that modifies a file's mtime after caching confirms the cache is invalidated.

#### REQ-F-019: Thumbnail Size (Normal Variant)
**Constraint:** Thumbnails shall be cached and decoded at the "normal" size per the freedesktop standard — a 128×128 pixel bounding box with the source image's aspect ratio preserved (e.g., a 4000×3000 photo becomes 128×96, not a distorted 128×128 square) — not the "large" (256px) or other variants.

**Acceptance Criterion:** All cached thumbnail files in `$XDG_CACHE_HOME/thumbnails/normal/` are bounded to 128px on their longer side with aspect ratio preserved (no dimension exceeds 128px, and neither dimension is distorted relative to the source); scaling to the preview pane's actual size happens at render time, not at cache time.

#### REQ-F-020: Thumbnail Directory Creation
**Unwanted Behaviour:** If `$XDG_CACHE_HOME/thumbnails/normal/` does not exist, the system shall create it (with appropriate parent directories) before writing a thumbnail.

**Acceptance Criterion:** On a fresh system with no `~/.cache/thumbnails/`, the first image preview attempt creates the directory structure and writes a cached thumbnail; no error occurs; subsequent previews find and use the cache.

### Image Formats and Binary Detection

#### REQ-F-021: Supported Image Formats
**Ubiquitous:** The system shall recognize and attempt to preview the following image formats: JPEG, PNG, WebP, GIF, BMP, SVG, TIFF, and any others detected by the underlying image library (Qt's QImage or similar) as valid image data.

**Acceptance Criterion:** A test folder containing JPEG, PNG, WebP, GIF, BMP, and SVG files each displays an image preview (not metadata-only fallback); an unrecognized or unsupported format (e.g., `.heic` if not supported) falls back to generic metadata.

#### REQ-F-022: Binary File Detection
**Event-driven:** Before attempting to decode a file as text, the system shall sniff the first 8 KB of the file. If the sniff detects a NUL byte or a ratio of non-printable bytes >50%, the system shall classify the file as binary and skip text preview, showing only generic metadata.

**Acceptance Criterion:** A compiled executable (binary) shows generic metadata only, no garbled text preview; a `.txt` file with legitimate non-ASCII characters (e.g., UTF-8 encoded) is correctly previewed as text; a partially-corrupted JPEG (not detected as image format) that contains NUL bytes is not attempted as text.

#### REQ-F-023: Corrupt or Truncated Image Graceful Fallback
**Unwanted Behaviour:** If an image file is corrupted, truncated, or contains malformed EXIF data, the system shall not crash, hang, or display garbled content. Instead, the system shall fall back to generic metadata only, with a visible "Cannot decode image" or "Corrupt image" notice.

**Acceptance Criterion:** A truncated JPEG file (cut off mid-stream) shows metadata + error notice, no crash; a JPEG with a corrupted EXIF tag shows image preview (if decodable) with EXIF fields omitted (not an error, graceful omit); a zero-byte file shows metadata + notice.

#### REQ-F-024: Symlink and Permission-Denied Fallback
**Unwanted Behaviour:** If the target of a symlink is broken, or the file cannot be read due to permission denial, the system shall not crash or show a blank preview. Instead, the system shall display generic metadata (including the symlink target path or permission state) and a visible notice (e.g., "Permission denied" or "Broken symlink").

**Acceptance Criterion:** A broken symlink shows metadata + "Broken symlink: /path/to/nonexistent" notice; a file owned by another user with mode 000 shows metadata + "Permission denied" notice; `cursorRow` can still move past these entries without hanging.

---

## Non-Functional Requirements

#### REQ-NF-001: Async Decoding Non-Blocking
**Ubiquitous:** The system shall not block the main/UI thread while decoding images or reading text files for preview. All decoding operations shall be dispatched to a worker thread and results marshalled back to the UI thread via a thread-safe queue or signal/slot mechanism.

**Acceptance Criterion:** While a 50 MB image is being decoded, the directory listing remains responsive to keypresses (e.g., `j`, `k`, `:` execute with <50ms latency); frame rate is ≥30 FPS; typing in the command palette (if open) is not jittery.

#### REQ-NF-002: Preview Pane Responsiveness
**Event-driven:** When `cursorRow` changes (via `j`, `k`, arrow keys, or mouse click), the preview pane shall update its content within 100 ms for cached entries, or within 500 ms for entries requiring decode.

**Acceptance Criterion:** Rapid keypresses (`j` `j` `j`) move through a folder; preview pane updates at each step without lag; a folder of images with some cached and some not shows fast updates for cache hits and slower (but <500ms) updates for cache misses.

#### REQ-NF-003: Memory Efficiency
**Ubiquitous:** The system shall not load entire large files into memory for preview. Text files larger than 64 KB shall be read with a head-read (first 64 KB only); image decoding shall use on-demand decoding or streaming, not load the entire compressed file into memory before decode.

**Acceptance Criterion:** Previewing a 1 GB video file (even if not fully previewed) does not allocate >200 MB of RAM; previewing a 100 MB text file loads only the first 64 KB; system RAM usage remains stable when browsing folders with many large files.

#### REQ-NF-004: Startup and First-Preview Latency
**Ubiquitous:** The system shall initialize the preview pane infrastructure and display the first preview for the initial cursor position within 500 ms of application startup.

**Acceptance Criterion:** Launch the application; the preview pane is visible and shows preview of the first file entry (or placeholder if empty) within 500 ms; startup time is not significantly increased by Stage 2's addition.

---

## Constraints

#### REQ-C-001: External Dependency – libexif
**Constraint:** EXIF metadata extraction shall use the **libexif** C library (new external dependency for this project). It shall be vendored or linked as a CMake dependency (e.g., via Conan, vcpkg, or manual include).

**Acceptance Criterion:** CMakeLists.txt explicitly lists libexif as a dependency; EXIF metadata is extracted via libexif's public C API; no inline EXIF parsing or alternative library is used for EXIF (Qt's built-in metadata support, if present, may be used for non-EXIF formats or as a fallback).

#### REQ-C-002: No Thumbnail Icons in Row Delegates
**Constraint:** The directory listing row delegates (DirectoryListing.qml) shall NOT display thumbnail icons from the ThumbnailService cache. Thumbnails are cached and used only by the preview pane and Quick Look overlay; the listing remains text-only (file name, size, date, etc.) as in Stage 1.

**Acceptance Criterion:** A folder of images is displayed; the listing shows file names and metadata, no thumbnail preview; the preview pane and Quick Look show thumbnails; code review confirms no thumbnail textures are bound to row delegates.

#### REQ-C-003: Single-Entry Scope
**Constraint:** This stage operates on a single cursor entry. Multi-entry inspection or aggregate preview (e.g., "N items selected, total size...") is **not** in scope; that depends on Stage 3's multi-selection (VISUAL mode).

**Acceptance Criterion:** A test that attempts to inspect multiple entries (if a way to trigger multi-selection exists in Stage 2) is a non-goal; no aggregate metadata or multi-file preview is visible in this stage.

#### REQ-C-004: Reuse Stage 1 DirectoryController and Async Pattern
**Constraint:** The preview pane shall integrate with the existing `DirectoryController`, `DirectoryModel`, and worker-thread infrastructure from Stage 1. A new `PreviewService` or similar component shall follow the same async/thread-safe patterns as `DirectoryModel` (incremental batching, stale cancellation, queue-based result delivery).

**Acceptance Criterion:** Code review confirms PreviewService follows DirectoryModel's pattern; no new threading primitives or libraries are introduced; existing tests of DirectoryModel continue to pass.

#### REQ-C-005: Fullscreen Window State Preservation
**Constraint:** Stage 1 introduced window state preservation for tiled fullscreen. The preview pane layout and resizing shall respect this state within a single running session (i.e., toggling tiled fullscreen during a session preserves preview pane width/proportion). Persisting preview pane proportion across an application restart is out of scope for v1 — this codebase has no settings-persistence mechanism yet, and introducing one is a non-goal of this stage (session-only behavior is acceptable; the pane may reset to a sensible default proportion on each launch).

**Acceptance Criterion:** Toggle tiled fullscreen with window-manager keybinding (e.g., Super+F11) during a session; preview pane adjusts appropriately and its proportion is preserved across the toggle. Restarting the app is explicitly not required to restore the prior proportion.

#### REQ-C-006: Qt 6 and QML
**Constraint:** The preview pane and Quick Look overlay shall be implemented as QML components and C++ integrations with Qt 6, consistent with the existing DirectoryListing and PlacesPanel architecture.

**Acceptance Criterion:** Preview pane is a QML Item embedded in Main.qml; Quick Look overlay is a QML Popup or overlay component; all C++ backend (PreviewService, ThumbnailService) integrates via Qt property bindings and signals/slots.

---

## Non-Goals

The following features are **explicitly out of scope** for Stage 2 and shall NOT be implemented:

- **PDF, Video, and Audio Preview:** PDF thumbnails, video playback, audio spectrum/metadata rendering, or preview of any media type beyond images and text. (Deferred to a future stage.)
- **Thumbnail Icons in Directory Listing:** Thumbnail images shall not appear as icons in the DirectoryListing row delegates. The listing remains text-only. (This is a Stage 3+ enhancement.)
- **Multi-Item Preview / Aggregate Summary:** Preview of multiple selected entries (e.g., "5 items selected, total 512 MiB") depends on Stage 3's multi-selection and is out of scope. This stage previews the single cursor entry only.
- **Quick Look Editing:** The Quick Look overlay is read-only. No in-place editing of text or metadata, no export/move/delete actions, no command palette integration within Quick Look.
- **Text Syntax Highlighting:** Text preview shows plain text only, without syntax highlighting (e.g., no Python/C++ keyword coloring). Future enhancement.
- **EXIF Data Writing / Modification:** The system reads EXIF data only. Writing, modifying, or removing EXIF tags is not supported.
- **GPS Coordinate Display:** GPS coordinates may be extracted from EXIF (if present) but shall NOT be displayed by default in v1 (privacy-sensitive). If extraction is trivial, it may be done internally for potential future use, but it is not a requirement. No UI element exposes GPS data in v1.
- **Remote / Network Filesystem Preview:** This stage operates on local filesystems only (consistent with Stage 1 scope). No preview of files on SMB/NFS/SSH mounts or cloud storage providers.
- **Cross-Restart Settings Persistence:** Preview pane proportion is session-only in v1. This codebase has no settings-persistence mechanism (e.g., `QSettings`), and introducing one is out of scope for this stage. The pane may reset to a default proportion on each application launch.

---

## Open Questions / Deferred Decisions

The following design questions are intentionally left open for later refinement:

1. **Full EXIF Field List:** The specification names five EXIF fields as minimum for v1 (camera make, model, exposure time, ISO, focal length). The complete list of additional EXIF fields (e.g., aperture F-number, white balance, metering mode, lens model, etc.) is **not finalized**. Additional fields may be added to the UI without requiring a spec update; removal of any of the five named fields constitutes a breaking change and requires design review.

2. ~~**Thumbnail Cache Size Confirmation**~~ — Resolved during DESIGN: the freedesktop "normal" size is a 128px bounding box with aspect ratio preserved (not a literal 128×128 square); REQ-F-019 has been corrected to reflect this. If a larger cache size (e.g., 256×256) is visually preferable once the pane layout is finalized, that remains open for future revisit, but the bounding-box semantics are now settled.

---

## Fixtures and Test Data

The following fixtures and test data are required to verify this specification. Reuse fixtures from Stage 1 where applicable; add new fixtures as listed.

### Reused from Stage 1
- **Empty directory:** Directory with zero entries (covers REQ-F-009, REQ-F-013).
- **Large directory (10k+ files):** Stress test for async loading and preview pane responsiveness (covers REQ-NF-002).
- **Permission-denied directory:** Directory the app can list but cannot read entries fully (covers REQ-F-024).
- **Unicode filenames:** Files with non-ASCII names (Japanese, Cyrillic, emoji, etc.) to verify charset handling (covers REQ-F-002).

### New for Stage 2
- **Valid JPEG with EXIF data:** A real or synthesized JPEG containing complete EXIF tags (camera make, model, exposure time, ISO, focal length). Used to verify REQ-F-006.
  - *Recommendation:* Use a public-domain or freely-licensed photo (e.g., from Unsplash with EXIF intact, or generate with exiftool). Store in `tests/fixtures/images/jpeg_with_exif.jpg`.

- **JPEG without EXIF data:** A JPEG stripped of all EXIF metadata. Verifies that missing EXIF fields do not cause crashes and generic metadata is shown (REQ-F-006, REQ-F-023).
  - *Recommendation:* Strip EXIF from a JPEG using exiftool or ImageMagick: `exiftool -All= input.jpg -o output.jpg`. Store in `tests/fixtures/images/jpeg_no_exif.jpg`.

- **Corrupt / Truncated Image:** A JPEG or PNG file truncated mid-stream or with a corrupted header. Verifies graceful fallback (REQ-F-023).
  - *Recommendation:* Truncate a valid image with `head -c 500 valid.jpg > truncated.jpg`. Store in `tests/fixtures/images/corrupt_jpeg.jpg`.

- **Small Text File:** A plain text file <64 KB (e.g., 10 KB of Lorem Ipsum or the CLAUDE.md from this project). Verifies text preview (REQ-F-007).
  - *Recommendation:* Create `tests/fixtures/text/small.txt` with ~10 KB of UTF-8 plain text.

- **Large Text File:** A plain text file >64 KB (e.g., 200 KB). Verifies REQ-F-008 (truncation notice).
  - *Recommendation:* Generate with `yes "Lorem ipsum dolor sit amet" | head -c 200000 > tests/fixtures/text/large.txt`.

- **Binary Non-Image File:** A file that is clearly binary but not recognized as an image format (e.g., a compiled ELF executable, a compiled Python `.pyc`, or random bytes generated with `/dev/urandom`). Verifies binary detection (REQ-F-022).
  - *Recommendation:* Create `tests/fixtures/binary/random.bin` with `dd if=/dev/urandom of=random.bin bs=1024 count=10`. Alternatively, copy a small system executable (e.g., `/bin/true` if available) or check in a fixture `.so`/`.dylib`.

- **Broken Symlink:** A symbolic link pointing to a nonexistent target. Verifies permission/symlink fallback (REQ-F-024).
  - *Recommendation:* Create in test setup via `ln -s /nonexistent/path/to/file tests/fixtures/symlinks/broken_link.txt`.

- **Text File with Non-ASCII UTF-8:** A text file containing non-ASCII characters (e.g., emoji, CJK text, Latin with diacritics). Verifies that binary detection does not false-positive and text is rendered correctly (REQ-F-007, REQ-F-022).
  - *Recommendation:* Create `tests/fixtures/text/utf8_multilang.txt` with content like "Hello 世界 🌍 Привет".

- **PNG with EXIF:** A PNG file with embedded EXIF data (PNG "eXif" chunk). Verifies EXIF extraction works for formats beyond JPEG (REQ-F-006).
  - *Recommendation:* Use a PNG tool or exiftool to add EXIF data to a PNG. Store in `tests/fixtures/images/png_with_exif.png`.

- **Multiple Image Formats:** One file each of PNG, WebP, GIF, BMP, SVG, TIFF to test format coverage (REQ-F-021).
  - *Recommendation:* Create or download small test images (public domain, e.g., from Wikipedia) in each format. Store in `tests/fixtures/images/`.

---

## Acceptance Criteria Summary

| Requirement | Acceptance Gate | Verifiable In |
|-------------|-----------------|---------------|
| REQ-F-001 | Preview pane visible, updates on cursor move | Manual UI inspection, automated keystroke simulation |
| REQ-F-002 | All five generic metadata fields shown for all entries | Fixture: each file type in directory |
| REQ-F-003 | Image preview for recognized formats; text files show no image | Fixture: jpeg_with_exif.jpg, small.txt |
| REQ-F-004 | Image rescales on window resize, preserves aspect ratio | Manual window resize test or automated window resizing |
| REQ-F-005 | No blur/pixelation at 1.5x scaling | Test environment with fractional scaling, visual inspection or pixel-comparison test |
| REQ-F-006 | EXIF fields extracted and displayed | Fixture: jpeg_with_exif.jpg, png_with_exif.png |
| REQ-F-007 | Text file content displayed up to 64 KB | Fixture: small.txt, large.txt |
| REQ-F-008 | Truncation notice shown for files >64 KB | Fixture: large.txt |
| REQ-F-009 | Placeholder shown for empty/no-cursor state | Fixture: empty directory, or deleting all entries |
| REQ-F-010 | Space opens Quick Look, same content as sidebar | Manual: press Space, inspect overlay |
| REQ-F-011 | j/k update overlay live while open | Manual: Quick Look open, press j/k multiple times |
| REQ-F-012 | Space/Esc closes Quick Look, focus returns to listing | Manual: Quick Look open, press Space or Esc |
| REQ-F-013 | Space does nothing on empty/no-cursor | Fixture: empty directory, press Space |
| REQ-F-014 | UI non-blocking during decode | Profiling: watch frame rate / input latency during decode |
| REQ-F-015 | Stale decodes cancelled, no image jumble on rapid cursor moves | Manual: rapid j/k through image folder, inspect preview |
| REQ-F-016 | Corrupt image falls back to metadata + error notice within 3s | Fixture: corrupt_jpeg.jpg |
| REQ-F-017 | Thumbnails cached in freedesktop structure | Verify cache files exist in `$XDG_CACHE_HOME/thumbnails/normal/` |
| REQ-F-018 | Cache invalidated on mtime/size change | Fixture: preview image, modify source, verify re-decode |
| REQ-F-019 | Thumbnails are 128×128 | Verify cache file dimensions (e.g., with `file` or `identify`) |
| REQ-F-020 | Cache directory auto-created if missing | Test on fresh system or remove cache, verify creation |
| REQ-F-021 | All named formats preview successfully | Fixture: images in each format (JPEG, PNG, WebP, GIF, BMP, SVG, TIFF) |
| REQ-F-022 | Binary files detected, no garbled text preview | Fixture: random.bin, compiled executable |
| REQ-F-023 | Corrupt image graceful fallback | Fixture: corrupt_jpeg.jpg, zero-byte file |
| REQ-F-024 | Broken symlink / permission denied show notice | Fixture: broken_link, permission-denied file |
| REQ-NF-001 | UI thread unblocked during decode | Profiling: frame rate ≥30 FPS during decode |
| REQ-NF-002 | Preview pane <100ms for cache, <500ms for decode | Profiling: measure cursorRow → preview update latency |
| REQ-NF-003 | No full-file load for large files | Profiling: memory usage stable, peak <200MB for 1GB preview |
| REQ-NF-004 | First preview within 500ms of startup | Profiling: measure launch time |
| REQ-C-001 | libexif dependency in CMakeLists.txt | Code review: CMake find_package(exif) or Conan/vcpkg reference |
| REQ-C-002 | No thumbnail icons in row delegates | Code review: DirectoryListing.qml, ThumbnailService.qml |
| REQ-C-003 | Single-entry scope only | Design: no multi-selection UI in this stage |
| REQ-C-004 | Reuse DirectoryModel pattern in PreviewService | Code review: async queue, single worker thread, stale cancellation |
| REQ-C-005 | Fullscreen state respected | Manual: toggle fullscreen, verify preview pane resizes |
| REQ-C-006 | QML + Qt 6 integration | Code review: Main.qml embeds preview component, C++ backend via Q_PROPERTY / signals |

---

## Implementation Notes

- **Preview Service Architecture:** Implement a `PreviewService` (C++) managing both `ThumbnailService` (cached image decoding) and a `TextPreviewService` (text head-read). Both operate on a single dedicated worker thread, similar to `DirectoryModel`'s async pattern.
- **QML Components:** 
  - `PreviewPane.qml` — the docked sidebar preview (reactive to `cursorRow`).
  - `QuickLookOverlay.qml` — the fullscreen/nearly-fullscreen overlay (triggered by Space, dismissed by Space/Esc).
  - Both bind to the same `PreviewService` data model.
- **Integration with DirectoryController:** Extend `DirectoryController` to expose a `PreviewService` property; QML accesses it via `controller.preview`.
- **Error Handling:** Every decode/read path must route errors (decode failure, permission denied, etc.) to a `PreviewError` model object, which is displayed as a notice in the QML component (never as a dialog or silent omission).
- **Testing:** Add unit tests for `ThumbnailService` (cache validation, format detection); integration tests for full preview pane workflow (cursor move, decode, display, Quick Look interaction).

## Review remediation requirements (approved implementation plan)

- REQ-R-001: Space press/release shall be consumed before delegate button activation, with auto-repeat ignored. Shortcut override shall only accept events. Focused popup content and selectable text shall route Space/Escape/j/k; dismissal restores listing focus and preserves fullscreen. Enter/l and mouse opening remain available. Toggle clears pending counts/chords; invalid selection cannot open Quick Look.
- REQ-R-002: Special files shall receive metadata only. Workers shall open with O_NONBLOCK | O_CLOEXEC and fstat-verify regular files, including symlink targets, before reading. Image, text and EXIF helpers shall use that verified descriptor and report read errors.
- REQ-R-003: EXIF extraction shall seek across JPEG segments and PNG chunks, allocate at most 1 MiB payload, scan at most 4,096 records and check cancellation between records. Invalid metadata shall not prevent image preview.
- REQ-R-004: Thumbnail results shall be delivered before full decode/EXIF with explicit intermediate/final stages. Intermediate results retain busy state and deadline. Timeout cancels work, clears specialized content, retains metadata/notice, and rejects all later results.
- REQ-R-005: One worker shall have one active and at most one replaceable pending request. Superseded target/resize/timeout/shutdown work shall be cancelled. Full-resolution LRU retention shall be bounded to two entries and 64 MiB, keyed by source identity/revision and dimensions; disk thumbnails remain 128px normal tier.
- REQ-R-006: Pane and QuickLook shall report actual image-area pixel sizes on completion, resize and DPR change. Only the active consumer controls sizing. Resize dispatch shall debounce 150ms and compare aspect-fitted requirements with decoded/dispatched dimensions, including gradual growth.
- REQ-R-007: Metadata snapshot and source revision changes shall refresh unchanged paths. Watch selected regular files and the directory; reattach after replacement and invalidate memory/disk reuse on modification. Empty selection closes Quick Look.
- REQ-R-008: Metadata shall use KiB/MiB/GiB/TiB with exact bytes, locale timestamps with ISO fallback and explicit unavailable values.

Native acceptance remains required: cached <100ms, initial/uncached <500ms, Quick Look updates <200ms; 100+ images over 5MB at 10+ movements/s, peak memory <500MB and >=30 FPS. Verify both locales as non-root, dark/light, fractional scaling, splitter resizing and fullscreen restoration. Unavailable checks remain pending. Hard preemption of Qt decoding is outside scope; bounded allocation, cooperative cancellation and a three-second visible deadline are the limits.
