# Application Window Layout — Design

Status: Implemented (refined after visual review — see §8)
Spec: `docs/sdd/app-window-layout/SPEC.md`

## 1. Overview

This is a pure QML layout restructuring of `apps/files/Main.qml` and `apps/files/DirectoryListing.qml`
to match `docs/mockups/moc1.png`: a solid-surface header bar with a read-only breadcrumb, a
three-section content area (places sidebar | column-aligned file listing | preview sidebar), and the
`ModeStatusBar` relocated from the top of the window to the footer. No `DirectoryController`,
`DirectoryProxyModel`, or other C++ surface changes are made (SPEC REQ-C-001); every value displayed
already exists as a controller/model property. The two structurally interesting problems this design
solves are (1) keeping the header breadcrumb's left edge and the places sidebar's width mathematically
tied together so they cannot drift apart (SPEC REQ-F-003/REQ-C-003), and (2) reworking
`DirectoryListing`'s stacked-card delegate into three pixel-aligned columns shared with a new static
header row (SPEC REQ-F-007/REQ-F-008), without breaking the existing INSERT-mode inline rename/create
`TextField` overlay (`inlineEditor`).

## 2. Components

| File | Status | Responsibility |
|---|---|---|
| `apps/files/Main.qml` | Modified | Window shell: header → content (places/listing/preview) → footer. Owns the shared `sidebarWidth` property. Also owns the visual chrome (borders/backgrounds) for the sidebar, preview pane, and footer, via thin wrapper `Item`s/`Rectangle`s — see §8.2. |
| `apps/files/AppHeaderBar.qml` | **New** | Thin wrapper around `Holonight.Controls.HnHeaderBar` that renders the read-only, elided breadcrumb inside a pill-shaped container, left-inset to align with the listing's Name column. |
| `apps/files/DirectoryListing.qml` | Modified | Adds a static `directoryColumnHeader` row (Name/Size/Modified labels, with vertical `HnSeparator`s between cells) above the `ListView`; reworks the delegate's `contentItem` from a stacked title/subtitle/metadata card to three column-aligned cells; attaches a themed `ScrollBar`. Owns the shared column-width properties. |
| `apps/files/ModeStatusBar.qml` | Unchanged | Relocated verbatim into a footer wrapper (see §8.2) in `Main.qml`'s outer `ColumnLayout`; no internal edits. |
| `apps/files/PlacesPanel.qml` | Unchanged | Repositioned only; wrapped (not edited) in `Main.qml` to add a right-edge border and `surface` background — see §8.2. |
| `apps/files/PreviewPane.qml` | Unchanged | Repositioned only (stays inside the same `SplitView`); wrapped (not edited) in `Main.qml` to add a `surface` background — see §8.2. |

`AppHeaderBar.qml` is a new file rather than inlining the breadcrumb `Component` directly in `Main.qml`
— see §6 for rationale. No new singleton/constants file is introduced for the sidebar width; it lives as
a plain `property real` on the `HnApplicationWindow` instance (see §3). Column widths for the listing
are plain `readonly property` values on `DirectoryListing.qml`'s root `Item`, not a separate file — see
§6.

## 3. Data flow

### 3.1 Breadcrumb text

`DirectoryController.currentPath` (existing `Q_PROPERTY`, `NOTIFY changed`) is passed into
`AppHeaderBar.controller` (a `required property DirectoryController`, mirroring the pattern already used
by `PlacesPanel`/`PreviewPane`/`DirectoryListing`). Inside `AppHeaderBar`, the breadcrumb `HnLabel`'s
`rawText` binds directly to `root.controller.currentPath`; Qt's binding system re-evaluates it whenever
`DirectoryController::changed()` fires, matching how `ModeStatusBar.qml`'s `normalStatusLabel` already
consumes the same property (`apps/files/ModeStatusBar.qml:26`). No new signals are needed.

### 3.2 Shared sidebar width

A single `property real sidebarWidth: 200` is added to `window` (the `HnApplicationWindow { id: window }`
root object in `Main.qml`) — this is exactly the `window.sidebarWidth: 200` shape SPEC REQ-C-003 gives as
its own example. Two consumers bind to it:

- `PlacesPanel.Layout.preferredWidth: window.sidebarWidth` (replaces the current literal `200` at
  `apps/files/Main.qml:74`).
- `AppHeaderBar.sidebarWidth: window.sidebarWidth`, forwarded in as a `required property real` and used
  internally to compute the breadcrumb's left inset (§4.2).

Because both reads trace back to the one `window.sidebarWidth` property, changing it in one place (e.g.
a future settings/resize feature) automatically re-flows to both the sidebar's rendered width and the
breadcrumb's left inset in the same frame — no manual re-binding, satisfying REQ-C-003's acceptance
criteria verbatim.

