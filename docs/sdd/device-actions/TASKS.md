# SDD Tasks — device-actions

Implements [SPEC.md](SPEC.md) per [DESIGN.md](DESIGN.md). Baseline `1cc5b2f`.

Commands below use Google Test filters, not CTest test-name filters: these suites all belong to the
single `files-smoke` CTest test. Run them with `QT_QPA_PLATFORM=offscreen
QSG_RHI_BACKEND=software` and the installed HoloNight `QML_IMPORT_PATH`/`LD_LIBRARY_PATH`.

Two wiring facts that bite silently:

- A new `.qml` file must be added to the explicit `module_qml` list in `apps/files/CMakeLists.txt`
  (`100-106`). That list feeds both `QML_FILES` and the `qml-lint` target, so an unlisted file is
  invisible to both. `scripts/format-sources.py` discovers `apps/` and `tests/` recursively, so
  neither it nor `Taskfile.yml` needs touching.
- Per REQ-C-003 and REQ-C-006 feature fixtures use `FakeStorage` and a substituted
  `CapacityProbe`; the small `CapacityProbe.*` adapter smoke tests use a temporary local directory.

Tasks are ordered so each one builds and tests green on its own.

---

- [x] T-001: `StoragePolicy` — classification, removal verb, icon name (original fixtures)
  - REQs: REQ-F-001, REQ-F-002, REQ-F-003, REQ-F-004, REQ-F-005, REQ-F-006, REQ-F-007, REQ-F-008, REQ-F-009, REQ-F-011, REQ-F-036, REQ-C-001, REQ-C-002, REQ-C-004
  - Wiring: add `apps/files/storage/storage_policy.h` (header-only, no CMake source entry needed) and `tests/storage_policy_test.cpp` to the `files-smoke` source list in `tests/CMakeLists.txt`.
  - Check: `build/test/tests/files-smoke --gtest_filter='StoragePolicy.*'` passes asserting: `optical=true` classifies Optical for both empty and `"usb"` connection buses; non-empty `connectionBus` classifies External for `"usb"`, `"ieee1394"`, `"sdio"` and an arbitrary value not present in the source; empty `connectionBus` with `optical=false` classifies Internal even when `removable`/`canEject`/`canPowerOff` are all true; `removalVerb` selects Eject for `mediaRemovable=true, canEject=true, canPowerOff=true` and PowerOff for `mediaRemovable=false` with the same capabilities; falls back to the alternate drive verb when the preferred one is unadvertised; falls back to Unmount when neither drive verb is advertised and the volume reports `canUnmount`; returns `nullopt` when neither drive verb is advertised and `canUnmount` is false; returns Unmount (never Eject or PowerOff) for an Internal drive advertising both drive capabilities; a four-LUN reader fixture never yields PowerOff from any slot; and `iconName` returns `media-optical-symbolic`, `media-flash-symbolic`, `drive-removable-media-symbolic`, `drive-harddisk-symbolic` for the four cases.
  - Check: a source scan of `apps/files/storage/storage_policy.h` finds no occurrence of `vendor`, `model`, `serial`, `siblingId`, `device`, `mountPoints`, nor any literal bus value (`"usb"`, `"ieee1394"`, `"sdio"`) — REQ-C-001, in the spirit of the existing `TomlLibraryIsIncludedOnlyByTheAdapter` scan.

- [x] T-002: `CapacityProbe` interface, `QStorageInfo` implementation, test fake
  - REQs: REQ-F-041, REQ-F-042 (seam only), REQ-C-006
  - Wiring: add `apps/files/storage/capacity_probe.{h,cpp}` to `apps/files/CMakeLists.txt` and `tests/capacity_probe_test.cpp` to `tests/CMakeLists.txt`. Add `FakeCapacityProbe` to `tests/directory_fixtures.h` alongside the existing fakes, recording calling thread and call order per path, with per-path results and a semaphore gate.
  - Check: `build/test/tests/files-smoke --gtest_filter='CapacityProbe.*'` passes asserting `StorageInfoCapacityProbe::measure` on a readable temp directory returns `valid=true` with `bytesTotal > 0` and `bytesAvailable <= bytesTotal`; a nonexistent path returns `valid=false` with both figures zero; and the returned available figure equals `QStorageInfo::bytesAvailable()` rather than `bytesFree()` for a path where the two differ (skip that assertion if no such mount is present).

