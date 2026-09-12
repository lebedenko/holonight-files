# SDD Tasks — main-view-icons

- [x] T-001: Record wall-clock baseline before implementing icon changes
  - REQs: REQ-F-021
  - Check: On the current main branch (before any icon implementation commits), build the application in release mode and run `task test` to completion, recording the wall-clock time and `files-memory-benchmark` test result (if present) or `FILES_DIRECTORY_PERF` output in a temporary file (e.g., `docs/sdd/main-view-icons/baseline_pre_icons.txt`) with timestamp and machine/OS details; this baseline will be compared against the post-icons build to satisfy the "10% regression" criterion in REQ-F-021.

- [x] T-002: Implement icon_name_resolver.h/.cpp namespace for icon name derivation
  - REQs: REQ-F-005, REQ-F-006, REQ-F-007
  - Check: `apps/files/icon_name_resolver.h` defines `candidateIconNames(quint32 mode, const QString& fileName)` and `genericFallbackName(bool isDir)` functions that return ordered `QStringList` and `QString` respectively; calling these with known mode/filename pairs (directory, FIFO, socket, chardevice, blockdevice, `.py` file) produces the exact candidate chains specified in REQ-F-006's examples; project builds without errors.

- [x] T-003: Add icon_name_resolver unit tests to tests/icon_name_resolver_test.cpp
  - REQs: REQ-F-006
  - Check: `tests/icon_name_resolver_test.cpp` contains nine separate `TEST()` functions verifying the exact ordered candidate lists for: directory, FIFO, socket, character device, block device, `.py` file, `.jpg` file, extensionless `README`, and unregistered `.xyz123` extension; all tests pass with gtest assertions on `QStringList` equality in order; file is added to `tests/CMakeLists.txt` `SOURCES` for `files-smoke` binary.

- [x] T-004: Implement icon_image_provider.h/.cpp as synchronous QQuickImageProvider
  - REQs: REQ-F-002, REQ-F-001, REQ-F-020, REQ-F-018
  - Check: `apps/files/icon_image_provider.h` defines `IconImageProvider : QQuickImageProvider(Pixmap)` with `requestPixmap(id, size, requestedSize)` override; provider reads the candidate chain from the id by splitting on `/` (it never re-derives the chain itself — per DESIGN.md §5.2 the provider has no mode or filesystem context and must do no MIME work on the GUI thread), loops through candidates calling `QIcon::fromTheme(name)` for each, caches results in a bounded `QCache<QString, QPixmap>` keyed by `"<name>@<w>x<h>"`, returns the first non-null pixmap or null if all candidates fail; no calls to `QIcon::setThemeName()`, `setFallbackThemeName()`, or `setThemeSearchPaths()` anywhere in the file; project builds.

- [x] T-005: Add icon_image_provider unit tests to tests/icon_image_provider_test.cpp
  - REQs: REQ-F-002, REQ-F-016, REQ-F-020, REQ-F-022
  - Check: `tests/icon_image_provider_test.cpp` covers null results, empty theme, cache hits, and malformed-chain failure safety. The earlier provider-only warning assertion was removed because Qt Quick also emits image-load warnings; strict REQ-F-022 log-count acceptance remains open in VERIFICATION.md.

- [x] T-006: Spike qt_add_qml_module plugin customization vs inline registration
  - REQs: REQ-C-005, REQ-NF-001
  - Check: Create a minimal `holonight_files_plugin.h` with `QQmlEngineExtensionPlugin::initializeEngine()` override, add `NO_GENERATE_PLUGIN_SOURCE CLASS_NAME HolonightFilesPlugin` to `qt_add_qml_module()` call, add the new files to `SOURCES`, compile a test binary (e.g., a minimal `smoke.cpp` that imports the module and creates an engine), confirm the plugin's `initializeEngine()` is called once per engine by inserting a `qDebug()` statement and observing its output; if successful, document this as the chosen path in the implementation tasks below; if it fails, document the inline-registration fallback (requiring `engine.addImageProvider(...)` at each of the four/ten engine construction sites) as the alternative and adjust subsequent tasks accordingly.

- [x] T-007: Add IconNameRole to DirectoryModel and icon_name field to DirectoryEntry
  - REQs: REQ-C-001, REQ-F-006, REQ-F-011
  - Check: `apps/files/directory_model.h` adds `IconNameRole` to the `Role` enum after `StatErrorRole`; `roleNames()` is updated to include `{IconNameRole, "iconName"}`; `DirectoryEntry` struct gains `QString icon_name;` field; the field participates in `operator==()` so changes are detected by the existing `refresh()` diffing machinery; `apps/files/directory_model.cpp`'s `insertPlaceholderRow()` sets the placeholder's `icon_name` to `IconNameResolver::genericFallbackName(false)` (generic file, not folder); project compiles without errors.