**Why a plain window property and not a new singleton file:** there are exactly two consumers, both
already inside `Main.qml`'s scope (`PlacesPanel` is instantiated directly in `Main.qml`; `AppHeaderBar`
receives it as a passed-in property). A dedicated `pragma Singleton` file would be the right move if a
third, unrelated component needed the value (e.g. a settings dialog), but for two siblings under the same
parent it's unnecessary indirection. This is called out again as a rejected alternative in §6.

### 3.3 Column widths (Name/Size/Modified)

`DirectoryListing.qml`'s root `Item` (`id: root`) gains four `readonly property real` values:
`nameLeadingWidth` (fixed slot for the directory/file glyph), `sizeColumnWidth`, `modifiedColumnWidth`,
and `columnSpacing`. Both the new static header row and every delegate instance read these same four
properties by qualifying with `root.<name>` — delegates can already reach `root.<anything>` today (see
`apps/files/DirectoryListing.qml:85`, `:89`, `:108`, which all read `root.controller...` from inside the
`delegate:` `Component`), so `pragma ComponentBehavior: Bound` does not block this; it only requires that
per-item model data be declared as `required property`, which the delegate already does correctly for
`name`/`isDir`/`size`/`modified`/etc.

## 4. Interfaces

### 4.1 `AppHeaderBar.qml`

```qml
pragma ComponentBehavior: Bound
import QtQuick
import Holonight.Core
import Holonight.Controls

HnHeaderBar {
    id: root
    objectName: "appHeaderBar"

    required property DirectoryController controller
    required property real sidebarWidth

    readonly property real breadcrumbLeftInset: root.sidebarWidth + HnMetrics.internalSpacing(HnControlSize.Normal)
    readonly property real breadcrumbPadding: HnMetrics.horizontalPadding(HnControlSize.Compact)

    horizontalPadding: 0          // see §4.2 — inset math folds the padding term to zero by design
    dividerVisible: true
    dividerColor: HoloniightPalette.borderPassive

    Rectangle {
        anchors.fill: parent
        color: HoloniightPalette.surface   // REQ-F-001 solid surface fill
        z: -1
    }

    // The label is wrapped in a plain Item (rather than being the Component's root) because a
    // Loader binds an unsized root item's width/height to its own — a bare Label root would be
    // stretched to the full header height and render top-aligned instead of centered (see §8.1).
    content: Item {
        Rectangle {
            id: breadcrumbContainer                        // REQ-... visible breadcrumb container, §8.1
            objectName: "breadcrumbContainer"
            x: root.breadcrumbLeftInset - root.breadcrumbPadding
            anchors.verticalCenter: parent.verticalCenter
            width: Math.max(0, Math.min(breadcrumbLabel.implicitWidth + 2 * root.breadcrumbPadding,
                                         parent.width - x - HnMetrics.internalSpacing(HnControlSize.Normal)))
            height: HnMetrics.controlHeight(HnControlSize.Compact)
            radius: height / 2
            color: HoloniightPalette.surfaceRaised

            HnLabel {
                id: breadcrumbLabel
                objectName: "breadcrumbLabel"
                anchors.fill: parent
                anchors.margins: root.breadcrumbPadding
                verticalAlignment: Text.AlignVCenter
                role: HnTypographyRole.Body
                elide: Text.ElideMiddle                       // REQ-F-004
                textFormat: Text.PlainText
                rawText: root.controller.currentPath           // REQ-F-002
            }
        }
    }
}
```

Properties: `required property DirectoryController controller`, `required property real sidebarWidth`.
No signals — the breadcrumb is read-only by construction (a bound `Text`/`HnLabel`, no `MouseArea`, no
`TextInput`), which is how REQ-C-004's "no clickable breadcrumb segments" is enforced structurally rather
than by convention. The container's `width` is clamped to available header space (not just the label's
natural `implicitWidth`) so elision still activates correctly at narrow window widths — see §8.1.

### 4.2 Sidebar/breadcrumb pixel math