- [x] T-003: `DevicesModel` — roles, `remove()`, confirmation removal
  - REQs: REQ-F-010, REQ-F-012, REQ-F-013, REQ-F-021 (model half), REQ-F-022, REQ-F-025, REQ-F-026, REQ-F-027, REQ-F-028, REQ-F-029
  - Note: drop roles `CanMount`/`CanUnmount`/`CanEject`/`CanPowerOff`; add `IconName`, `RemovalVerb`, `RemovalLabel`; add the `RemovalVerb` `Q_ENUM` mirror of `StorageOperation` (DESIGN §2.3) so QML can name its values. Delete `mount()`, `unmount()`, `eject()`, `requestPowerOff()`, `confirmPowerOff()`, `cancelPowerOff()`, `confirmationText` and `visibleTarget()`; add `remove()` and `findRow()`.
  - Check: `build/test/tests/files-smoke --gtest_filter='FilesStorage.*'` passes with `PowerOffRejectsChangedVolumeScopeWithoutPartialUnmount` invoking `remove()` directly (no confirmation step) and still asserting `ScopeChanged` with every affected volume left mounted; `MountOpensOnlyForCurrentActivationAndPreservesLocationOnFailure` passing **unmodified** (REQ-F-013); new cases asserting `remove()` on a verb-less row starts no backend request and leaves `errorMessage` empty (REQ-F-022), `remove()` on a busy row records no request (REQ-F-028), `remove()` dispatches `unmount`/`eject`/`powerOff` per the row's verb with `eject`/`powerOff` receiving `driveId` and `unmount` receiving `targetId`, the PowerOff call carries the scope derived at invocation (REQ-F-026), the model exposes no `confirmationText` property via `QMetaObject` (REQ-F-025), the model exposes no invokable named `mount` (REQ-F-012), and each `operationError` branch reaches `errorMessage` (REQ-F-029).

- [x] T-004: `DevicesModel` — visibility and grouping
  - REQs: REQ-F-015, REQ-F-016, REQ-F-017, REQ-F-018, REQ-F-019, REQ-F-020
  - Note: `appendEmptyDrives` gains `if (!drive.optical) continue;` after the existing media check. Group labels are computed model-side after the sort, as a per-`driveId` count pass.
  - Check: `build/test/tests/files-smoke --gtest_filter='FilesStorage.*'` passes with new `FakeStorage` fixtures asserting: a four-LUN reader sharing one `SiblingId` with no media yields zero rows, and yields exactly one row when a card is inserted in one slot (REQ-F-016); an optical drive with `mediaPresent=false`, `canEject=true` yields one drive-backed row whose verb is Eject and whose `canActivate` is false (REQ-F-017); an Internal drive with an unmounted data partition yields no row (REQ-F-015); a drive contributing two visible rows sets `groupLabel` to the drive name on both, and a drive contributing one row leaves `groupLabel` empty (REQ-F-018, REQ-F-019); and rows of one drive stay adjacent in the original non-overlapping-ID fixture. T-016 covers the missing overlapping-ID case (REQ-F-020).

- [x] T-005: `DevicesModel` — capacity acquisition and text
  - REQs: REQ-F-039, REQ-F-042, REQ-F-043, REQ-F-044, REQ-F-045
  - Note: cache keyed on mount point, generation counter, `QTimer` running only while rows exist. The mount point used is `volume.mountPoints.first()`, unchanged — see SPEC "Deferred to follow-up work" item 2; do **not** add canonical-mount-point handling.
  - Check: `build/test/tests/files-smoke --gtest_filter='FilesStorage.*'` passes asserting: every `FakeCapacityProbe::measure` call ran off the GUI thread (REQ-F-042); a mounted row reports `capacityValid=false` until the probe returns, then `true` with `capacityFraction` equal to `1 - available/total` (REQ-F-043, REQ-F-046 model half); `capacityText` is produced through a `tr()` format with two arguments and reads e.g. `"345 GB free of 1 TB"` for 1 TB total / 345 GB available (REQ-F-039); an unmounted row and an empty-optical row both report `capacityValid=false` (REQ-F-043); mounting a volume dispatches a query and unmounting clears the stored figures rather than leaving them stale (REQ-F-044); two volumes at the same mount point share one probe call; advancing past the named refresh interval dispatches a fresh query when none is in flight, and the timer stops when no mounted rows remain (REQ-F-045); and a gated probe leaves its row without capacity figures while another row resolves.

- [x] T-006: `SidebarNavigator`
  - REQs: REQ-F-049, REQ-F-050, REQ-F-051, REQ-F-052
  - Wiring: add `apps/files/presentation/sidebar_navigator.{h,cpp}` to `apps/files/CMakeLists.txt` and `tests/sidebar_navigator_test.cpp` to `tests/CMakeLists.txt`.
  - Check: `build/test/tests/files-smoke --gtest_filter='SidebarNavigator.*'` passes with both models populated, asserting: `moveDown()` from the last Places row lands on device index 0 and returns true (REQ-F-050); `moveUp()` from device index 0 lands on the last Places row and returns true (REQ-F-051); `moveDown()` on the last device row, `moveUp()` on the first Places row, and `moveDown()` on the last Places row with an empty Devices model all return false and leave the cursor unchanged (REQ-F-052); after every move exactly one `(section, index)` pair is set and the other section reports index `-1` (REQ-F-049); `revealRequested` is emitted once per successful move and not at all on a refused one; and the cursor clamps rather than dangles when the underlying model shrinks beneath it.