- [x] T-008: Integrate IconNameResolver into DirectoryModel worker's readEntry()
  - REQs: REQ-F-005, REQ-C-004
  - Check: `apps/files/directory_model.cpp`'s `readEntry()` function, after the existing `stat()` call succeeds, calls `IconNameResolver::candidateIconNames(info.st_mode, name)` and joins the result with `/` to populate `entry.icon_name`; on `stat()` failure, calls `IconNameResolver::genericFallbackName(false)` and sets `entry.icon_name` to that single name; MIME detection occurs only on the worker thread (never moved to GUI thread), and the icon name is set before the entry is queued back to the GUI; existing directory-listing tests still pass; the worker thread's profiling shows no measurable regression in listing speed for a 10k-entry directory.

- [x] T-009: Add DirectoryModel unit tests verifying IconNameRole end-to-end
  - REQs: REQ-C-001
  - Check: `tests/directory_model_test.cpp` includes a new test fixture/parametrized test that populates a DirectoryModel with a small fixture directory (regular file, subdirectory, dangling symlink), calls `model.data(index, DirectoryModel::IconNameRole)` for each row, and asserts the icon names match expected values (e.g., `"folder"` for the directory, `"application-x-generic/..."` for the file, `"application-x-generic"` for the dangling symlink); test passes unchanged.

- [x] T-010: Add iconName property to PreviewService
  - REQs: REQ-C-002, REQ-F-015
  - Check: `apps/files/preview_service.h` adds `Q_PROPERTY(QString iconName READ iconName NOTIFY changed)` and `QString icon_name_` private member; `setTarget()` method signature gains a `const QString& iconName` parameter placed just before `quint64 revision`; `clear()` resets `icon_name_` to empty string; `iconName()` accessor returns `icon_name_`; the property is read-only and notifies `changed()` when set.

- [x] T-011: Update DirectoryController to forward IconNameRole to PreviewService
  - REQs: REQ-F-015
  - Check: `apps/files/directory_controller.cpp`'s `syncPreviewTarget()` method, at the line reading `preview_.setTarget(...)`, adds one additional argument: `model_.data(sourceIndex, DirectoryModel::IconNameRole).toString()`, inserted as the eighth positional argument (after `statError` and before `preview_revision_`); the call signature now matches PreviewService's updated `setTarget()` signature; compilation succeeds.

- [x] T-012: Add PreviewService unit tests verifying iconName propagation
  - REQs: REQ-F-015, REQ-C-002
  - Check: `tests/preview_service_test.cpp` includes a new test that calls `preview.setTarget(path, ..., "text-x-python/text-x-generic/...")` with an icon name, then verifies `preview.iconName()` returns that exact string; a second test calls `preview.clear()` and asserts `iconName()` becomes empty; a third test calls `setTarget()` twice with different icon names and asserts each call updates the property; test passes.

- [x] T-013: Register new C++ files and SVG resources in apps/files/CMakeLists.txt
  - REQs: REQ-C-005
  - Check: `apps/files/CMakeLists.txt`'s `SOURCES` list includes `icon_name_resolver.cpp`, `icon_image_provider.cpp`, and (if plugin customization was chosen) `holonight_files_plugin.cpp`; a new `RESOURCES` block (or `qt_add_resources()` call) includes `apps/files/icons/folder-fallback.svg` and `apps/files/icons/generic-file-fallback.svg` so they are compiled into the `files-ui` QML module's resource bundle; `qt_add_qml_module()` call includes `NO_GENERATE_PLUGIN_SOURCE CLASS_NAME HolonightFilesPlugin` (if spike succeeded) or retains auto-generation (if rejected); `target_link_libraries(files-ui PRIVATE Qt6::Qml)` or similar already includes Qml (pre-existing, no change required); project builds without CMake errors.

