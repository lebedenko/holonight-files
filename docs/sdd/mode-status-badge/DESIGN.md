# Mode Status Badge — DESIGN

## Grounding summary

- `apps/files/ModeStatusBar.qml` is a `RowLayout` whose children are the mode-contextual `HnLabel`s
  (`normalStatusLabel`, `taskProgressLabel`, `conflictPromptLabel`, `trashConfirmLabel`,
  `visualStatusLabel`, `searchField`, `insertStatusLabel`), in that order. It already imports
  `Holonight`, `Holonight.Core`, `Holonight.Controls` — no new imports are needed for
  `HnAppearance`, `HnSurfaceRole`, `HnMetrics`, `HnControlSize`, `HolonightTheme`, `HoloniightPalette`
  (all already used by sibling files such as `QuickLookOverlay.qml`).
- `Main.qml`'s `footerBar` `Rectangle` hosts `ModeStatusBar` with `anchors.fill: parent` and
  `anchors.margins: HnMetrics.internalSpacing(HnControlSize.Normal)`; `footerBar.implicitHeight`
  is derived from `modeStatusBar.implicitHeight`. The badge lives *inside* that inset — REQ-F-007's
  "inset pill" and non-goal "no corner-flush geometry" are already satisfied structurally by putting
  the badge as an ordinary first row child, not by any special anchoring.
- `VimModeController::Mode` is `enum class Mode { Normal, Visual, Search, Insert }` (`Q_ENUM`), so in
  QML `VimModeController.Normal/.Visual/.Search/.Insert` evaluate to `0/1/2/3` — a plain
  array-indexed mapping is safe and matches the existing switch-less style already used for
  `TaskManager::TaskKind` nowhere in this file (the file uses `if`/ternary and `switch`, so either
  idiom fits; I use an array for O(1) lookup and one readonly binding per REQ-F-005).
- `TaskManager::hasPrompt()`/`promptKind()` are what `trashConfirmLabel`/`conflictPromptLabel`
  already gate on; the badge does *not* need to know about prompts at all — REQ-F-006 falls out for
  free because prompts never change `vim.currentMode` (VimModeController is untouched by TaskManager;
  confirmed by grepping `task_manager.h`/`.cpp` for any `vim()` call — there is none).
- `tests/smoke.cpp` drives modes exclusively through real key events (`QTest::keyClick(window,
  Qt::Key_V)` for Visual, `Qt::Key_Slash` for Search, `Qt::Key_I`/`A`/`O` for Insert, `Qt::Key_Escape`
  back to Normal) against a `QQmlApplicationEngine` loaded via `engine.loadFromModule("HolonightFiles",
  "Main")`, then asserts with `window->findChild<QQuickItem*>("objectName")` /
  `window->findChild<QObject*>("objectName")` and `controller.vim()->currentMode()`. No test sets
  `currentMode` directly — there is no invokable setter, only mode-transition methods — so the new
  tests follow the same keystroke-driven pattern.
- Sibling design system (`holonight-qt`): `HnKeyHint.qml` is `Control` + `HnLabel` `contentItem` +
  `Rectangle` `background` with `radius: HnAppearance.roundedRadius(HnSurfaceRole.Badge, width,
  height, HnAppearance.revision)`, `font.family: HolonightTheme.monospaceFont`. **Gotcha**:
  `qml/hnshapetypes.h`'s `HnSurfaceRole` enum is `Window, Panel, Popup, Card, Menu, Tooltip, Control,
  Pill, Hud, WorkspaceIndicator` — there is no `Badge` member. `HnKeyHint.qml` already references
  `HnSurfaceRole.Badge`, which resolves to `undefined` in QML, coerces to `0` at the
  `Q_INVOKABLE qreal roundedRadius(int role, ...)` boundary, and `hnappearance.cpp`'s `surfaceRole()`
  helper accepts `0` as `Window` (it's in-range, so it is *not* remapped to the `Card` fallback). This
  is a real, live inconsistency in `holonight-qt`, out of scope to fix (constraint: no theme-engine
  changes), so this design uses `HnSurfaceRole.Pill` instead of reproducing that existing
  bug. See **Known risks**.
- `HoloniightPalette`'s live default is the `HoloNightDark` scheme (`palette/palette.cpp`,
  `holoNightDarkTokens()`): `background=#0C1118`, `accentBlue=#5EA2FF`, `accentViolet=#9A8CFF`,
  `success=#79D97F`, `accentYellow=#F2C46B`. **A light scheme exists** (`HoloNightLight`,
  `holoNightLightTokens()`, and 14 further built-in schemes) and *does* change every one of these
  five tokens — see **Contrast** below for why this matters.