- [x] T-007: `DirectoryController` wiring
  - REQs: REQ-F-023
  - Note: add `navigator_` as a value member constructed after `places_`/`devices_`, the `sidebarNavigator` CONSTANT property, and the `sidebarActivationEnabled` property lifting the expression currently duplicated in `PlacesPanel.qml:13` and `DevicesPanel.qml:11`.
  - Check: `build/test/tests/files-smoke --gtest_filter='DirectoryController.*'` passes asserting `sidebarActivationEnabled` is true in Normal mode with no prompt and no Quick Look, and false in each of VISUAL mode, with a task prompt open, and with Quick Look open; and that `sidebarNavigator` returns a non-null object whose models match `places()` and `devices()`.

- [x] T-008: `SidebarPanel.qml` — one scroll area
  - REQs: REQ-F-030, REQ-F-031, REQ-F-032, REQ-F-033, REQ-F-053
  - Wiring: add `qml/places/SidebarPanel.qml` to `module_qml` in `apps/files/CMakeLists.txt`. `PlacesPanel` and `DevicesPanel` change from anchored-fill `Item` to content-sized: inner `ListView` gets `interactive: false`, `height: contentHeight`, loses its own `ScrollBar`, and sets `keyNavigationEnabled: false`. `Main.qml:97-113` collapses to one `SidebarPanel`.
  - Check: `build/test/tests/files-smoke --gtest_filter='PlacesWindow.*'` passes asserting: exactly one `ScrollBar` exists under the sidebar container (REQ-F-030); scrolling past the last Places row reaches device rows in the same flickable (REQ-F-030); the separator's `y` lies between the last Places row and the Devices heading at 220 px and 400 px sidebar widths (REQ-F-031); with an empty devices model neither separator nor Devices heading is visible and no vertical gap remains below the last Places row (REQ-F-032); sidebar `contentHeight` equals the sum of both sections' content heights plus separator and spacing within 1 px for 3-row and 30-row fixtures (REQ-F-033); no sidebar child binds its height to a proportion of the sidebar height (REQ-F-033); and crossing into Devices with a Places list long enough to overflow leaves the focused device row fully visible (REQ-F-053).

- [x] T-009: `DeviceRow` three-column composition
  - REQs: REQ-F-010, REQ-F-014, REQ-F-034, REQ-F-035, REQ-F-037, REQ-F-038, REQ-F-040, REQ-NF-002
  - Wiring: add `icons/eject-fallback.svg` and `icons/drive-fallback.svg` to the `foreach(icon ...)` list in `apps/files/CMakeLists.txt:113`.
  - Check: `build/test/tests/files-smoke --gtest_filter='PlacesWindow.*'` passes asserting: a device row contains no separator/divider/rule element between its columns (REQ-F-034); the icon's and the removal control's vertical centres each match the row's within 1 px for both a row with a capacity bar and one without (REQ-F-035, REQ-F-040); the content column absorbs sidebar width changes while columns one and three keep their implicit widths (REQ-F-038); External and Optical rows render exactly one visible removal-family button and no separate Unmount alongside it while mounted (REQ-F-010); External and Optical resolve the same eject glyph source (REQ-F-014); with the theme icon forced to fail every row still renders a visible fallback icon (REQ-F-037); and `Accessible.name` differs between a row whose verb is Eject and one whose verb is PowerOff (REQ-NF-002).

- [x] T-010: `CapacityBar.qml` — threshold colouring
  - REQs: REQ-F-046, REQ-F-047, REQ-F-048, REQ-C-005
  - Wiring: add `qml/places/CapacityBar.qml` to `module_qml` in `apps/files/CMakeLists.txt`.
  - Check: `build/test/tests/files-smoke --gtest_filter='PlacesWindow.*'` passes asserting: a fixture reporting 1 TB total / 345 GB available fills the bar to `1 - 345/1000` of its width within 1 px (REQ-F-046); the fill colour read back at used fractions 0.899, 0.90, 0.949, 0.95 and 0.999 equals `HoloniightPalette.primary`, `accentViolet`, `accentViolet`, `warning`, `warning` respectively, compared against the live palette singleton and never a literal (REQ-F-047, REQ-F-048); and a source scan finds no use of `Holonight.Controls`' `ProgressBar` in `apps/files/qml/places/` (REQ-C-005).