`Main.qml`'s outer `ColumnLayout` has no margins (`anchors.fill: parent`, spacing only), so the content
`RowLayout`'s left edge is at window x = 0. Inside it, `PlacesPanel` occupies `[0, sidebarWidth)`, then
one `RowLayout` spacing gap of `HnMetrics.internalSpacing(HnControlSize.Normal)` (the same constant
already used for that `RowLayout`'s `spacing:` at `apps/files/Main.qml:69`), then the `SplitView`
(containing `DirectoryListing`) begins flush at:

```
listingLeftEdge = sidebarWidth + HnMetrics.internalSpacing(HnControlSize.Normal)
```

`AppHeaderBar` sets its own `horizontalPadding: 0` (overriding `HnHeaderBar`'s default 12px content
inset), so its `content` `Loader` fills the bar edge-to-edge and the breadcrumb `HnLabel`'s
`anchors.leftMargin` is measured from the window's left edge directly. Setting that margin to
`root.sidebarWidth + HnMetrics.internalSpacing(HnControlSize.Normal)` — the exact same formula as
`listingLeftEdge` above, term for term — makes the alignment an algebraic identity rather than two
independently-tuned constants that happen to match today. This is the concrete mechanism behind REQ-F-003's
"plus any internal header padding" clause: that padding term is deliberately fixed at zero in this design
so it drops out of the formula instead of needing to be threaded through as a second shared value.

### 4.3 DirectoryListing column layout

`DirectoryListing.qml` root `Item` gains:

```qml
readonly property real sizeColumnWidth: 88
readonly property real modifiedColumnWidth: Math.ceil(modifiedColumnMetric.implicitWidth)
readonly property real columnSpacing: HnMetrics.internalSpacing(HnControlSize.Compact)

// Offstage label measuring the widest expected "yyyy-MM-dd HH:mm" rendering (all-digit fields, so
// "9" stands in for the widest glyph in every position), in the exact role/font the delegate's own
// Modified field renders with — see §8.3. Superseded a fixed `160` after visual review.
HnLabel {
    id: modifiedColumnMetric
    visible: false
    role: HnTypographyRole.Caption
    rawText: "9999-99-99 99:99"
}
```

There is no `nameLeadingWidth`/leading-glyph column — see §8.3 for why the original directory-glyph
slot was removed after visual review.

A new `directoryColumnHeader` `Rectangle` (REQ-F-007) is inserted above the `ListView`:

```qml
Rectangle {
    id: columnHeader
    objectName: "directoryColumnHeader"
    anchors { top: parent.top; left: parent.left; right: parent.right }
    height: HnMetrics.controlHeight(HnControlSize.Compact)
    color: "transparent"   // no fill — §8.4

    RowLayout {
        anchors.fill: parent
        anchors.leftMargin: HnMetrics.horizontalPadding(HnControlSize.Normal)
        anchors.rightMargin: HnMetrics.horizontalPadding(HnControlSize.Normal)
        spacing: root.columnSpacing

        HnLabel {
            objectName: "nameColumnHeader"
            role: HnTypographyRole.Body            // matches filename font — §8.4
            rawText: qsTr("Name")
            Layout.fillWidth: true
        }
        HnSeparator {
            orientation: Qt.Vertical
            color: HoloniightPalette.borderPassive // not the HnSeparator default — §8.5
            Layout.fillHeight: true
        }
        HnLabel {
            objectName: "sizeColumnHeader"
            role: HnTypographyRole.Body
            rawText: qsTr("Size")
            horizontalAlignment: Text.AlignRight
            Layout.preferredWidth: root.sizeColumnWidth
        }
        HnSeparator {
            orientation: Qt.Vertical
            color: HoloniightPalette.borderPassive
            Layout.fillHeight: true
        }
        HnLabel {
            objectName: "modifiedColumnHeader"
            role: HnTypographyRole.Body
            rawText: qsTr("Modified")
            Layout.preferredWidth: root.modifiedColumnWidth
        }
    }
    HnSeparator {
        color: HoloniightPalette.borderPassive
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
    }
}
```

`ListView`'s anchors change from `anchors.fill: parent` to
`anchors { top: columnHeader.bottom; left: parent.left; right: parent.right; bottom: parent.bottom }`.

The delegate's `contentItem` becomes a `RowLayout` with the **same** four cells, in the same order, with
the same `Layout.preferredWidth`/`Layout.fillWidth` values, read from the identical `root.*` properties:

```qml
contentItem: RowLayout {
    spacing: root.columnSpacing
    Item {                              // unchanged highlight-run rendering, just re-homed
        Layout.fillWidth: true
        implicitHeight: filenameRuns.implicitHeight
        clip: true
        Row { id: filenameRuns; Repeater { /* unchanged */ } }
    }
    HnLabel {
        objectName: "sizeColumnField"
        role: HnTypographyRole.Caption
        rawText: delegate.isDir ? "" : root.formatSize(delegate.size)
        color: HoloniightPalette.textSecondary
        horizontalAlignment: Text.AlignRight
        Layout.preferredWidth: root.sizeColumnWidth
    }
    HnLabel {
        objectName: "modifiedColumnField"
        role: HnTypographyRole.Caption
        rawText: delegate.statFailed ? delegate.statError
                                      : Qt.formatDateTime(delegate.modified, "yyyy-MM-dd HH:mm")
        color: delegate.statFailed ? HoloniightPalette.error : HoloniightPalette.textMuted
        elide: Text.ElideRight
        Layout.preferredWidth: root.modifiedColumnWidth
    }
    Loader { active: delegate.statFailed; visible: active; sourceComponent: errorIndicator }
}
```

Because both `RowLayout`s (header and every delegate) are given identical cell widths/spacing and both
stretch to the same overall width (`columnHeader`/`ListView` both span `DirectoryListing`'s full width;
each delegate is `width: listView.width`, unchanged), Qt Quick's layout engine resolves both to the same
per-cell x-offsets automatically — no manual pixel bookkeeping or fraction math is needed beyond keeping
the four `root.*` properties as the single source of truth. `delegate.title`/`subtitle`/`metadata` stay
bound exactly as today (`title: name`, `subtitle: ...`, `metadata: ...`) purely for
`HnListDelegate`'s/`HnSelectableDelegate`'s `Accessible.name`/`Accessible.description`, even though the
overridden `contentItem` no longer visually renders them via those properties (this mirrors the existing
file, where `contentItem` is already overridden and `title`/`subtitle`/`metadata` already serve
accessibility only — not a new pattern this design introduces).

The `inlineEditor` `TextField` (REQ-F overlay, `apps/files/DirectoryListing.qml:177-211`) is untouched:
it is a sibling of `contentItem` inside the delegate (not inside the `RowLayout`), anchored to
`parent.left`/`parent.right` where `parent` is the delegate item itself, not `contentItem`. Delegate width
(`listView.width`) is unchanged by this rework, so the overlay continues to span the full row exactly as
before, covering the new column-aligned cells the same way it previously covered the stacked card.

## 5. Key decisions with rationale

1. **Sidebar width lives as `window.sidebarWidth`, a plain property on the `HnApplicationWindow`
   instance in `Main.qml`.** Two consumers, both direct children of `window`'s scope; a singleton file
   would be premature generalization (see §6 for the rejected alternative).

2. **The header is its own file, `AppHeaderBar.qml`, not inlined in `Main.qml`.** `Main.qml` already
   composes several sibling regions (content `RowLayout`, keybinding hints `Row`, `QuickLookOverlay`);
   adding the breadcrumb's elision/margin logic as a fourth inline block would make the file's structure
   harder to scan. A named component also gives the breadcrumb a stable `objectName` (`appHeaderBar`) and
   a narrow, typed interface (`controller`, `sidebarWidth`) that a test can construct or query in
   isolation, consistent with how `PlacesPanel`/`PreviewPane`/`DirectoryListing` are already factored out
   of `Main.qml` rather than inlined.

3. **Column widths stay in sync via `root.*` `readonly property real` values on `DirectoryListing.qml`,
   read by both the static header row and every delegate's `contentItem`**, rather than a shared
   fraction-based singleton. `RowLayout` already resolves identical `Layout.preferredWidth`/
   `Layout.fillWidth` inputs to identical per-cell offsets given identical total width — the "shared
   column-width object" the SPEC prompt anticipates is just these four properties plus the fact that
   both `RowLayout`s span the same width. No fraction math, no manual x-coordinate computation.

4. **The inline rename/create `inlineEditor` overlay is preserved by construction, not by special-casing
   it against the new columns.** It anchors to the delegate item (`parent.left`/`parent.right`), not to
   `contentItem` or any individual column, so it is agnostic to how many columns `contentItem` has or how
   they're arranged. The only invariant that matters is that the delegate's own `width` stays
   `listView.width`, which this design does not change.

5. **Breadcrumb elision: `Text.ElideMiddle`, not custom truncation.** This matches the existing
   convention for the same data (`DirectoryController.currentPath`) in
   `ModeStatusBar.qml:23` (`normalStatusLabel`'s `elide: Text.ElideMiddle`), which already elides paths
   the same way. Middle elision keeps both the drive/root segment and the leaf directory name visible,
   which is more informative for a path than `ElideRight` (which would just chop off everything after
   some prefix). REQ-F-004 leaves the exact strategy as an implementation detail; reusing the existing
   convention avoids introducing a second, inconsistent elision behavior for the same underlying string.

6. **The footer is `ModeStatusBar` relocated verbatim, with no wrapping `HnHeaderBar` or `Item`.**
   REQ-F-010 requires the component's "properties... layout properties, text content, styling" to be
   unmodified, and REQ-NF-002 explicitly states the footer "does not require palette changes" (unlike
   the header, which REQ-F-001 explicitly requires to be an opaque, palette-driven surface). Reusing
   `HnHeaderBar` for the footer would require flipping its divider from bottom to top, which the shared
   component does not support (its `HnSeparator` is hard-anchored to `bottom: parent.bottom` — a
   sibling-repo file this project must not modify) — and would add a solid background the SPEC never asks
   for. The literal, lowest-risk reading of REQ-F-010 is: keep `ModeStatusBar`'s existing
   `RowLayout { Layout.fillWidth: true; Layout.margins: ... }` container exactly as written today, and
   simply move that block to after the content `RowLayout` in `Main.qml`'s outer `ColumnLayout`.

7. **Relative order of the footer and the static keybinding-hints `Row`.** Non-goal #7 leaves this
   unspecified. This design places `ModeStatusBar` as the last child of the outer `ColumnLayout` (i.e.
   the true bottom-most row), with the existing keybinding-hints `Row` immediately above it — preserving
   the hints row's current visual position directly under the main content area, while making
   `ModeStatusBar` the literal footer per the header→content→footer hierarchy in REQ-F-005.

8. ~~A fixed-width leading glyph slot (`nameLeadingWidth`) is reserved in the Name column, shown as `▸`
   for directories and empty for files~~ — **reverted after visual review** (§8.3). The glyph wasn't
   requested and was removed, along with its reserved column, from both the header and the delegate.
   REQ-F-008's "name (with directory/file icon if applicable)" is satisfied by the name text alone; no
   icon asset pipeline exists in `apps/files` (no `.qrc`, no `HnIcon` usage), so a real per-type icon
   remains future work, not part of this cycle.

## 6. Alternatives considered

- **Anchors-based breadcrumb alignment (bind `anchors.left` directly to `placesPanel.right`) instead of
  a shared `sidebarWidth` property.** Rejected: `PlacesPanel` and `AppHeaderBar` are siblings in
  different branches of the item tree once the header sits above the content `RowLayout` (header is not
  inside the same `RowLayout` as `PlacesPanel`), so a direct anchor would need to reach across the
  `ColumnLayout` (`placesPanel.parent.parent...`), which is exactly the kind of fragile, implicit
  coupling REQ-C-003 is written to rule out. A named, explicit shared property is more discoverable and
  is what REQ-C-003's own acceptance criteria describe.

- **A dedicated `SidebarMetrics`/`LayoutConstants` singleton (`pragma Singleton`) holding `sidebarWidth`
  (and possibly the column widths too) instead of a window property + `DirectoryListing`-local
  properties.** Considered because it would generalize cleanly if a third consumer appeared. Rejected for
  now: it adds a new file and a new QML type registration for values that, today, have exactly two
  consumers each, both already reachable through direct property passing. If a future SPEC cycle adds a
  third consumer (e.g., a resizable-sidebar settings panel), promoting `window.sidebarWidth` to a
  singleton is a small, non-breaking refactor at that point — YAGNI in the meantime.

- **`QML TableView` instead of manually column-aligned `RowLayout` delegates for
  `DirectoryListing`.** Rejected: `TableView` is a rewrite of `DirectoryListing`'s selection, keyboard
  navigation (`InspectionKeys.js`), highlight-run search rendering, and the INSERT-mode inline editor
  overlay — none of which `TableView` provides for free, and all of which the SPEC (REQ-C-002) requires
  to keep working unchanged. `TableView` also models rows/columns as a 2D delegate grid, which does not
  map onto the existing single `HnListDelegate`-per-row structure without a much larger restructuring
  than "column-align the existing delegate." The manually-aligned `RowLayout` approach is a much smaller
  diff that satisfies REQ-F-008 exactly.

- **A separate `HeaderBreadcrumb.qml` component (just the label, wrapped by an ad-hoc `Rectangle`+
  `HnSeparator` in `Main.qml`) instead of wrapping `HnHeaderBar`.** Rejected: `HnHeaderBar` already
  provides the fixed `HnMetrics.headerHeight` height, the `content: Component` slot, and an optional
  bottom divider — reimplementing that in `Main.qml` would duplicate what `HnHeaderBar` already does and
  risk a subtly different height/divider treatment than other Holonight-family apps that use
  `HnHeaderBar` for their own headers. Wrapping it (`AppHeaderBar.qml`) instead reuses the design-system
  component per REQ-NF-002 and only adds the one thing `HnHeaderBar` doesn't provide out of the box: a
  solid background (it is transparent by default) and the sidebar-aware left inset.

## 7. Known risks

- **`tests/smoke.cpp`** directly asserts on delegate/overlay structure that this design's `contentItem`
  rework touches:
  - `TEST(Files, PopulatedWindowKeyboardAndInlineError)` (`tests/smoke.cpp:31`) reads
    `item->property("title")` off the current-item delegate and compares it to the model's `NameRole`
    text (`tests/smoke.cpp:65-66`). Since `title` stays bound to `name` for accessibility (§4.3), this
    should keep passing, but it is exercising the exact property this design repurposes from
    "visually rendered" to "accessibility-only" — worth an explicit look during implementation.
  - `TEST(Files, ModalEditingWindowKeyboardAndHighlighting)` (`tests/smoke.cpp:462`) asserts
    `editor->objectName() == "inlineNameEditor"` and walks `window->contentItem()->childItems()`
    recursively hunting for `objectName() == "filenameRun"` labels to check search-highlight
    bold/color state (`tests/smoke.cpp:539-553`). Both object names are preserved unchanged by this
    design (`filenameRuns`/`filenameRun` `Repeater` is moved, not renamed; `inlineEditor`'s
    `objectName: "inlineNameEditor"` is untouched), but this test is the most likely to break if the
    `filenameRuns` `Row` is accidentally renamed or dropped during the `contentItem` rewrite.
  - This same test also calls `capture(...)` (`tests/smoke.cpp:480-486`), which does
    `window->grabWindow().save(...)` into `$FILES_CAPTURE_PREFIX-modal-{insert,search}.png` — these are
    the "insert" and "search" state screenshots that `scripts/check-visual.sh` collects into
    `build/visual/{dark,light}-{1,1.25,1.5}-modal-{insert,search}.png` for every theme/scale combination
    it runs (`Files.*Window*` gtest filter, six theme/scale combinations). Every one of those PNGs will
    visually differ after this change (header bar, column layout, relocated footer all change pixels)
    and needs regenerating/re-reviewing; this is exactly the risk SPEC REQ-NF-001 flags for
    `build/review-visual/` and `build/native-matrix/` (referenced by SPEC.md; not currently produced by
    any script in this repo, so likely produced by the umbrella/CI pipeline — confirm their generation
    path before assuming they update automatically).

- **`tests/directory_performance_test.cpp`**'s `hasVisibleDelegate()` helper
  (`tests/directory_performance_test.cpp:24-33`) walks `childItems()` looking for
  `item->objectName() == "directoryEntryDelegate"` with a non-empty `title` property and positive
  `width`/`height` intersecting the viewport. `objectName: "directoryEntryDelegate"` and the `title`
  binding are both preserved by this design, so this should keep passing, but the delegate's rendered
  `height` will change slightly (single-line column row vs. two-line stacked card) — worth confirming the
  opt-in `FILES_BROWSE_BENCHMARK=1` native-Wayland benchmark still finds intersecting delegates at the
  new (likely shorter) row height, since `HnMetrics.controlHeight`/row height math is being touched
  indirectly through the `contentItem` change even though no explicit height override is added here.

- **`tests/directory_controller_test.cpp`, `tests/directory_controller_file_ops_test.cpp`,
  `tests/vim_mode_controller_test.cpp`** were checked and do not appear to instantiate or inspect
  `Main.qml`/`DirectoryListing.qml` at all (they exercise `DirectoryController`/`VimModeController`
  directly, headless) — SPEC REQ-NF-001 lists them defensively, but this design does not expect them to
  need changes; confirm during implementation rather than assuming.

- **Places-sidebar-width regression surface**: any test or fixture that currently hardcodes `200` as an
  expected `PlacesPanel` width (in pixels, e.g. via `item->width()` assertions) will still pass unchanged
  since `window.sidebarWidth` defaults to `200` — the risk is purely that such an assertion, if it
  exists, is now checking a value that flows through a binding rather than a literal, so a future width
  change would need the test updated too. No such assertion was found in the files read for this design,
  but it's worth a repo-wide grep for a literal `200` in `tests/` before implementation.

## 8. Refinements after visual review (2026-09-11)

T-001 through T-012 shipped against §1–§7 above and passed `task check` end to end. A subsequent visual
review against `docs/mockups/moc1.png` on the running app found several gaps between the design and its
actual rendered appearance; this section records what changed and why, without editing the historical
rationale in §5–§7 above beyond the two corrections already struck through inline.

### 8.1 Breadcrumb vertical centering and visible container

**Bug found:** the breadcrumb text rendered noticeably above center in the header, not on
`root.controller.currentPath`'s baseline as §4.1's original code intended.

**Root cause:** `HnHeaderBar`'s internal `Loader` (`horizontalPadding`/`verticalPadding`-derived
`anchors.fill` + margins) has an *explicit* size, and Qt Quick's default two-way Loader/item size binding
then stretches an unsized loaded root item to match it. The original `content: Component { HnLabel {
anchors.verticalCenter: parent.verticalCenter } } }` used the `HnLabel` itself as that root — since the
label set no explicit `height`, the Loader stretched it to the full content height, and `Label`'s
inherited default `verticalAlignment` (`Text.AlignTop`) then rendered the text at the top of that
stretched box. `anchors.verticalCenter: parent.verticalCenter` was centering the (now-full-height) *item*
correctly; the *text inside it* was the part left top-aligned.

**Fix:** wrap the label in a plain `Item` (the `content` Component's root is now `Item`, not `HnLabel`) —
this matches the pattern already used by the closest sibling consumer,
`holonight-viewer/apps/viewer/Main.qml:357-367` (`content: Item { HnLabel { anchors.centerIn: parent }
... } }`). A generic `Item` has no built-in text-alignment default to fight, so centering a child inside
it (here, positioning the pill `Rectangle` via `anchors.verticalCenter`) works regardless of whatever size
the Loader binds onto the wrapper.

**Also addressed in the same fix:** SPEC review flagged "no visible breadcrumbs container" — the mockup
shows the path inside a rounded pill, not as bare text on the header surface. `breadcrumbContainer` (a
`Rectangle`, `radius: height / 2`, `color: HoloniightPalette.surfaceRaised`) was added inside that wrapper
`Item`. Its `x` is set to `root.breadcrumbLeftInset - root.breadcrumbPadding` so the *text* (not the pill
edge) still lands exactly on the REQ-F-003 alignment point — the pill's internal padding is subtracted
back out of its position, not added on top of it. Its `width` is clamped to
`Math.min(label.implicitWidth + padding, availableSpace)` rather than left as pure content-fit, so REQ-F-004
elision still activates at narrow window widths instead of the pill overflowing the header.

### 8.2 Explicit region chrome: sidebar/preview borders, all-region `surface` backgrounds, footer wrapper

None of §1–§7 specified visible borders between the header/sidebar/listing/preview/footer regions or a
consistent `surface` fill across them; the mockup implies both. Rather than edit `PlacesPanel.qml`,
`PreviewPane.qml`, or `ModeStatusBar.qml` (all three remain **Unchanged** components per REQ-C-002/REQ-F-006/
REQ-F-009/REQ-F-010), `Main.qml` now wraps each in a plain `Item`/`Rectangle` that owns the chrome and
positions the untouched component via `anchors.fill`:

- **Sidebar**: `PlacesPanel` sits inside a new `sidebarContainer` `Item` (`Layout.preferredWidth:
  window.sidebarWidth`, replacing the direct `Layout.preferredWidth` binding that used to live on
  `PlacesPanel` itself) with a `surface`-colored `Rectangle` behind it and a vertical `HnSeparator` pinned
  to its right edge.
- **Preview pane**: `PreviewPane` sits inside a new `previewContainer` `Item` (the `SplitView.preferredWidth`/
  `SplitView.minimumWidth` attached properties move from `PreviewPane` onto this wrapper, since SplitView
  attached properties must be set on its direct children) with the same `surface` background. Its *left*
  border is the SplitView handle itself — see §8.6 — not a second separate line.
- **Footer**: `ModeStatusBar` sits inside a new `footerBar` `Rectangle` (`surface` background,
  `implicitHeight: modeStatusBar.implicitHeight + 2 * HnMetrics.internalSpacing(Normal)`, since a
  `RowLayout`-rooted component like `ModeStatusBar` reports a valid `implicitHeight` even outside a Layout
  parent) with a top `HnSeparator`. `ModeStatusBar`'s own `Layout.fillWidth`/`Layout.margins` bindings are
  replaced by `anchors.fill` + `anchors.margins` on the wrapper, since the wrapper is no longer inside a
  `Layout` — `ModeStatusBar.qml` itself is not touched.

This keeps the REQ-C-002/REQ-F-006/REQ-F-009/REQ-F-010 "unchanged component" guarantee intact: all three
files are byte-for-byte what §2's original table shipped, and the wrapper items exist purely so `Main.qml`
can own chrome that none of those three components' original responsibilities included.

### 8.3 Directory glyph removed; Modified column width now measured, not guessed

Decision 8 in §5 (reproduced struck-through above) added a `▸` glyph and its reserved
`nameLeadingWidth` column to satisfy REQ-F-008's "name (with directory/file icon if applicable)" clause.
Visual review found this unrequested and asked "what is the triangle glyph... it wasn't requested" — it
is removed from both the delegate and the header's matching spacer, which also fixed a reported "too big
left padding" before the Name column (the reserved 20px glyph slot plus its `columnSpacing` gap was the
excess).

Separately, `modifiedColumnWidth` was a guessed constant (`160`). It is now
`Math.ceil(modifiedColumnMetric.implicitWidth)`, measured off an offstage `HnLabel` (`visible: false`,
`role: HnTypographyRole.Caption`, `rawText: "9999-99-99 99:99"`) — the same role/font the delegate's own
Modified field renders with, so the measurement can't drift from the rendered font by hand-copying family/
size/weight separately. `"9999-99-99 99:99"` was chosen over the reviewer's suggested `"9999-99-99"`
because the delegate actually renders `Qt.formatDateTime(modified, "yyyy-MM-dd HH:mm")` — the time portion
is real rendered content the column must also fit.

### 8.4 Column header typography: `Body` role instead of `MicroHeader`

Review found the column header's font family, size, and case all wrong: `MicroHeader` (§4.3's original
role choice) maps to `HolonightTheme.titleFont`, `microSize`, and `Font.AllUppercase` via `HnLabel.qml`'s
role switch — producing a different, smaller, all-caps rendering ("NAME") instead of the requested
"regular font, same size as filenames, Name/Size/Modified sentence case". Switching the three header
labels' `role` to `HnTypographyRole.Body` (the same role the delegate's own name field uses) fixes all
three complaints in one change, since family/size/capitalization are all driven by the same role switch.

