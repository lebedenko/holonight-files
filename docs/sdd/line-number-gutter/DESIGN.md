# Line-Number Gutter in Directory Listing — Design

Status: Draft
Spec: `docs/sdd/line-number-gutter/SPEC.md`

## Spec revisions folded in

After the first draft the user decided three things, now reflected in SPEC.md and below:

1. **Breadcrumb aligns with the list view's left edge** (SPEC.md REQ-F-015), no longer with the icon
   column (§5.1).
2. **`showSize`/`showModified` thresholds stay exactly as they are** (SPEC.md non-goal 8). Narrow-window
   layout is deferred to a follow-up SDD cycle (§5.3).
3. **Default minimum gutter width is that of `"999"`**: digits = `max(3, String(count).length)`
   (SPEC.md REQ-F-004/REQ-C-004).

## 1. Overview

This feature adds a first "column" — a Vim-hybrid-numbered gutter — to
`apps/files/DirectoryListing.qml`'s header row and every delegate row. It is pure QML: a shared
`readonly property real lineNumberGutterWidth` measured off a hidden `HnLabel` (mirroring the
existing `modifiedColumnMetric` pattern), a per-delegate `HnLabel` bound to `delegate.index` and
`root.controller.cursorRow`, and a matching blank spacer in the header. The gutter replaces the row's
leading `columnPadding` by zeroing the delegate's `leftPadding` and the header RowLayout's
`anchors.leftMargin`, so the gutter itself — not generic padding — is what now sits flush at the
row's left edge. No `DirectoryModel`/`DirectoryController` change (REQ-C-003); no new C++ at all.

## 2. Component inventory