- [x] T-014: Implement and register IconImageProvider in plugin or at engine construction sites
  - REQs: REQ-F-002, REQ-NF-001
  - Check: If plugin spike (T-005) succeeded: `apps/files/holonight_files_plugin.h` defines `class HolonightFilesPlugin : public QQmlEngineExtensionPlugin`, overrides `void initializeEngine(QQmlEngine* engine, const char* uri)` with a call to `engine->addImageProvider(QStringLiteral("icon"), new IconImageProvider)` inside; the plugin registers with `Q_PLUGIN_METADATA(IID QQmlEngineExtensionInterface_iid)`. If rejected: add `engine.addImageProvider(QStringLiteral("icon"), new IconImageProvider)` at each of the four engine construction sites (`apps/files/main.cpp`, `tests/smoke.cpp`, `tests/directory_performance_test.cpp`, `tests/window_cross_filesystem_test.cpp`); all engines that import `HolonightFiles` module have the provider registered; test: instantiate a QML component with `HnIcon { source: "image://icon/folder" }` and verify the image resolves without errors.

- [x] T-015: Add iconColumnWidth property and icon cell to DirectoryListing.qml delegate
  - REQs: REQ-F-008, REQ-F-010, REQ-F-003, REQ-C-003
  - Check: `apps/files/DirectoryListing.qml`'s root `Item` declares `readonly property real iconColumnWidth: 20`; delegate adds `required property string iconName` alongside existing required properties; the delegate's `contentItem` `RowLayout` gains a new first child `Item` with `objectName: "iconColumnField"`, fixed width `iconColumnWidth`, containing an `HnIcon` pair (theme icon with `source: "image://icon/" + delegate.iconName` visible when no error, fallback SVG visible on error); code review confirms no plain `Image` component is used for file-type icons, only `HnIcon`; all icon sources follow the `"image://icon/" + iconName` pattern (REQ-C-003); the icon cell uses `anchors.centerIn` to center the icon; visual inspection confirms the icon column is 20 logical pixels wide and appears before the Name cell in every row; existing Name, Size, Modified cells shift one position to the right but otherwise remain unchanged.

- [x] T-016: Indent DirectoryListing header Name label to align with icon column
  - REQs: REQ-F-009, REQ-F-026
  - Check: `apps/files/DirectoryListing.qml`'s existing header row (`objectName: "directoryColumnHeader"`) has its "Name" label adjusted to include `Layout.leftMargin: root.iconColumnWidth + root.columnSpacing`; the header's Size and Modified `HnSeparator`s remain unchanged between their columns (no new separator between icon and Name); visual inspection across window widths 420–1600 px confirms the header "Name" label and delegate Name cell left edges align within 1 pixel; the icon column has no vertical divider or separator between it and the Name column.

- [x] T-017: Add icon fallback display to PreviewPane.qml thumbnail slot
  - REQs: REQ-F-012, REQ-F-013, REQ-F-014, REQ-F-017, REQ-F-003, REQ-F-004, REQ-C-003
  - Check: `apps/files/PreviewPane.qml`'s `imageArea` `visible` binding changes from `root.preview.hasImage` to `root.preview.hasEntry` so the slot stays reserved when only an icon (no thumbnail) is shown; inside `imageArea`, `PreviewImageItem` gains `visible: root.preview.hasImage`; a new `HnIcon` pair is added: primary theme icon (REQ-F-003, rendered untinted via `image://icon/` scheme per REQ-C-003) with `source: "image://icon/" + root.preview.iconName` and `size: Math.min(128, imageArea.width)`, visible only when `!hasImage && !hasError`; secondary fallback SVG with `tinted: true` (REQ-F-004: never routed through `image://hnicons` for MIME icons, only for bundled glyphs), sized identically, visible when `!hasImage && themeIcon.hasError`; code review confirms no plain `Image` is used, only `HnIcon` (REQ-F-003); the fallback glyph choice is derived from `isFolderIconName: root.preview.iconName === "folder" || root.preview.iconName.startsWith("folder/")` to pick folder or generic-file SVG; both icons are centered in the slot via `anchors.centerIn`; text preview below the metadata labels remains unchanged when icon is displayed; visual inspection shows icons display at 128 px max and are centered.

- [x] T-018: Create and bundle folder-fallback.svg and generic-file-fallback.svg
  - REQs: REQ-F-016, REQ-F-017
  - Check: Two new files `apps/files/icons/folder-fallback.svg` and `apps/files/icons/generic-file-fallback.svg` are created as simple line-art SVGs in the HoloNight design-system style, legible at both 20 and 128 logical pixels; both files include REUSE/SPDX licensing headers identical to those in `packaging/org.holonight.Files.svg` (copyright 2026, GPL-3.0-or-later); files are included in `apps/files/CMakeLists.txt` `RESOURCES` block; `task license-check` passes with no complaints about the new SVGs; with an empty icon theme (via QScopeGuard in test), both glyphs are rendered correctly tinted by `HnIcon`; visual inspection confirms both glyphs display in palette colors and change with dark/light theme fixtures.