### 8.5 `HnSeparator`'s default color is too low-contrast for these dividers

Every `HnSeparator` newly added in §8.2/§8.4 (sidebar right edge, footer top edge, the two vertical
separators between Name/Size/Modified) was, in its first pass, left at `HnSeparator`'s own default
`color: HoloniightPalette.borderSubtle` (`#263241` in the `holonight-dark` scheme) — chosen implicitly by
omission, not deliberately. Pixel-level inspection of the rendered `task visual-check` output showed this
was the actual reason several of these borders read as "not there" on review: `borderSubtle` differs from
the `surface`/`background` tones around it by roughly half the contrast of `borderPassive`
(`#36465A`), which is what `HnHeaderBar`'s own built-in divider already uses (§4.1's `dividerColor:
HoloniightPalette.borderPassive`, carried over from the design-system default). All newly added
separators now explicitly set `color: HoloniightPalette.borderPassive` to match — a consistent divider
language across header/sidebar/column-header/footer, rather than one visible line (the header's) and
several barely-visible ones (everything added in this cycle).

### 8.6 Custom thin `SplitView` handle

Qt Quick Controls' default `SplitView` handle is a wide (visually multi-pixel) drag grip. Review asked for
a 1px visible divider between the listing and preview pane while preserving drag-to-resize. `SplitView.handle`
is now a custom `Item` (`implicitWidth: HnMetrics.internalSpacing(Compact) + HnMetrics.separatorWidth`,
giving a comfortable ~9px hit region) containing a single centered 1px `Rectangle`
(`color: SplitHandle.pressed ? borderActive : (SplitHandle.hovered ? borderHover : borderPassive)`, using
the `SplitHandle` attached property Qt Quick Controls provides on handle delegates). The wide hit region
stays fully interactive (`SplitView` treats the whole handle item's bounds as the drag target, not just
its visible pixels) — confirmed by `tests/smoke.cpp`'s `InspectionImageSplitterAndPixelSizing`, which
computes its drag point as the midpoint between the listing's right edge and the preview pane's left edge
and continued passing unmodified.

### 8.7 Other changes

- **Scrollbar**: the `ListView` had no `ScrollBar.vertical` attached, so overflowing directories scrolled
  with no visible affordance. `ScrollBar.vertical: ScrollBar {}` (the `Holonight`-styled scrollbar, already
  used elsewhere via `import Holonight`) was added with its default `AsNeeded` policy — it fades in on
  hover/active scroll, matching the design system's other list views (e.g.
  `holonight-ai/qml/shared/BottomAnchoredListView.qml`), rather than `AlwaysOn`.
- **Keybinding-hints row removed**: SPEC non-goal #7 explicitly left this row's fate "an implementation
  detail." Review noted it isn't in the mockup; it's removed from `Main.qml` entirely rather than merged
  into the footer or kept.

### Review fixes: responsive columns and shared alignment

The approved review fixes preserve filename space before metadata: hide Modified,
then Size, when the listing cannot fit those columns plus 120 logical pixels for
Name. Headers and delegates use identical widths and visibility. Header dividers
are painted inside the column labels rather than participating in the layout.
Stat-error indicators occupy the Name cell so they do not shift metadata columns.
The breadcrumb inset includes the listing's normal horizontal padding.
These implement REQ-F-003, REQ-F-007/008 and narrow-pane usability in REQ-F-012.