| File | Status | Responsibility |
|---|---|---|
| `apps/files/DirectoryListing.qml` | Modified | New `lineNumberGutterDigits`/`lineNumberGutterWidth` root properties and hidden metric `HnLabel` (REQ-F-004/REQ-NF-001/REQ-C-002/REQ-C-004). New header spacer `Item` (`lineNumberGutterHeader`, REQ-F-009). New delegate gutter `HnLabel` (`lineNumberGutterField`, REQ-F-001/002/003/REQ-C-001/REQ-NF-004). Delegate `leftPadding: 0` and header `anchors.leftMargin: 0` (REQ-F-005). `showSize`/`showModified` deliberately unchanged (§5.3). `inlineNameEditor` anchor change (REQ-F-011). |
| `apps/files/AppHeaderBar.qml` | Modified | `breadcrumbLeftInset` formula changed to track the list view's left edge instead of the icon column (§5.1, REQ-F-015). |
| `apps/files/Main.qml` | Unaffected | No change: the list view's x is already `sidebarWidth + internalSpacing(Normal)` by Main.qml's content `RowLayout`, which AppHeaderBar can derive from the `sidebarWidth` it already receives. |
| `tests/smoke.cpp` | Described only (§9), not written here | New assertions for gutter structure, numbering, reactivity, width, alignment, placeholder rows, empty/error state, inline-editor position; one existing assertion (`WindowColumnAlignmentAndNarrowNames`'s breadcrumb-vs-icon check) updated per §5.1. |
| `tests/directory_performance_test.cpp` | Unaffected (asserted, not modified) | Existing budgets already exercise per-cursor-move delegate rebinding (§5.5); REQ-NF-002 requires it keep passing unmodified. |

Explicitly **not** touched, per SPEC.md's non-goals: `PlacesPanel.qml`, `PreviewPane.qml`,
`QuickLookOverlay.qml`, `DirectoryModel`, `DirectoryController`, `DirectoryProxyModel`,
`VimModeController`, `TaskManager`.

## 3. Data flow

```
listView.count (ListView, bound to root.controller.listing — includes any o/O placeholder row)
    │
    ▼
root.lineNumberGutterDigits = Math.max(3, String(listView.count).length)     // REQ-C-004
    │
    ▼
lineNumberGutterMetric.rawText = "9".repeat(root.lineNumberGutterDigits)     // hidden HnLabel,
    │                                                                          role: Code (REQ-NF-001)
    ▼
root.lineNumberGutterWidth = Math.ceil(lineNumberGutterMetric.implicitWidth) + 16   // REQ-F-004/REQ-C-002
    │
    ├──────────────────────────────┬───────────────────────────────────────────┐
    ▼                               ▼                                           ▼
header spacer Item                 delegate gutter HnLabel                     inlineNameEditor
  width: lineNumberGutterWidth       Layout.preferredWidth: lineNumberGutterWidth anchors.leftMargin:
  (lineNumberGutterHeader,           text: delegate.index === cursorRow           lineNumberGutterWidth
   REQ-F-009)                              ? (delegate.index+1) : |Δ|             (REQ-F-011)
                                      color: cursorRow match ? accentViolet
                                             : textMuted
                                      (lineNumberGutterField, REQ-F-001/002/003)

root.controller.cursorRow (DirectoryController, existing) ──> read by every live delegate's gutter
  HnLabel binding, alongside delegate.index (required property, bound from model.index) — REQ-C-003.
  Only instantiated (visible + cacheBuffer) delegates hold live bindings; ListView recycles/destroys
  the rest, so a cursor move re-evaluates a bounded number of labels, not the whole model (§5.5).
```

## 4. Interfaces

### 4.1 New root properties (`DirectoryListing.qml`)

```qml
// REQ-C-004: string-length based, not log10; includes the o/O placeholder row via listView.count.
function lineNumberGutterDigitsFor(rowCount) {
    return Math.max(3, String(rowCount).length);
}
readonly property int lineNumberGutterDigits: lineNumberGutterDigitsFor(listView.count)

// REQ-C-002: shared read-only width, following the iconColumnWidth/sizeColumnWidth/
// modifiedColumnWidth/columnSpacing/columnPadding pattern already on this root.
readonly property real lineNumberGutterWidth: Math.ceil(lineNumberGutterMetric.implicitWidth) + 16
```

`lineNumberGutterDigitsFor` is declared as a standalone function (not inlined into the property
binding) specifically so a C++ test can call it directly via `QMetaObject::invokeMethod` with
synthetic row counts the real ListView never has to hold — see §5.4.

### 4.2 Hidden reference label (mirrors `modifiedColumnMetric`)

```qml
HnLabel {
    id: lineNumberGutterMetric
    visible: false
    role: HnTypographyRole.Code
    rawText: "9".repeat(root.lineNumberGutterDigits)
}
```

Declared next to `modifiedColumnMetric`, above `columnHeader`. `lineNumberGutterWidth`'s forward
reference to `listView` (declared later in the file) and to this label mirrors the file's existing
`modifiedColumnWidth` → `modifiedColumnMetric` forward reference; QML resolves ids after the full
tree is parsed, so declaration order does not matter here, exactly as it already does not for
`modifiedColumnWidth`.

### 4.3 Header spacer (`columnHeader`'s `RowLayout`, new first child)

```qml
Item {
    objectName: "lineNumberGutterHeader"
    Layout.preferredWidth: root.lineNumberGutterWidth
    Layout.minimumWidth: root.lineNumberGutterWidth
    Layout.maximumWidth: root.lineNumberGutterWidth
    Layout.fillHeight: true
    // No label, no HnSeparator (REQ-F-009: blank, unlike the Size/Modified column separators).
}
```

`columnHeader`'s `RowLayout.anchors.leftMargin` changes from `HnMetrics.horizontalPadding(HnControlSize.Normal)`
to `0` (§5.2); `anchors.rightMargin` is unchanged.

### 4.4 Delegate gutter cell (`contentItem`'s `RowLayout`, new first child)

```qml
HnLabel {
    id: gutterLabel
    objectName: "lineNumberGutterField"
    role: HnTypographyRole.Code
    rawText: (delegate.index === root.controller.cursorRow)
             ? (delegate.index + 1).toString()
             : Math.abs(delegate.index - root.controller.cursorRow).toString()
    color: delegate.index === root.controller.cursorRow
           ? HoloniightPalette.accentViolet
           : HoloniightPalette.textMuted
    horizontalAlignment: Text.AlignRight
    verticalAlignment: Text.AlignVCenter
    leftPadding: 8
    rightPadding: 8
    textFormat: Text.PlainText
    Layout.preferredWidth: root.lineNumberGutterWidth
    Layout.minimumWidth: root.lineNumberGutterWidth
    Layout.maximumWidth: root.lineNumberGutterWidth
    Layout.fillHeight: true
    Accessible.ignored: true
}
```

Placed before `iconCell` in the delegate's `contentItem: RowLayout`. `HnLabel` is a `Label`
(`QtQuick.Controls.Basic`), which has `leftPadding`/`rightPadding`; setting both to 8 and binding the
cell's width to `lineNumberGutterWidth` (which already includes the same 16px) means the label's
drawable (post-padding) area exactly equals `lineNumberGutterMetric`'s measured text width — a
smaller number simply renders closer to the right edge, inside the same fixed-width cell, matching
REQ-F-005's flush-left/8px-left/8px-right/constant-width requirements without a second wrapping
`Item`. `Accessible.ignored: true` and no `activeFocusOnTab` satisfy REQ-NF-004; this label is never
placed in any `Keys.on*` or focus chain.

The delegate's own `leftPadding: 0` (new; `HnListDelegate`/`HnSelectableDelegate`'s default
`padding: HnMetrics.horizontalPadding(root.resolvedSizeRole)` is overridden on just this axis,
`rightPadding` untouched) is what lets `gutterLabel` sit flush at the delegate's left edge — see §5.2
for why this is safe with the shared selection background.