- [x] T-011: Keyboard — crossing, activation and `x`
  - REQs: REQ-F-021, REQ-F-022, REQ-F-023, REQ-F-024, REQ-F-054, REQ-NF-001
  - Note: key handling lives on the `SidebarPanel` `Flickable`; plain `x` is matched on `event.text`, leaving `Shift+X` unhandled.
  - Check: `build/test/tests/files-smoke --gtest_filter='PlacesWindow.*'` passes asserting: `j`/Down and `k`/Up cross both boundaries and are refused at both ends (REQ-F-054 via the navigator); Return, Enter and Space each navigate when a Places row holds focus and each activate when a device row holds focus (REQ-F-054); plain `x` on a focused External row reaches `powerOff`, on an Internal row reaches `unmount`, and on an Optical row reaches `eject` (REQ-F-021); `x` on a row without a removal control records no backend call and leaves `errorMessage` empty (REQ-F-022); `x` starts no operation in VISUAL mode, with a task prompt open, or with Quick Look open (REQ-F-023); `Shift+X` is left unaccepted by the panel (REQ-F-024); and every control the panel renders is reachable by keyboard (REQ-NF-001).

- [x] T-012: Documentation — amendments and README
  - REQs: (documentation of REQ-F-016, REQ-F-017, REQ-F-025, REQ-F-026, REQ-F-027)
  - Check: `docs/sdd/udisks2-storage/SPEC.md` records the R3 narrowing (empty optical drives shown, empty media-removable non-optical slots hidden) and the R4 change (confirmation removed, stale-scope rejection retained on an invocation-time scope), each pointing at this cycle. README documents the single removal action per device, the three device classes, that activation mounts, and the `x` keybinding.

- [x] T-013: Full verification run
  - REQs: all, and REQ-C-003 in particular
  - Check: `task build`, `task test`, `task format-check`, `task tidy` and `task qml-lint` all exit 0 on the final tree. No test reads real UDisks2 or real filesystem capacity other than `CapacityProbe.*` (T-002), which touches only a temp directory and a nonexistent path.

- [x] T-014: Manual native visual verification on Hyprland @1.5 (manual)
  - REQs: REQ-F-030, REQ-F-031, REQ-F-034, REQ-F-035, REQ-F-040, REQ-F-047
  - Note: offscreen rendering does not reproduce fractional-scale geometry; this must be run on the real display.
  - Check: The user confirms on the native 1.5-scale display that the sidebar scrolls as one area across Places and Devices with a single crisp separator between them, a device row shows a vertically centred icon, name, capacity bar and usage text with the removal button vertically centred at the right, the capacity bar colour matches the used fraction, and the device glyphs are legible — or, if they are not, that the only cause is the deferred upstream defect in `holonight-qt/docs/icons-recoloring-issue.md` and nothing in this cycle.

- [x] T-015: Close removal scope and empty-optical capability gaps found in implementation review
  - REQs: REQ-F-006, REQ-F-008, REQ-F-010, REQ-F-011, REQ-F-017
  - Check: A populated multi-slot reader with `canEject = false`, `canPowerOff = true` offers Unmount when mounted and never invokes PowerOff. A multi-drive enclosure with `mediaRemovable = false` also suppresses sibling-wide PowerOff. An empty optical drive with `canEject = false`, `canPowerOff = true` remains visible but offers no PowerOff control. Use the controller-derived scope for the presentation guard and repeat the check when `remove()` is invoked so a stale row cannot bypass it.

- [x] T-016: Guarantee drive-row contiguity for overlapping IDs
  - REQs: REQ-F-018, REQ-F-020, REQ-F-055
  - Check: Replace the concatenated `driveId + targetId` sort key with separate `(classRank, driveId, targetId)` fields. A `FakeStorage` fixture with drive IDs `a` and `ax`, interleaved backend rows, and target IDs chosen to interleave the old concatenated keys yields one contiguous run and one group label per drive.

---

## Requirement coverage

SPEC currently contains 64 requirements. T-015 and T-016 cover cases the initial fixtures missed.
Requirements appearing under
two tasks are split deliberately: a model half and a QML half.

| Task | Requirements |
|---|---|
| T-001 | F-001…009, F-011, F-036, C-001, C-002, C-004 |
| T-002 | F-041, F-042, C-006 |
| T-003 | F-010, F-012, F-013, F-021, F-022, F-025…029 |
| T-004 | F-015…020, F-055 (added after native review) |
| T-005 | F-039, F-042, F-043, F-044, F-045 |
| T-006 | F-049…052 |
| T-007 | F-023 |
| T-008 | F-030…033, F-053 |
| T-009 | F-010, F-014, F-034, F-035, F-037, F-038, F-040, NF-002 |
| T-010 | F-046, F-047, F-048, C-005 |
| T-011 | F-021…024, F-054, NF-001 |
| T-012 | documentation |
| T-013 | all, C-003 |
| T-014 | manual: F-030, F-031, F-034, F-035, F-040, F-047 |
| T-015 | F-006, F-008, F-010, F-011, F-017 |
| T-016 | F-018, F-020, F-055 |