- `Taskfile.yml`: `format-check` runs `bash scripts/check-qml-format.sh`, which already lists
  `apps/files/ModeStatusBar.qml` in its `qmlformat`-diff loop (no gap — unlike the historical
  "new .qml file" gap, this file already exists and is already covered). `qml-lint` runs `cmake
  --build build/{{.PRESET}} --target qml-lint`. Both must pass with zero new diagnostics
  (REQ-NF-002).

## Contrast (REQ-F-004)

WCAG relative-luminance formula, computed against the **live default `HoloNightDark` scheme**
(same formula `holonight-qt/tests/test_palette_contrast.cpp` already uses for its own palette
tests):

| Mode | Fill | Hex | Contrast vs. `background` (#0C1118) as text | Passes ≥4.5:1? |
|---|---|---|---|---|
| NORMAL | accentBlue | `#5EA2FF` | **7.30 : 1** | Yes |
| VISUAL | accentViolet | `#9A8CFF` | **6.83 : 1** | Yes |
| INSERT | success | `#79D97F` | **10.86 : 1** | Yes |
| SEARCH | accentYellow | `#F2C46B` | **11.62 : 1** | Yes |

All four pass WCAG AA comfortably under the scheme the app actually ships with today — no palette
substitution is needed for REQ-F-004 as written.

**Risk carried forward, not fixed here (constraint: no palette changes):** the same computation
against `HoloNightLight` (`background=#E7EEF5` as text, `accentBlue=#3E7BDB`,
`accentViolet=#7566D4`, `success=#3E9449`, `accentYellow=#C38A1C`) gives **3.55 : 1, 3.89 : 1,
3.24 : 1, 2.58 : 1** respectively — all four *fail* WCAG AA, some badly. `background` is a poor
choice of text token in any scheme where `background` is itself light, because the badge fills in
this palette family are mid-tone accents designed to sit *on* a dark canvas, not to be a dark-on-them
surface. Since `holonight-files` has no theme-switcher UI today (grep of
`apps/files/settings/*.cpp` and `apps/files/*.cpp` shows no `ThemeSchemeKind`/scheme selection code)
this is a latent risk, not an active bug: it only bites if/when a light or non-Dark scheme is wired
up. Flagging per the SPEC's own instruction to "propose the palette color that passes... if any is
< 4.5:1" — but changing the palette is explicitly out of scope (REQ-C-001/REQ-C-002), so the
recommendation is deferred: **when theme switching ships, revisit `background` as the badge's text
token** (a candidate fix would be a dedicated `onAccent`-style token, computed per-scheme, rather
than reusing `background`).

## Components

`ModeStatusBar.qml`, first child of the `RowLayout`, before `normalStatusLabel`:

```qml
RowLayout {
    id: root
    required property DirectoryController controller
    spacing: HnMetrics.internalSpacing(HnControlSize.Normal)

    readonly property var modeMeta: [
        { label: "NORMAL", fill: HoloniightPalette.accentBlue },   // VimModeController.Normal (0)
        { label: "VISUAL", fill: HoloniightPalette.accentViolet }, // VimModeController.Visual (1)
        { label: "SEARCH", fill: HoloniightPalette.accentYellow }, // VimModeController.Search (2)
        { label: "INSERT", fill: HoloniightPalette.success },      // VimModeController.Insert (3)
    ]
    readonly property var currentModeMeta: root.modeMeta[root.controller.vim.currentMode]

    Control {
        id: modeBadge
        objectName: "modeBadge"
        Layout.alignment: Qt.AlignVCenter
        implicitWidth: badgeMetric.implicitWidth + leftPadding + rightPadding
        padding: 4
        leftPadding: 10
        rightPadding: 10
        activeFocusOnTab: false
        Accessible.role: Accessible.StaticText
        Accessible.name: qsTr("%1 mode").arg(root.currentModeMeta.label)

        contentItem: HnLabel {
            objectName: "modeBadgeLabel"
            role: HnTypographyRole.Code
            rawText: root.currentModeMeta.label
            color: HoloniightPalette.background
            horizontalAlignment: Text.AlignHCenter
        }
        background: Rectangle {
            color: root.currentModeMeta.fill
            radius: HnAppearance.roundedRadius(HnSurfaceRole.Pill, width, height, HnAppearance.revision)
        }
    }

    // Hidden metric label, same font/role as the visible one, holding the longest of the four
    // 6-char labels so the badge width never changes across modes (REQ-NF-001). All four labels
    // are exactly 6 chars in HolonightTheme.monospaceFont, so any one of them is a valid metric —
    // "NORMAL" is used for readability. Same trick as the line-number gutter's hidden Code-role
    // metric label (DirectoryListing.qml) and the same instance-count approach already trusted
    // in this codebase.
    HnLabel {
        id: badgeMetric
        visible: false
        role: HnTypographyRole.Code
        rawText: "NORMAL"
    }

    HnLabel {
        objectName: "normalStatusLabel"
        …
```

Key structural points:
- `modeBadge` is a `Control` (not a bare `Rectangle`) so it gets `Control`'s `padding`/`background`/
  `contentItem` triad for free, exactly mirroring `HnKeyHint.qml` — this is the "closest existing
  analog" the brief points at, reused rather than reinvented. `objectName: "modeBadge"` is on this
  `Control`, not on the inner `Rectangle`, which is what `find(objectName: "modeBadge")` /
  `findChild<QQuickItem*>("modeBadge")` resolve to; its `background` Rectangle is reached by
  `modeBadge->property("background").value<QQuickItem*>()` in the test.
- No `MouseArea`/`TapHandler` anywhere in the subtree, `activeFocusOnTab: false`, and `Control`'s
  default `focusPolicy` is `Qt.NoFocus` unless set — left unset, so REQ-F-010 holds without extra
  code.
- `Layout.alignment: Qt.AlignVCenter` centers the badge within the `RowLayout`'s cross-axis, which
  is already vertically centered within `footerBar`'s `anchors.margins` inset (REQ-F-007).

## Data flow

Single source of truth: `root.controller.vim.currentMode` (an `int`-valued enum in QML) indexes
`root.modeMeta` once, in `currentModeMeta`. Every visual attribute — badge label text, fill color,
`Accessible.name` — reads from `currentModeMeta`, never from `currentMode` directly a second time.
This is the "single readonly mapping" REQ-F-005 needs for atomic same-turn updates: QML re-evaluates
`currentModeMeta` when `controller.vim.currentMode` changes (it depends on `VimModeController::changed`
through the `Q_PROPERTY(... NOTIFY changed)`), and every consumer binding re-evaluates off that one
new object in the same event-loop turn — there is no intermediate signal hop that could let text and
fill visibly desync.

## Interfaces (what tests read)

| Property | Path from `window` | Type |
|---|---|---|
| Badge presence/order | `window->findChild<QQuickItem*>("modeBadge")`, compare to `RowLayout`'s `children()[0]` or `modeStatusBar->childItems().first()` | `QQuickItem*` |
| Label text | `modeBadge->findChild<QQuickItem*>("modeBadgeLabel")->property("text")` (or `"rawText"`, both hold the plain 6-char string since `textFormat` stays `PlainText`) | `QString` |
| Fill color | `modeBadge->property("background").value<QQuickItem*>()->property("color")` | `QColor` |
| Accessible name | `QQmlProperty(modeBadge, "Accessible.name", qmlContext(modeBadge)).read()` (same idiom the gutter test already uses for `Accessible.ignored`) | `QString` |
| Width / x | `modeBadge->width()`, `modeBadge->x()` (position within `RowLayout`) or `mapToItem(window->contentItem(), QPointF())` for an absolute check | `qreal` |
| Radius | `modeBadge->property("background").value<QQuickItem*>()->property("radius")` | `qreal` |

## Exact changes to `visualStatusLabel` / `insertStatusLabel`

```qml
// visualStatusLabel — before:
rawText: qsTr("VISUAL  ·  %1 selected").arg(root.controller.vim.selectedCount)
// after:
rawText: qsTr("%1 selected").arg(root.controller.vim.selectedCount)
```

```qml
// insertStatusLabel — before:
rawText: root.controller.vim.insertValid
    ? qsTr("INSERT  ·  Enter to confirm, Esc to cancel")
    : qsTr("INSERT  ·  %1").arg(root.controller.vim.insertErrorMessage)
// after:
rawText: root.controller.vim.insertValid
    ? qsTr("Enter to confirm, Esc to cancel")
    : root.controller.vim.insertErrorMessage
```

(The INSERT error branch drops the now-redundant `qsTr("INSERT  ·  %1")` wrapper entirely since
`insertErrorMessage` is already a plain, presentation-ready string coming from `NameValidationResult`
— confirmed by how `insertStatusLabel`'s `color` already branches on `insertValid` without any other
wrapper text.)

## Test plan

The table below records the original plan. Implemented test names and consolidated
assertions are mapped in [VERIFICATION.md](VERIFICATION.md); accessible-name checks
are shared by the per-mode tests rather than a separate test function.

All new tests go in `tests/smoke.cpp`, following the existing `loadActiveWindow`/`engine.loadFromModule`
+ `QTest::keyClick` idiom already used by `ModeStatusBarShowsProgressAndConflictPromptAndCtrlCCancels`
and `ModeStatusBarShowsTrashConfirmation` (same file, same fixtures: `files_test::fixturePattern`,
a `DirectoryController`, `window->requestActivate()` + `QTest::qWaitForWindowActive`).

| Test | REQ IDs | How it drives state |
|---|---|---|
| `test_ModeBadge_NormalMode` | REQ-F-001, F-002, F-003, C-003 | Load window, `controller.open(dir)`, wait `!scanning()`. Badge is present at load (no key needed): assert `find("modeBadge")` non-null, `visible`, label text `"NORMAL"`, fill `== HoloniightPalette` read live via `evaluateInContext` (same helper the gutter test uses for `HolonightTheme.monospaceFont`) or via a small inline `QQmlExpression` against the badge's own `qmlContext`, `accentBlue`. |
| `test_ModeBadge_VisualMode` | REQ-F-002, F-003 | `QTest::keyClick(window, Qt::Key_V)` → `ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Visual)`; assert label `"VISUAL"`, fill `HoloniightPalette.accentViolet`. |
| `test_ModeBadge_SearchMode` | REQ-F-002, F-003 | `QTest::keyClick(window, Qt::Key_Slash)` → mode `Search`; assert label `"SEARCH"`, fill `HoloniightPalette.accentYellow`. |
| `test_ModeBadge_InsertMode` | REQ-F-002, F-003 | `QTest::keyClick(window, Qt::Key_I)` → mode `Insert`; assert label `"INSERT"`, fill `HoloniightPalette.success`. |
| `test_ModeBadge_WidthConstant` (REQ-NF-001's cross-mode jitter test) | REQ-F-005, REQ-NF-001 | From Normal, record `modeBadge->width()`/`x()` as baseline; cycle `V` → `Escape` → `/` → `Return`-cancel via `Escape` → `I` → `Escape`, re-reading width/x and label/fill after each `QTest::qWait(0)` (a single event-loop spin, matching REQ-F-005's "no jitter" wording) and asserting equality to baseline and correctness against the just-entered mode in the same pass. |
| `test_ModeBadge_VisibleDuringPrompt` | REQ-F-006 | Reuse the exact trash-confirm sequence from `ModeStatusBarShowsTrashConfirmation`: `QTest::keyClick(window, 'D', Qt::ShiftModifier)`, `QTest::qWaitFor([&]{ return controller.tasks()->hasPrompt(); })`; assert `modeBadge->property("visible").toBool()` and label text `"NORMAL"` (mode never left Normal — trash-confirm is raised from Normal-mode `D`). |
| `test_ModeBadge_RemovesPrefixes` | REQ-F-008 | Visual: `Qt::Key_V` then `Qt::Key_J` to select one row; assert `visualStatusLabel`'s `rawText` does **not** start with `"VISUAL"` and does `contain("selected")`. Insert: `Qt::Key_I`; assert `insertStatusLabel`'s `rawText` does not start with `"INSERT"` and still contains `"Enter to confirm"` for the valid case (existing `PopulatedWindowKeyboardAndInlineError`-style flow can trigger the invalid case by typing `"../invalid"` into `inlineNameEditor` to also check the error-message branch keeps only `insertErrorMessage`, no prefix). |
| `test_ModeBadge_NoMouseOrFocus` | REQ-F-010 | Simulate `QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, modeBadge->mapToScene(...).toPoint())`; assert `window->activeFocusItem()` is unchanged (still the list or whatever had focus before) and `controller.vim()->currentMode()` unchanged — no handler exists to fire, so this is really a *lack-of-effect* assertion. |
| `test_ModeBadge_AccessibleName` | REQ-F-009 | After each of the four keyboard-driven mode entries above, assert `QQmlProperty(modeBadge, "Accessible.name", qmlContext(modeBadge)).read().toString() == "<MODE> mode"` and `Accessible.role` reads `Accessible.StaticText` (cast through `QQmlProperty` since `Accessible.role` isn't a plain `QQuickItem` property). |

This is 9 test functions total — REQ-NF-003's minimum of "4 mode tests + jitter + prompt-visibility"
(6) is exceeded; the two extra (`RemovesPrefixes`, `NoMouseOrFocus`, `AccessibleName` — 3 extra)
directly cover REQ-F-008/F-010/F-009, which the SPEC's own "Smoke Test Coverage" list (7 tests) also
implies are needed even though it enumerates only 7 by name. I keep the SPEC's 7 names where they
map 1:1 and add the 2 it left implicit (`NoMouseOrFocus`, `AccessibleName`) since REQ-F-009/F-010
have explicit "Acceptance: Smoke test..." clauses that the SPEC's own summary table doesn't otherwise
schedule a named test for.

## Key decisions & rationale

1. **`Control` + `HnLabel` contentItem + `Rectangle` background, not a bare `Rectangle` with a child
   `Text`.** Matches `HnKeyHint.qml` exactly (the SPEC's own pointer to "the closest existing
   analog"), gets `Accessible.role`/`Accessible.name` as first-class `Control` properties instead of
   hand-rolled `Accessible.*` on a `Rectangle` (which also supports them, but `Control` is the
   idiomatic host for an interactive-looking-but-not-interactive widget in this design system).
2. **Array-indexed `modeMeta` keyed by the `Mode` enum's underlying int, not a `switch`/if-chain.**
   `VimModeController::Mode` has no explicit enumerator values, so Qt assigns `0..3` in declaration
   order (`Normal, Visual, Search, Insert`) — matches array indices `0..3` directly, giving REQ-F-005
   a single dependency edge instead of four independent `switch`-in-binding expressions that could in
   principle re-evaluate out of lockstep.
3. **A single hidden `HnLabel` metric, not `TextMetrics`.** Considered
   `TextMetrics { text: "NORMAL"; font: ... }` for REQ-NF-001's fixed width, but the codebase already
   has an established, tested idiom for "hidden label sized off worst-case text" — the line-number
   gutter's hidden Code-role metric label in `DirectoryListing.qml`, asserted on directly by
   `LineNumberGutterWidthGrowsWithDigitsButBreadcrumbStays`. Reusing that idiom keeps the two
   fixed-width tricks in this codebase consistent and avoids introducing `TextMetrics` (a different
   QtQuick type with its own font-sync foot-guns) for the first time in this file.
4. **Text color is `HoloniightPalette.background` for all four modes (REQ-F-004 as written), despite
   the Light-scheme failure found above.** Not fixed here per constraint REQ-C-002 (existing palette
   tokens only) and REQ-C-001 (no palette changes) — see **Contrast** and **Known risks**.
5. **Translate the accessible-name template with `qsTr("%1 mode").arg(label)`.**
   REQ-C-004 exempts only the four mode labels from translation. Translating the surrounding
   text follows CONTRIBUTING.md while retaining the literal mode name as the placeholder value.

## Alternatives considered

- **Separate `ModeBadge.qml` file.** Rejected — REQ-C-001 explicitly forbids new `.qml` files;
  everything must live inside `ModeStatusBar.qml`.
- **Reuse `HnStatusIndicator`-style component.** `HnStatusIndicator.qml` exists but renders a dot + colored text, not a filled pill, so it does
  not fit. `HnKeyHint.qml` is
  the actual closest analog and is what's used above.
- **Corner-flush badge (edge-aligned to the footer, ignoring `anchors.margins`).** Rejected per
  SPEC's explicit non-goal ("Corner-flush or edge-aligned badge geometry (inset only)"); the plain
  `RowLayout`-child placement inside `footerBar`'s existing inset achieves this with zero extra
  anchoring code.
- **`TextMetrics` for fixed width instead of a hidden `HnLabel`.** Rejected in favor of reusing the
  gutter's existing hidden-label idiom (see Key Decision 3) — same effect, fewer new patterns.

## Known risks

- **`HnSurfaceRole.Badge` does not exist in holonight-qt's `HnSurfaceRole` enum** (`Window, Panel,
  Popup, Card, Menu, Tooltip, Control, Pill, Hud, WorkspaceIndicator`). `HnKeyHint.qml` references it
  and silently gets role `0` (`Window`) radius. Decision (user-approved): the badge uses the real
  `HnSurfaceRole.Pill`, so its rounding intentionally differs from `HnKeyHint`. Fixing `HnKeyHint` is
  a separate holonight-qt follow-up.
- **Text-on-fill contrast fails WCAG AA under the `HoloNightLight` scheme** (and likely most of the
  14 other built-in schemes, given `background` is the wrong semantic token for this use — see
  **Contrast**). No smoke test can catch this today because the test suite runs under the default
  `HoloNightDark` scheme; a future scheme-parameterized contrast test (mirroring `holonight-qt`'s own
  `test_palette_contrast.cpp` pattern) would be needed once theme switching lands in this app.
- **1.5× DPR crispness cannot be verified by any offscreen/CI smoke test** — this is an established,
  already-documented gap in this codebase (`[[fractional-scale-rendering-gotcha]]`: offscreen
  screenshots miss DPR-change bugs on this user's Hyprland @1.5 setup). REQ-F-007's crispness
  acceptance criterion is manual-only; the SPEC's own "Manual Verification Checklist" already
  captures this and no smoke test attempts it.
- **Monospace font fallback breaking equal width.** `HolonightTheme.monospaceFont` is a `QString`
  font family name (`HnFontRegistry`/embedded font not inspected here — out of this feature's scope);
  if it's ever unavailable on a target system and Qt substitutes a non-monospace fallback, the four
  6-character labels would no longer render at identical advance widths *within* the hidden metric
  label itself, silently invalidating the REQ-NF-001 fixed-width assumption even though the width
  value read by the test would still self-consistently match (both the visible and hidden labels use
  the same font, so they'd drift together, not apart) — the real risk is purely *visual* (glyphs not
  perfectly monospaced against each other across mode switches), not something the width/x assertions
  can catch. No action taken; noted as an accepted, pre-existing risk shared by every other
  monospace-dependent surface in this app (e.g., the line-number gutter).
- **Existing tests asserting old prefixed strings.** Grepped `tests/smoke.cpp` for `"VISUAL  ·"` /
  `"INSERT  ·"` and for any `rawText`-equality assertion that could embed either prefix — none found.
  No currently-passing test needs to change because of the REQ-F-008 prefix removal; this risk is
  present in the SPEC's own concern but does not materialize in the current test suite.