### 4.5 Inline editor (`inlineNameEditor`)

```qml
TextField {
    id: inlineEditor
    objectName: "inlineNameEditor"
    anchors.left: parent.left
    anchors.right: parent.right
    anchors.verticalCenter: parent.verticalCenter
    anchors.leftMargin: root.lineNumberGutterWidth   // was: part of anchors.margins: 8
    anchors.rightMargin: 8                           // unchanged
    ...
}
```

### 4.6 `showSize`/`showModified` — unchanged (§5.3)

Both expressions stay byte-for-byte as they are today (SPEC.md non-goal 8, REQ-C-005).

### 4.7 `AppHeaderBar.qml` (§5.1, REQ-F-015)

```qml
// Breadcrumb text starts at the list view's left edge (Main.qml content RowLayout: sidebar + spacing).
readonly property real breadcrumbLeftInset: root.sidebarWidth + HnMetrics.internalSpacing(HnControlSize.Normal)
```

(Drops the `+ HnMetrics.horizontalPadding(HnControlSize.Normal)` term, which previously matched the
row's leading padding that the gutter now replaces.)

## 5. Key decisions with rationale

### 5.1 Breadcrumb alignment — decided: list view's left edge

Today `breadcrumbLeftInset` lands on the listing's icon-column x (`main-view-icons` REQ-NF-002,
enforced within 1px by `WindowColumnAlignmentAndNarrowNames`), because the icon column used to be the
row's leading content edge. The gutter moves the icon column right, so the breadcrumb needed a new
anchor. **User decision:** align it with the list view's left edge (SPEC.md REQ-F-015).