- [x] T-019: Add icon column structural assertions to tests/smoke.cpp
  - REQs: REQ-F-008, REQ-F-009, REQ-F-026, REQ-NF-002
  - Check: `tests/smoke.cpp` gains a new test (or extends an existing populated-window test) that: (1) renders a DirectoryListing with at least one file and one folder; (2) locates the first delegate via `rootObject->findChild<QObject*>("directoryListDelegate")` or similar; (3) asserts the delegate has a child `Item` with `objectName: "iconColumnField"` and `width() == 20` (logical pixels); (4) locates the header via `rootObject->findChild<QObject*>("directoryColumnHeader")`; (5) measures the "Name" label's `x` (left edge) in both header and first delegate row, and asserts they align within 1 pixel; (6) verifies existing `title`, `filenameRun`, and `inlineNameEditor` object-name assertions still pass unmodified; test passes with all assertions.

- [x] T-020: Manual fractional-scale sharpness verification on Hyprland 1.5x display
  - REQs: REQ-F-019
  - Check: On a real Wayland display running Hyprland with 1.5x fractional scaling (or 1.25x if 1.5 unavailable), build the application and launch it with a directory containing various file types (folders, images, documents); visually inspect the file icons in the listing and preview pane at native display resolution (not a screenshot capture); confirm icons are sharp, not pixelated or blurry, and do not show upscaling artifacts; if the window is moved to a second display with different DPI (or when the compositor updates fractional scale during runtime), the icons re-render at the new scale without visible quality degradation; record the observation in VERIFICATION.md as "fractional-scale sharpness verified on real Hyprland 1.5x display" with date and display hardware details.

- [x] T-021: Compare wall-clock performance after icon implementation
  - REQs: REQ-F-021
  - Check: After all implementation tasks are complete, build the application in release mode and run `task test` again, recording results in `docs/sdd/main-view-icons/baseline_post_icons.txt`; run both the 10k-entry directory listing test and any memory-benchmark test with icons enabled; compare wall-clock times: post-icons performance stays within 10% of pre-icons baseline (i.e., wall-clock delta / baseline ≤ 0.10); if regression exceeds 10%, profile the icon provider's cache hit rate and adjust as needed; document the comparison and decision (pass/remediate) in VERIFICATION.md.

- [x] T-022: Verify existing keyboard navigation, inline editing, and stat-error indicator unchanged
  - REQs: REQ-F-023, REQ-F-024, REQ-F-025
  - Check: `tests/smoke.cpp` tests `ModalEditingWindowKeyboardAndHighlighting` and `PopulatedWindowKeyboardAndInlineError` pass unmodified, confirming: (1) j/k/page-up/page-down/G navigation works unchanged; (2) search-match highlighting in the Name cell remains functional; (3) inline INSERT-mode editor appears and functions unchanged, its full-row overlay geometry is unaffected by the new icon cell; (4) stat-error warning indicators (e.g., permission-denied marks) are visible and uncut in rows; no assertions need loosening due to the icon column; all keyboard shortcuts and editor state transitions work as before.

- [x] T-023: Visual regression snapshot regeneration and final verification
  - REQs: REQ-NF-002
  - Check: Run `scripts/check-visual.sh` (or `task visual-check`) and regenerate snapshots in `build/visual/` for all theme/scale combinations (light/dark × 1.0/1.25/1.5 scales) capturing the new icon column in the directory listing and icon fallback in the preview pane; visually review generated PNGs to confirm: (1) icon column is visible and correctly spaced, (2) header Name label aligns with row Name cells, (3) Size and Modified columns align, (4) no visual regressions in existing UI elements; record the new snapshots as approved baselines; run `task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint`, `task license-check`, and `task install-check` — all must pass; verify window minimum dimensions remain 420×280; confirm the icon column does not cause any layout failures at minimum or maximum window widths.

- [x] T-024: Keep a visible placeholder if a bundled fallback SVG fails
  - REQs: REQ-F-022
  - Check: `Files.IconColumnUsesThemeIconsAndFallsBackToBundledGlyphs` forces invalid resource URLs for both listing and preview fallback icons and observes the corresponding placeholders.

- [ ] T-025: Limit total Qt Quick image-load warnings to one per distinct icon name
  - REQs: REQ-F-022
  - Check: With a missing theme, capture all warnings while listing and preview request the same chain at changing sizes; assert at most one total warning for that chain without suppressing unrelated Qt warnings.