`breadcrumbLeftInset = sidebarWidth + internalSpacing(Normal)`, which is exactly the x of the
`listingPreviewSplit` in Main.qml's content `RowLayout` (sidebar `Layout.preferredWidth:
window.sidebarWidth`, `spacing: internalSpacing(Normal)`). It is a fixed offset, independent of row
count, and needs no new property plumbed through Main.qml.

Rejected: tracking the icon column by forwarding the listing's leading inset through Main.qml — more
wiring, and the breadcrumb would shift when a folder crosses 999↔1000 entries.

### 5.2 `leftPadding: 0` is safe because the selection background spans the full delegate, not `contentItem`

`HnSelectableDelegate.qml` (holonight-qt) sets `background: Item { ... Rectangle { anchors.fill:
parent } ... }`, where `parent` is the delegate item itself, not `contentItem`. Padding
(`leftPadding`/`rightPadding`/`padding`) in `T.ItemDelegate` only insets `contentItem` inside the
delegate's boundary; it has no effect on `background`'s geometry. Reducing the delegate's
`leftPadding` to `0` therefore only moves where `contentItem`'s `RowLayout` starts — the highlighted/
hovered/selected background Rectangle still spans `x: 0` to the delegate's full width, i.e. still
covers the gutter. This is why the design changes `leftPadding` on the delegate instance rather than
shifting the delegate itself (e.g. via an `x` offset or an extra leading margin item), which would
have left a gap of unhighlighted background at the row's true left edge.

### 5.3 `showSize`/`showModified` kept unchanged

The existing formulas (`2 * columnPadding + 120 + ...`) assume a symmetric leading/trailing inset of
`columnPadding`. With the gutter, the real leading inset becomes `lineNumberGutterWidth +
columnSpacing`, so Size/Modified will stay visible slightly longer than the Name column's 120px minimum
strictly allows, squeezing Name at narrow widths. **User decision:** accept this for now — narrow-window
layout is a more complex problem addressed in a follow-up SDD cycle (SPEC.md non-goal 8). Tests must
not assert new show/hide breakpoints; existing breakpoint assertions that break because of the gutter
are adjusted to the new geometry (window widths only), not by changing the formulas.

### 5.4 Testing width/digit growth

REQ-F-004/REQ-C-004 want the digit count verified at 0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000,
99999 rows and the width shown to grow across a power of ten.

1. **Digit computation** (`lineNumberGutterDigitsFor`, §4.1) is a standalone QML function, called from
   `tests/smoke.cpp` via `QMetaObject::invokeMethod(listing, "lineNumberGutterDigitsFor",
   Q_RETURN_ARG(QVariant, result), Q_ARG(QVariant, n))` for every breakpoint, with no fixture.
2. **Width measurement**: in a small fixture, assert `lineNumberGutterHeader` and `lineNumberGutterField`
   widths equal `ceil(lineNumberGutterMetric.implicitWidth) + 16` and the metric's `rawText` is `"999"`.
3. **One real crossing, 999 → 1000 entries**: with the minimum at 3 digits this is cheap (1000 empty
   files). Open a 999-file fixture, record `lineNumberGutterWidth`, create one more file, wait for the
   watcher refresh (`listView.count == 1000`), assert the metric `rawText` is `"9999"` and the width
   grew, and that the breadcrumb x did not change (REQ-F-015).

A 10,000+ entry fixture is not needed; beyond 1000 the path is identical and covered by (1).

### 5.5 Reactivity cost on cursor movement is acceptable

REQ-NF-002 requires that per-cursor-move rebinding of every visible delegate's gutter label not
regress `directory_performance_test.cpp`'s existing budgets. This holds because:

- QML property bindings only exist for **instantiated** delegate items. `ListView` only instantiates
  delegates for rows inside its viewport plus `cacheBuffer` (Qt Quick's standard virtualization) — a
  cursor move re-evaluates a bounded number of `gutterLabel.rawText`/`color` bindings (viewport row
  count, typically a few dozen even on a large monitor), never the full model, regardless of
  directory size.
- Each re-evaluation is a `Math.abs`/ternary/`toString()` and a palette lookup — no layout
  invalidation, since the label's `Layout.preferredWidth` is the constant `lineNumberGutterWidth`,
  unaffected by the text's new value (text simply re-renders right-aligned within the same fixed
  cell; the `RowLayout` never re-flows because no cell's size changed).
- `directory_performance_test.cpp`'s `RenderedRowsAndInteractionWhileLoading` test already exercises
  exactly this: its `QTimer` posts `5j` repeatedly while `maxInput`/`maxGap`/`frames` are measured
  against fixed budgets (150ms first frame, 100ms max frame gap, 100ms max input latency). Every one
  of those `j` presses changes `controller.cursorRow`, which after this feature also rebinds every
  visible gutter label — the existing test's budgets are the enforcement mechanism; REQ-NF-002
  requires them to pass **unmodified**, not that a new test be added, and no new test is proposed
  here for this requirement beyond what §9 lists.

### 5.6 Confirming no delegate recreation (smoke-test technique)

REQ-NF-002's second acceptance criterion wants proof that gutter rebinding is a property update, not
a delegate teardown/recreate. Technique: pick a row that stays visible and stays *not* the cursor row
across one `j` press (so its gutter value is guaranteed to change — its distance-from-cursor changes
by exactly one — without it becoming the ListView's `currentItem`, which avoids relying on
`currentItem` reassignment semantics). Concretely, extending
`PopulatedWindowKeyboardAndInlineError`'s existing `expectCursor`-style pattern:

```cpp
auto* rowZeroLabel = /* row 0's item */ ->findChild<QQuickItem*>("lineNumberGutterField");
ASSERT_NE(rowZeroLabel, nullptr);
const QString before = rowZeroLabel->property("text").toString();
// cursor starts at row 2; press 'j' to move to row 3 — row 0 is never the current row
QTest::keyClick(window, Qt::Key_J);
ASSERT_TRUE(QTest::qWaitFor([&] { return controller.cursorRow() == 3; }));
auto* rowZeroLabelAfter = /* re-locate row 0's item the same way */ ->findChild<QQuickItem*>("lineNumberGutterField");
EXPECT_EQ(rowZeroLabel, rowZeroLabelAfter);              // same QQuickItem*: no delegate recreated
EXPECT_NE(rowZeroLabelAfter->property("text").toString(), before);  // value did update
```

Locating row 0 the same way before and after (e.g. via the ListView's `contentItem`'s `childItems()`
filtered by `directoryEntryDelegate` + `index == 0`, the same pattern
`IconColumnUsesThemeIconsAndFallsBackToBundledGlyphs`'s `rowIcons()` helper already uses in this file)
and comparing raw pointers is the direct, minimal proof that the object instance persisted.

### 5.7 Inline editor anchoring

`inlineNameEditor`'s `anchors.margins: 8` (uniform) is split into `anchors.leftMargin:
root.lineNumberGutterWidth` and `anchors.rightMargin: 8` (§4.5). This is a pure margin substitution —
the editor's `anchors.right`, `anchors.verticalCenter`, height, and focus/text-input behaviour are
untouched, satisfying REQ-F-011's "height, vertical centering, and text input behavior remain
unchanged." Anchoring to `lineNumberGutterWidth` rather than the icon or Name cell's x is deliberate:
it is the one property already guaranteed to equal the gutter's right edge regardless of whether an
icon column exists in some future layout variant, keeping the editor's position derived from the
gutter's own contract (REQ-C-002) rather than re-deriving `iconColumnWidth + columnSpacing`
independently.

## 6. Alternatives considered

- **A wrapping `Item` cell around the gutter `HnLabel`, with the label anchored inside it**, instead
  of giving `HnLabel` itself the `Layout.*Width` properties and `leftPadding`/`rightPadding`
  directly. Rejected: `sizeColumnField`/`modifiedColumnField` in this same file already set
  `Layout.minimumWidth`/`preferredWidth`/`maximumWidth` directly on an `HnLabel` RowLayout child with
  no wrapping `Item` — matching that existing convention is a smaller diff and avoids an extra
  layout level with no behavioural benefit (unlike the icon cell, which needs the wrapper only
  because it stacks multiple sibling `HnIcon`s).
- **Deriving gutter width from `QFontMetricsF` in C++** instead of a hidden `HnLabel`. Rejected
  outright by REQ-NF-001 ("avoiding hand-derived font metrics") and REQ-C-003 (no new C++ for gutter
  computation); also the project's own `libexif_and_qimage_gotchas`/font-metrics history favours
  measuring through the real rendering path over reimplementing it.
- **Creating a real 99,999-entry fixture directory to test width/digit growth end-to-end.** Rejected
  in §5.4: correct but slow, and this project already reserves that scale of fixture for the opt-in
  `directory_performance_test.cpp`, not routine smoke coverage.
- **Shifting the delegate's `x` by `lineNumberGutterWidth` instead of zeroing `leftPadding`.**
  Rejected per this task's own stated constraint and §5.2: it would leave the selection background
  (which spans the full delegate) starting to the right of the gutter, visibly excluding the gutter
  from the highlighted-row rectangle — exactly the bug the "reduce leftPadding, not shift the
  delegate" instruction avoids.
- **Breadcrumb tracking the icon column**: rejected by the user in favour of the list view's left edge (§5.1).
- **Correcting `showSize`/`showModified` for the gutter now**: deferred to a follow-up narrow-window SDD (§5.3).

## 7. Risks and mitigations

- **Existing pixel-width breakpoints in `WindowColumnAlignmentAndNarrowNames` (700/850/1000px).** The
  show/hide formulas are unchanged, so those breakpoints still flip at the same widths, but the Name
  column now gets `lineNumberGutterWidth + columnSpacing - columnPadding` less space, and the test's
  `name->width() >= 120` wait may no longer hold at some widths. Mitigation: adjust only the test's
  window widths / Name-width expectation to the new geometry; do not change the formulas (§5.3). The
  proper fix belongs to the follow-up narrow-window cycle.
- **420px minimum-window-width tightness.** At the smallest supported width, the gutter now consumes
  space the Name column previously had. `nameColumnField`'s width is already computed via `Math.max`
  guards elsewhere in the file, so it cannot go negative, but the existing `WindowColumnAlignmentAndNarrowNames`
  420px assertions (`icon->width() == 20`, header/delegate alignment) should be re-run, not assumed,
  once the gutter is in place — flagged here rather than re-verified, since this document does not
  run the app.
- **`HnLabel` (`Label`) padding vs. `RowLayout` cell width double-accounting.** §4.4's `leftPadding: 8`/
  `rightPadding: 8` on the gutter `HnLabel`, combined with a `Layout.preferredWidth` that already
  includes 16px for those same paddings, is deliberate (§4.4), but it is a slightly unusual pattern
  relative to `sizeColumnField`/`modifiedColumnField` (which have no such padding, since their
  right/left alignment relies only on `Layout` width and `horizontalAlignment`). Implementation should
  visually confirm (native display, per REQ-NF-003) that the padding does not double up with
  `horizontalAlignment: Text.AlignRight` to push text outside the intended 8px margin.
- **Fractional-scale rendering (REQ-NF-003) is not automatable**, consistent with this project's
  established `fractional_scale_rendering_gotcha` — verification is native-display-only, listed
  plainly in §9 rather than represented as a gtest.

## 8. Requirement coverage map

| Requirement | Design element |
|---|---|
| REQ-F-001 | Gutter `HnLabel` as first `contentItem` RowLayout child (§4.4); width from `lineNumberGutterWidth` (§4.1). |
| REQ-F-002 | `gutterLabel.rawText`/`color` ternary on `delegate.index === root.controller.cursorRow` (§4.4). |
| REQ-F-003 | `role: HnTypographyRole.Code`, `horizontalAlignment: Text.AlignRight`, `verticalAlignment: Text.AlignVCenter` (§4.4). |
| REQ-F-004 | `lineNumberGutterDigitsFor`/`lineNumberGutterMetric`/`lineNumberGutterWidth` (§4.1/4.2). |
| REQ-F-005 | Delegate `leftPadding: 0`, header `anchors.leftMargin: 0`, 8px `leftPadding`/`rightPadding` on the label, unchanged `columnSpacing` after it (§4.3/4.4, §5.2). |
| REQ-F-006 | Bindings reference `root.controller.cursorRow` directly (§4.4) — no polling, no `Connections`. |
| REQ-F-007 | Same bindings re-evaluate automatically on `DirectoryProxyModel` re-sort/re-filter, since `delegate.index` and `cursorRow` are the only inputs and both stay live through Qt's own model-change machinery. |
| REQ-F-008 | `listView.count` (model-driven) feeds `lineNumberGutterDigits`/`Width`; existing placeholder-row insertion (`vim-modal-editing` design) is unmodified and already changes `listView.count`. |
| REQ-F-009 | `lineNumberGutterHeader` spacer, no label/separator (§4.3). |
| REQ-F-010 | Header `anchors.leftMargin: 0` + spacer width matching `lineNumberGutterWidth` keeps downstream columns' relative offsets identical to before (both RowLayouts receive identical widths). |
| REQ-F-011 | `inlineNameEditor` `anchors.leftMargin: lineNumberGutterWidth` (§4.5, §5.7). |
| REQ-F-012 | No model change; gutter number is purely `index`/`cursorRow`-derived, so the placeholder row is numbered like any other row automatically (REQ-C-003). |
| REQ-F-013/014 | Header spacer is part of the always-visible `columnHeader` Rectangle, not gated on `listView.count`/`directoryError` (§4.3; no change needed to existing visibility bindings). |
| REQ-NF-001 | Hidden `lineNumberGutterMetric` `HnLabel` with `role: Code`, `visible: false` (§4.2). |
| REQ-NF-002 | §5.5 (cost acceptance) and §5.6 (recreation-check technique). |
| REQ-NF-003 | No automated coverage; native-display verification only (§9, §7). |
| REQ-NF-004 | `Accessible.ignored: true` on `gutterLabel`, no `activeFocusOnTab` (§4.4). |
| REQ-C-001 | `HoloniightPalette.accentViolet`/`textMuted` and `HnTypographyRole.Code` only; no colour or font literals (§4.2, §4.4). |
| REQ-C-002 | `readonly property real lineNumberGutterWidth` on `DirectoryListing` root, consumed by header/delegate/inline editor (§4.1, §4.3, §4.4, §4.5). |
| REQ-C-003 | Zero C++/model changes; everything in §4 is QML-only. |
| REQ-C-004 | `lineNumberGutterDigitsFor(rowCount)` — string-length based, standalone, testable (§4.1, §5.4). |
| REQ-C-005 | `iconColumnWidth`/`sizeColumnWidth`/`modifiedColumnWidth` and `showSize`/`showModified` untouched (§4.6, §5.3). |
| REQ-F-015 | `AppHeaderBar.breadcrumbLeftInset = sidebarWidth + internalSpacing(Normal)` (§4.7, §5.1). |

## 9. Test plan summary

All new assertions land in `tests/smoke.cpp`, extending existing `TEST()`s where the fixture already
matches (per this project's established preference for surgical additions over new `TEST()`s, per
`main-view-icons` DESIGN.md §2's own note) rather than new files, since this is a QML-only,
single-component change.

- **Gutter numbering (REQ-F-002, REQ-C-001)**: extend `PopulatedWindowKeyboardAndInlineError` (or a
  neighbouring small-fixture test) to assert `lineNumberGutterField` text/color for the cursor row
  (accentViolet, `cursorRow+1`) and at least two non-cursor rows (textMuted, correct `Math.abs`
  distance) at a known cursor position, then again after one `j`/`k` press.
- **Width/digit measurement (REQ-F-004, REQ-C-004, REQ-NF-001)**: `QMetaObject::invokeMethod` calls
  against `lineNumberGutterDigitsFor` for 0, 1, 9, 10, 99, 100, 999, 1000, 9999, 10000, 99999 (no
  fixture); small-fixture assertion that the width equals `ceil(metric.implicitWidth) + 16` with metric
  `rawText == "999"`; one 999 → 1000 entry crossing asserting the width grows (§5.4).
- **Reactivity on sort/filter/directory changes (REQ-F-006/007/008)**: reuse existing hidden-files-
  toggle and `o`/`O` placeholder fixtures already present in `smoke.cpp`'s modal-editing tests; add
  gutter-value assertions to their existing post-condition checks rather than new `TEST()`s.
- **Header alignment (REQ-F-009/010)**: extend `WindowColumnAlignmentAndNarrowNames` — assert
  `lineNumberGutterHeader`'s width equals `lineNumberGutterWidth`, and that its presence does not
  change the already-asserted `nameHeader`/`name`, `sizeHeader`/`size`, `modifiedHeader`/`modified`
  1px-alignment checks across the same 420/700/1000/1600px sweep (§7's tightness risk applies here).
- **Breadcrumb (REQ-F-015)**: replace the existing
  `EXPECT_NEAR(breadcrumb->mapToScene(...).x(), icon->mapToScene(...).x(), 1)` with a comparison
  against `directoryListView`'s scene x, keeping the 420/700/1000/1600px sweep, and update the comment
  to record why; assert breadcrumb x is unchanged across the 999 → 1000 crossing.
- **Inline editor position (REQ-F-011)**: extend the modal-editing INSERT-mode test to assert
  `inlineEditor->mapToScene(QPointF()).x() >= lineNumberGutterWidth` (via the delegate's own
  coordinate space) after entering edit mode.
- **Placeholder row numbering (REQ-F-012)**: extend the existing `o`/`O` insert/commit/cancel test to
  assert the placeholder's gutter value at insertion, after commit (post re-sort), and that
  neighbours renumber after cancel.
- **Empty/error state (REQ-F-013/014)**: extend (or add small, fast) tests opening an empty directory
  and a permission-denied directory, asserting `lineNumberGutterHeader` is visible and no
  `lineNumberGutterField` exists anywhere under the window.
- **Delegate-recreation check (REQ-NF-002)**: §5.6's pointer-identity technique, added to the same
  test that already exercises `j`/`k` cursor movement.
- **Accessibility (REQ-NF-004)**: `QQmlProperty(label, "Accessible.ignored", qmlContext(label)).read()` is true (the pattern `history_navigation_window_test.cpp` already uses for `Accessible.name`)
  — alongside the numbering assertions above (no new fixture).
- **`directory_performance_test.cpp`**: no modification; REQ-NF-002 requires it pass with its budgets
  unrelaxed, which §5.5 argues is already exercised by its existing `5j` interaction loop.
- **REQ-NF-003 (fractional-scale rendering)**: explicitly not automated, per SPEC.md's own acceptance
  criteria ("no screenshot-based test or offscreen rendering... only native on-display inspection").
  Manual verification on the user's Hyprland 1.5x display, consistent with this project's established
  practice for this exact class of requirement.
- **`task check`** (`task build`, `task test`, `task format-check`, `task tidy`, `task qml-lint`) must
  pass; no new `.qml` files are introduced (only existing files are edited), so the
  `qml_format_check_gap` chore (new `.qml` files needing manual `Taskfile.yml`/
  `scripts/check-qml-format.sh` registration) does not apply this cycle.
