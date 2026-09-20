# SDD Design — quick-look-text-viewer

Status: Implemented; review corrections authorized on 2026-09-21. Manual native acceptance confirmed by the user on 2026-09-21; isolated runtime verification remains pending (see VERIFICATION.md).
Spec: `docs/sdd/quick-look-text-viewer/SPEC.md`

Grounded in: `apps/files/preview/{preview_service,text_preview_service,preview_selection}.{h,cpp}`,
`apps/files/application/{directory_controller,file_command_router,navigation_session}.{h,cpp}`,
`apps/files/presentation/{inspection_keys,quick_look_presentation_model}.cpp`,
`apps/files/qml/inspection/{QuickLookOverlay,PreviewPane}.qml`, `qml/listing/DirectoryListing.qml`,
`tests/{preview_fixtures.h,preview_service_test_access.h,directory_controller_test_access.h,
preview_integration_test.cpp,directory_controller_test.cpp,smoke.cpp}`. Identifiers below either exist at
those locations or are marked **new**.

## 1. Overview

Quick Look becomes a pinned, keyboard-driven line viewer for `text/plain` and images. Four decisions shape
everything else:

1. **Worker computes lines.** The existing one-shot worker job reads up to 102,400 bytes once, decodes
   UTF-8, splits lines and returns a `QStringList` through the existing queued `PreviewResult`.
2. **PreviewService owns the state.** It holds a small line model (**new** `TextLineModel`) and the
   current-line index, clamps navigation, and resets on Quick Look open.
3. **Keys keep one path.** `InspectionKeys::press` -> `DirectoryController::handleKey` ->
   `FileCommandRouter`. While Quick Look is open the router maps j/k/ArrowDown/ArrowUp to a new
   `MoveQuickLookLine` command and stops emitting listing `Move`. That is what pins the file.
4. **QML is a ListView.** `QuickLookOverlay.qml` replaces the `Flickable`+`TextEdit` with a `ListView`
   over `preview.textLines`; delegates render gutter + text + highlight. Auto-scroll is a
   signal-driven `positionViewAtIndex(..., ListView.Contain)`, never a binding.

Finding that affects scope: the docked `PreviewPane.qml` has no text panel today (README: "The sidebar no
longer shows text content"; `grep textContent` finds only `QuickLookOverlay.qml`). See section 8, item 10.

## 2. Components and file-level ownership

AGENTS.md: worker filesystem algorithms, application coordination and QML presentation stay separate.

| Layer | File | Status | Responsibility |
|---|---|---|---|
| Worker (pure, no QObject) | `apps/files/preview/text_lines.h/.cpp` | **new** | `namespace TextLines`: `QByteArrayView trimIncompleteUtf8Tail(QByteArrayView)`, `QString decodeUtf8(QByteArrayView)`, `QStringList splitLines(const QString&)`. No I/O, no Qt object identity; unit-testable alone. |
| Worker (file I/O) | `apps/files/preview/text_preview_service.h/.cpp` | modified | `readHead` keeps the open/seek/read/error logic; `TextPreviewResult` drops `content`, gains `QStringList lines`; `wasTruncated`/`totalSize` semantics unchanged. Calls `TextLines`. `looksBinary` unchanged. |
| Worker job | `apps/files/preview/preview_service.cpp` (`runPreviewJob`) | modified | `kTextHeadBytes` 65536 -> **`kTextViewerMaxBytes = 102400`**; adds gate-MIME reporting (3.2); cancel check before decode/split. |
| App coordination | `apps/files/preview/preview_service.h/.cpp` | modified | Owns `TextLineModel text_lines_`, `int current_line_`, eligibility, `moveCurrentLine`, reset rules, `currentLineIndexChanged`. |
| App coordination (model) | `apps/files/preview/text_line_model.h/.cpp` | **new** | `QAbstractListModel`: `setLines(QStringList)`, `clear()`, roles `lineText`, `lineNumber`. Storage only, no navigation logic. |
| App coordination | `apps/files/application/file_command_router.h/.cpp` | modified | New `FileCommand::Kind::MoveQuickLookLine`; new QL-open key branch (4.3). Pure parsing, no side effects. |
| App coordination | `apps/files/application/directory_controller.cpp` | modified | `execute()` handles `MoveQuickLookLine` -> `preview_.moveCurrentLine(count)`. |
| App coordination | `apps/files/application/navigation_session.cpp` | modified | Reject history traversal while Quick Look is open, including shortcut paths that bypass the command router. |
| App coordination | `apps/files/preview/preview_selection.h/.cpp` | modified | `canPreviewSelection()` adds `preview_.quickLookEligible()`; `syncPreviewTarget()` pin guard (4.4). |
| Presentation glue | `apps/files/presentation/inspection_keys.cpp` | modified | In popup mode only, normalize `Qt::Key_Up/Down` to `"ArrowUp"/"ArrowDown"` and allow them through the popup filter. |
| QML presentation | `apps/files/qml/inspection/QuickLookOverlay.qml` | modified | Replace `Flickable`+`TextEdit` with the line viewer; caption fix (4.6). No logic beyond bindings and one `Connections`. |
| Unchanged | `PreviewPane.qml`, `quick_look_presentation_model.*` | unchanged | `Kind::Text` still keys off `hasText`; card sizing unchanged. |
| Build | `apps/files/CMakeLists.txt`, `tests/CMakeLists.txt` | modified | Register new sources and test files. |
| Tests / docs | see 7 | modified / **new** | |

No `holonight-qt` change (only existing `HnLabel`/`HolonightTheme`/palette tokens are used).

## 3. Data flow

```
Space (Normal mode) -> InspectionKeys.press -> controller.handleKey(" ")
  router: canPreview? (cursor valid, hasEntry, !statFailed, preview.quickLookEligible())
     no  -> consumed no-op (REQ-F-001/022; quickLookOpen stays false, nothing else touched)
     yes -> ToggleQuickLook -> quick_look_open_=true; preview_.setQuickLookActive(true)
                               (false->true transition: current_line_ retained-index reset to 0)
Selection (already earlier, on cursor move): PreviewService::setTarget -> dispatch() -> worker:
  open O_NONBLOCK, fstat regular, sniff 8 KiB, QMimeDatabase mime,
  not image, !looksBinary(sniff) -> TextPreviewService::readHead(file, 102400):
      bytes = read(102400); truncated = fileSize > bytes.size()
      if truncated: bytes = trimIncompleteUtf8Tail(bytes)
      text = decodeUtf8(bytes)            // invalid -> U+FFFD, leading BOM stripped
      lines = splitLines(text)            // LF, CRLF, CR; trailing terminator adds no line
      if bytes empty: lines = {""}        // REQ-F-017
  -> PreviewResult{has_text, text{lines,totalSize,wasTruncated}, gate_mime}
UI thread queued applyResult (generation-checked): assign all fields, text_lines_.setLines(std::move(lines)),
  clamp current_line_, then emit changed(), then currentLineIndexChanged() if the index changed.
QML: ListView{ model: preview.textLines } delegates bind lineNumber/lineText;
     highlight = (index === preview.currentLineIndex).
j / k / ArrowDown / ArrowUp (QL open) -> InspectionKeys -> handleKey -> router
  -> MoveQuickLookLine(+-1) -> execute -> preview_.moveCurrentLine(+-1) [clamp] -> currentLineIndexChanged
  -> QML Connections.onCurrentLineIndexChanged -> positionViewAtIndex(i, ListView.Contain)
Space / Escape -> Toggle/CloseQuickLook -> quick_look_open_=false (existing focus restore in DirectoryListing).
```

Threading (REQ-NF-004): all file reads, decode and splitting run on the existing preview worker thread. The
UI thread only moves a `QStringList` (implicitly shared, O(1)) into the model. Generation counter, the
cancellation token and the 3 s `timeout_timer_` apply as today.

### 3.1 Line splitting, truncation and terminators (precise rules)

- Terminators: `\n`, `\r\n` (one terminator), lone `\r`. U+2028/U+0085 are not terminators.
- A terminator ends the current line. After the last terminator no empty line is appended (REQ-F-020).
  `"a\n"` -> `["a"]`; `"a\n\n"` -> `["a",""]`; `"\n"` -> `[""]`; `"a\r"` -> `["a"]`.
- Empty byte buffer (zero-byte file) yields `[""]`; this is the only synthesized line (REQ-F-017).
- Truncation is a byte cap. The last loaded line is whatever the loaded bytes end in: it may be a partial
  line (kept, not trimmed: spec non-goal "individual lines are not trimmed") or a complete line if the cap
  fell right after a terminator (still no phantom empty line). A cut between `\r` and `\n` reads as a lone
  `\r` terminator, which is harmless.
- If the cap cuts a multi-byte UTF-8 sequence, `trimIncompleteUtf8Tail` drops the incomplete tail (at most
  3 bytes) so no spurious U+FFFD appears at the end. This only applies when `truncated`; a file that
  genuinely ends in an invalid/incomplete sequence still gets U+FFFD (REQ-F-018).
- `wasTruncated = totalSize > bytesRead` (before trimming). A file of exactly 102,400 bytes is not truncated.

### 3.2 Gating MIME (why the worker reports it)

`mime_type_` is set only by the worker after a successful sniff. If the file cannot be opened (chmod 000),
`mime_type_` stays empty, so gating on `mime_type_` alone would make REQ-F-021 (open Quick Look on an
unreadable text/plain file) unreachable. Design: the worker result carries a **new** `gate_mime`:
the sniffed MIME when available; on open/read failure the extension-only guess
`QMimeDatabase().mimeTypeForFile(path, QMimeDatabase::MatchExtension).name()` (in-memory glob match, no I/O).
`PreviewService::quickLookEligible()` (**new**, C++ only) returns true when `hasEntry && !stat_failed_ &&
(gate_mime startsWith "image/" || gate_mime == "text/plain" || gate_mime == "application/x-zerosize")`.
`mime_type_` semantics and the existing tests on it are untouched. Before the worker result arrives
(`gate_mime` empty and no error) the entry is not eligible.

## 4. Interfaces

### 4.1 PreviewService additions (`preview_service.h`)

```cpp
Q_PROPERTY(QAbstractItemModel* textLines READ textLines CONSTANT)
Q_PROPERTY(int textLineCount READ textLineCount NOTIFY changed)
Q_PROPERTY(int currentLineIndex READ currentLineIndex NOTIFY currentLineIndexChanged)

QAbstractItemModel* textLines() { return &text_lines_; }
int textLineCount() const;                 // text_lines_.rowCount()
int currentLineIndex() const;              // 0-based; -1 when there are no lines (error/pending/no text)
bool quickLookEligible() const;            // gate (3.2); not a Q_PROPERTY: QML never gates (REQ-C-007)
Q_INVOKABLE void moveCurrentLineDown();    // moveCurrentLine(+1)
Q_INVOKABLE void moveCurrentLineUp();      // moveCurrentLine(-1)
void moveCurrentLine(int delta);           // C++ entry used by the controller; clamps; no-op with no lines
signals: void currentLineIndexChanged();   // emitted only when the value actually changes
```

Removed: `textContent` (`Q_PROPERTY`, accessor, `TextPreviewResult::content`). Kept: `hasText`,
`textTruncated`, `textTotalSize`. `setQuickLookActive(bool)` gains the open-transition reset.

Indexing convention (resolved): **0-based** `currentLineIndex` everywhere in C++ and QML state (matches
`ListView` index and model rows). The 1-based number shown in the gutter comes from the model's
`lineNumber` role (row + 1), never computed by QML arithmetic on state.

State rules:
- `current_line_` is `-1` iff there are no lines. When lines arrive: `current_line_ = min(retained_line_, n-1)`.
- `retained_line_` (private) is reset to 0 on: `setQuickLookActive(false->true)`, and `setTarget` with a
  different path. It is preserved across same-path reloads (file edited externally while open), so the
  highlight is clamped rather than jumped to line 1 (open question 8.13).
- `resetDisplayState()` clears the model (`text_lines_.clear()`) and sets `current_line_ = -1`.
- All fields are assigned before any notification (same discipline as `QuickLookPresentationModel`).

### 4.2 TextLineModel (**new**, `text_line_model.h`)

```cpp
class TextLineModel : public QAbstractListModel {
  Q_OBJECT
 public:
  enum Role { LineTextRole = Qt::UserRole + 1, LineNumberRole };  // names: "lineText", "lineNumber"
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex&, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  void setLines(QStringList lines);   // beginResetModel / endResetModel
  void clear();                       // no-op when already empty
};
```
Chosen over exposing a `QStringList` property because a `QStringList` model reset rebuilds all delegates on every
`changed()` (which fires on every selection change and busy flip), while the model only resets when lines
change; and `ListView` virtualizes 100k rows only over a real model.

### 4.3 Router / controller

`FileCommand::Kind` gains `MoveQuickLookLine` (count = +1 / -1). `FileCommandRouter::handleKey`, after the prompt
and Insert/Search checks, adds `if (context_.quickLookOpen) return handleQuickLookKey(key);`:

| key | result |
|---|---|
| `" "` | `ToggleQuickLook` (closes), consumed |
| `"Escape"` | `CloseQuickLook`, consumed |
| `"j"`, `"ArrowDown"` | `MoveQuickLookLine`, count +1, consumed |
| `"k"`, `"ArrowUp"` | `MoveQuickLookLine`, count -1, consumed |
| anything else | pending count/`g`/`y`/`d` cleared, consumed as no-op |

History shortcuts (`Ctrl+O`/`Ctrl+I`) also enter through window shortcuts or the popup key helper.
`NavigationSession::traverseHistory` rejects traversal while Quick Look is open, so these paths and
direct history calls preserve the pinned file. This supersedes navigation-history REQ-F-024
(close Quick Look on history navigation). History works again after Space/Escape closes the overlay.

Count prefixes are deliberately ignored in Quick Look (digits never reach the router through the popup filter;
a count typed before opening is discarded). The catch-all swallow is defense in depth for REQ-F-002
("any other navigation key"): `G`, `gg`, `h`, `Return`, etc. cannot move or open anything behind the overlay.
Outside Quick Look, `"ArrowUp"/"ArrowDown"` are unrecognized and fall through unchanged (listing behavior
untouched; `InspectionKeys` only produces them for `popup == true`).

`DirectoryController::execute`: `case Kind::MoveQuickLookLine: preview_.moveCurrentLine(command.count); break;`
(no `emit changed()`: the listing did not change, and avoiding `changed` avoids a needless `syncPreviewTarget`).

### 4.4 Pinning invariant

While `quick_look_open_`, the only writers of the cursor are the keys above (now no-ops for the cursor) and
external listing changes (watcher refresh, sort/hidden toggles cannot be typed through the popup). If the entry
under `cursor_row_` differs from `preview_target_path_` while open (row shifted by an external create/delete),
`PreviewSelection::syncPreviewTarget` closes Quick Look (`quick_look_open_=false`, `setQuickLookActive(false)`,
`emit changed()`, same pattern as the existing out-of-range branch) and then retargets as it does today.
Removal of the pinned file already closes it (existing test `SelectedFileEdits...RefreshInPlace`).

### 4.5 QML (`QuickLookOverlay.qml`)

```qml
Rectangle {                              // existing bordered frame, visible: root.preview.hasText
  ListView {
    id: viewer
    objectName: "quickLookText"          // REQ-C-001: keeps the text-display role; Keys.forwardTo: [keyContent]
    anchors.fill: parent; anchors.margins: root.cardPadding
    clip: true; interactive: true
    flickableDirection: Flickable.VerticalFlick; boundsBehavior: Flickable.StopAtBounds
    keyNavigationEnabled: false; activeFocusOnTab: false
    model: root.preview.textLines
    readonly property real rowHeight: metrics.height              // TextMetrics on monospace font, constant
    readonly property real gutterWidth: gutterMetrics.width       // "0".repeat(digits(textLineCount))
    delegate: Item {
      id: line
      required property int index
      required property string lineText
      required property int lineNumber
      readonly property bool isCurrent: line.index === root.preview.currentLineIndex
      objectName: "quickLookLine"
      width: ListView.view.width; height: viewer.rowHeight
      Rectangle { objectName: "quickLookCurrentLine"; anchors.fill: parent; visible: line.isCurrent; color: <palette token> }
      Text { objectName: "quickLookLineNumbers"; width: viewer.gutterWidth; text: line.lineNumber; horizontalAlignment: Text.AlignRight; font: monospace }
      Text { objectName: "quickLookLineText"; x: viewer.gutterWidth + gap; width: parent.width - x; text: line.lineText
             wrapMode: Text.NoWrap; elide: Text.ElideNone; clip: true; textFormat: Text.PlainText; font: monospace }
    }
    Connections { target: root.preview
      function onCurrentLineIndexChanged() { if (root.preview.currentLineIndex >= 0) viewer.positionViewAtIndex(root.preview.currentLineIndex, ListView.Contain) } }
    Connections { target: root.preview.textLines
      function onModelReset() { viewer.positionViewAtBeginning() } }
  }
}
// Popup: onOpened: existing forceActiveFocus(); additionally viewer.positionViewAtBeginning()
```
Palette/tokens (highlight color, gutter muted color) come from existing `HoloniightPalette`; the exact tokens are
picked at implementation and verified against the QML lint/token policy. Font is `HolonightTheme.monospaceFont`
/ `monospaceFontSize` as today.

Binding-loop avoidance (REQ-NF-003): no binding reads `contentY`, `contentHeight`, `contentWidth` or
`currentIndex`; delegate height is a constant from `TextMetrics`, delegate width from `ListView.view.width`
(not from content); gutter width depends only on `textLineCount` (bumps only across digit boundaries); the
highlight is a per-delegate bool (no `ListView.highlight` item, so no highlight/`contentY` feedback); scrolling is
imperative in a signal handler.

Wheel vs auto-scroll (REQ-F-012/013): the wheel scrolls `contentY` only. `currentLineIndex` is never written by
QML, so it is unchanged. Auto-scroll fires only when the C++ index changes, using `Contain` (minimal scroll:
nothing moves if the row is already visible). After a wheel scroll, the next j/k that changes the index scrolls
back just enough to show the current row: this is the standard vim/less feel and satisfies both requirements.
A j at the last line (index unchanged, no signal) does not snap the view back.

Caption (REQ-F-016): `metadataText` text branch becomes
`preview.textTruncated ? qsTr("%1 · truncated").arg(SizeFormat.formatSize(preview.size)) : SizeFormat.formatSize(preview.size)`
(replaces the concatenated `qsTr(" · truncated")`, whose leading space is a translation hazard).

### 4.6 InspectionKeys

`normalized(key, text)` gains, guarded by `popup`, `Qt::Key_Up -> "ArrowUp"`, `Qt::Key_Down -> "ArrowDown"`; the popup filter
`input != " " && != "Escape" && != "j" && != "k"` adds the two names. Held-key auto-repeat of j/k/arrows is
allowed (only Space swallows repeats).

## 5. Key decisions and rationale

| # | Decision | Rationale |
|---|---|---|
| D1 | Split lines on the worker, ship `QStringList` | REQ-NF-004; QML must not split (REQ-NF-002); reuse the existing cancellable job and stale-generation drop. |
| D2 | One 102,400-byte read replaces the 64 KiB head read | Docked pane shows no text, so nothing else consumes the old cap; avoids a second file read and duplicate 64 KiB `textContent`. |
| D3 | `QAbstractListModel` over `QStringList` | Reset only when lines change; row roles carry `lineNumber`; virtualization. |
| D4 | 0-based `currentLineIndex`, `-1` when no lines | Matches ListView/model rows; `-1` gives REQ-F-021's "no row highlighted" for free. |
| D5 | Keys stay on the `handleKey` path; router owns pinning | Single dispatch point, existing consistent-key-hints/focus/repeat logic, testable without QML; the router already owns the `quickLookOpen` context. |
| D6 | Highlight via per-delegate bool, scroll via `Connections` + `positionViewAtIndex(Contain)` | No binding loops; wheel independent of current line. |
| D7 | Gate MIME computed on the worker with extension fallback | Lets unreadable text/plain files open (REQ-F-021) without I/O on the UI thread. |
| D8 | Close Quick Look if the cursor entry diverges from the pinned path | Keeps "preview == entry under cursor" true after close (REQ-F-014). |
| D9 | Keep `hasText`/`Kind::Text`; do not touch the presentation model | Card sizing unchanged; smallest blast radius. |

## 6. Alternatives considered

- **QML `ListView.currentIndex: preview.currentLineIndex` + `highlightFollowsCurrentItem`**: rejected; ListView
  writes `currentIndex` on model reset/interaction, risks binding breakage and highlight-vs-`contentY` loops.
- **`QStringList` property or `TextEdit` with per-line rich text**: rejected; full rebuild on every `changed()`,
  selectable text (violates REQ-F-005), logic in QML.
- **One big `Text` with a separate gutter `Text`**: rejected; cannot highlight a row or virtualize, forces height math in QML.
- **Router keeps emitting `Move` and `PreviewSelection` ignores retargeting while open**: rejected; cursor would
  still move, violating REQ-F-002 and REQ-F-014.
- **QML calls `preview.moveCurrentLineDown()` directly from `Keys.onPressed`**: rejected (duplicates gating/pinning
  outside C++; bypasses `InspectionKeys`); the invokables remain public API per REQ-C-005 and are what tests use.
- **Deferred open when MIME is unresolved**: rejected as extra state (violates REQ-F-022 "no other state").
- **Two reads (keep 64 KiB `textContent`, add 100 KiB lines)**: rejected; double I/O and memory for no consumer.

## 7. Tests and verification plan

Fixtures (`tests/preview_fixtures.h`, additive): `writeBytes(dir,name,bytes)`, `writeNumberedLines(dir,name,n)`,
`writeEmptyText`, `writeCrlfText`, `writeCrText`, `writeInvalidUtf8Text`, `writeTextOfSize(dir,name,bytes)`
(50,000 / 102,400 / 102,401 / 200,000), `writeCutMidUtf8Text` (multi-byte char straddling byte 102,400),
`writeSingleLongLine`. `PreviewServiceTestAccess`: no new seam needed; NF-004 uses the existing `beforeDispatch`
(runs on the worker) with a blocking latch while a `QTimer` fires on the UI thread (release < 3 s decode timeout).

New / rewritten tests:
- `text_lines_test.cpp` (**new**): splitter and UTF-8 tail (terminators, empty, `"\n"`, CR/CRLF/LF equivalence, mid-sequence cut, BOM, invalid bytes) - REQ-F-017..020.
- `text_preview_service_test.cpp`: `lines`, 102,400 boundary, truncation flag, unreadable error - REQ-F-015/018/021.
- `preview_service_test.cpp`: `currentLineIndex` init/clamp/signal-once, reset on QL open, retained on same-path reload, `-1` on error, eligibility incl. unreadable and zero-size, NF-004 timer test - REQ-F-006..011/021, REQ-C-005, REQ-NF-001/004.
- `directory_controller_test.cpp`: replace `JAndKKeepUpdatingThePreviewWhileQuickLookStaysOpen` with pinned assertions (cursor row and `preview()->name()` unchanged after 10 j/k/arrows, current line moves, Space/Escape close, cursor unchanged); new gating test (jpg opens; dir, `.json`, `.md`, `.tar.gz` do not; `quickLookOpen` and preview state unchanged) - REQ-F-001/002/014/022, REQ-C-002/003.
- `preview_integration_test.cpp:81` rewrite: open on photo, j keeps photo and cursor 0; close, j, reopen shows text lines.
- Existing tests that press Space right after `settled(controller)` must first wait for preview settled and use an eligible file (`directory_controller_test.cpp` Space/Stage1And2/Escape tests, `smoke.cpp` ~802/~890/~1270 "quicklook" mode harness, `QuickLookHarness` tests); `smoke.cpp:826-853` (`cursorRow()==1` after j) becomes `cursorRow()==0` + `currentLineIndex` movement; the quick-look latency block (~908-913: j while open expects the next image) is rewritten as close / j / reopen.
- `smoke.cpp` rendered: gutter/highlight objects present, highlight row matches `currentLineIndex`, long line clipped and no horizontal scroll, viewport follows index past the first screen, synthetic in-process `QWheelEvent` moves `contentY` with `currentLineIndex` unchanged, 100+ j presses plus resize produce no "Binding loop detected" message (message handler) - REQ-F-003/004/012/013, REQ-NF-003. Real pointer wheel and native focus checks are manual (AGENTS.md: no pointer automation); record as pending.
- Perf guard: single 100 KiB line and 100k one-byte lines both show within budget (risk R2).
- Docs (REQ-C-004): README lines ~58-64 and ~207 rewritten; `docs/BACKLOG.md` note; there is no `docs/user-guide.md`; older SDD cycles (`quick-look-redesign`) stay as history with a superseded note in this cycle's VERIFICATION.

## 8. Implemented decisions and spec deviations

1. **REQ-F-002 pinning vs listing cursor vs existing j/k.** Today j/k in the popup reach `handleKey` -> `Move` -> cursor -> `syncPreviewTarget`. Resolution: the router's QL-open branch consumes j/k/arrows as `MoveQuickLookLine` and swallows every other key; the cursor cannot move from keys. External listing shifts that would retarget the preview close Quick Look instead (4.4). Deviation: spec says "listing remains frozen", but external changes cannot be frozen; closing is the least surprising.
2. **REQ-F-001/022 vs REQ-F-021 (gating needs the worker MIME).** Resolution in 3.2: worker-reported `gate_mime` with extension fallback on open failure; Space while the MIME is still unresolved is a consumed no-op (the worker job is milliseconds; tests wait for settle). `application/x-zerosize` is also eligible so an empty file with no `.txt` extension still opens as one empty line (REQ-F-017's intent). This is an implemented deviation from the spec's strict "text/plain only" wording, recorded in VERIFICATION.md.
3. **REQ-C-007 literal "router checks `mimeType == text/plain`".** The check lives in `PreviewService::quickLookEligible()` (C++), consumed by `canPreviewSelection()`, because the fallback (item 2) needs worker data. QML still has no MIME checks.
4. **REQ-F-016 caption wording.** Keep `SizeFormat` output (the app formats SI, e.g. "8.3 MB"; the spec's "KiB" examples are illustrative) and append `" · truncated"` via a single `%1 · truncated` string. Requirement is the suffix, not the unit.
5. **REQ-F-021 "empty viewer body".** `previewErrorKind != None` already classifies as the compact icon card and shows the error in the caption. Resolution: keep that card instead of an empty text frame; `currentLineIndex == -1`, j/k are consumed no-ops. No rows exist to highlight.
6. **REQ-F-005 selection/copy.** `Text` items are not selectable and the popup key filter never forwards Ctrl+C, so nothing copies. Verify no window-level Ctrl+C `Shortcut` fires while the modal popup is open (Main.qml audit at implementation; add a regression assertion if one exists).
7. **REQ-NF-003 wheel vs auto-scroll.** Imperative `Contain` scroll only when the C++ index changes; wheel never touches it; documented behavior: the next effective j/k re-reveals the current row (4.5).
8. **"Last loaded line", trailing terminator and truncation (REQ-F-011/015/020).** Rules in 3.1: partial last line kept; cap after a terminator adds no empty line; a UTF-8 sequence cut by the cap has its incomplete tail dropped (<= 3 bytes, so "first 102,400 bytes" is exact only up to that trim); a `\r` at the cut is a terminator. REQ-F-011's example ("500 lines ... last loaded line 1200") is internally inconsistent and is read as "the last line of the loaded prefix".
9. **REQ-C-005 vs REQ-NF-001 naming.** REQ-C-005 is used: property `currentLineIndex` (0-based, `-1` = none), notify `currentLineIndexChanged` (the spec's "currentLineChanged" in Implementation Notes is renamed to the Qt property-notify convention), `Q_INVOKABLE moveCurrentLineDown()/Up()`. REQ-NF-001's `moveCurrentLine(int)` exists as a public C++ method but is not `Q_INVOKABLE`. Spec's `resetCurrentLine()` is not public API: reset is internal on QL open / path change.
10. **REQ-F-023 / REQ-C-006 premise is false.** The docked pane has no text panel and nothing but Quick Look reads `textContent`. Resolution: the 64 KiB `kTextHeadBytes` and `textContent` are removed; acceptance becomes "PreviewPane.qml is unmodified and still shows no text or line UI, and opening Quick Look does not change it". This is an implemented deviation from the spec premise that the pane "continues to display text", recorded in VERIFICATION.md.
11. **REQ-NF-002 "QML forwards keys to PreviewService".** Keys go through `InspectionKeys` -> `controller.handleKey` -> router -> `PreviewService::moveCurrentLine`; the public invokables satisfy REQ-C-005's interface but QML key handlers do not call them directly (REQ-C-005 acceptance text "QML calls `preview.moveCurrentLineDown()`" is met by API presence, not call site).
12. **REQ-F-009/010 arrows.** `InspectionKeys` does not pass arrows today (popup filter, empty `event.text`); fixed in 4.6, popup-only.
13. **Reload while open.** External edit of the pinned file re-runs `setTarget`; the current line is clamped, not reset (spec only mandates reset on open).
14. **REQ-F-018 details.** Leading BOM stripped; U+0000 inside text (possible past the 8 KiB binary sniff) is replaced by U+FFFD so it renders; both are outside spec, low risk.
15. **Existing 3 s decode timeout** applies to text and reports "Cannot decode image". Pre-existing; not changed here; noted for follow-up.

## 9. Known risks

- R1: Requiring resolved MIME before Space changes behavior for fast Space-after-navigation (was: opens with spinner). Mitigation: worker job is tiny; revisit deferred open if native acceptance complains.
- R2: A single 100 KiB line or 100k rows in a `ListView`/`Text` may be slow to lay out. Mitigation: perf tests above; fallback is a per-line display cap in `TextLineModel::data` (would need spec approval, non-goal says lines are not trimmed).
- R3: `positionViewAtIndex` on the first frame after open/reset before delegates exist. Mitigation: `onOpened` plus `onModelReset` calls, verified in a rendered test; `Qt.callLater` if needed.
- R4: Removing `textContent` / `TextPreviewService::TextPreviewResult::content` breaks existing tests that read them; those tests are updated in the same change.
- R5: `QMimeDatabase` result for an empty file differs by extension (`.txt` glob vs `application/x-zerosize`); covered by test and item 2.
- R6: Flickable drag-scroll with a mouse is still enabled; it scrolls, never selects. Kinetic flick after drag is acceptable; disable via `interactive`+`WheelHandler` only if native review objects.
- R7: Hint row still shows only "Space/Esc Close"; adding line-navigation hints is out of scope.

## 10. Requirement traceability

| REQ | Components / evidence |
|---|---|
| F-001 | `PreviewService::quickLookEligible`, `PreviewSelection::canPreviewSelection`, router Space branch; gating test |
| F-002 | Router QL-open branch (4.3), `PreviewSelection` pin guard (4.4); pinned controller/integration/smoke tests |
| F-003 | `TextLineModel` `lineNumber` role, delegate gutter + highlight; smoke render test |
| F-004 | Delegate `Text` `NoWrap` + clip, `contentWidth` unset, vertical-only flick; smoke test |
| F-005 | `Text` (non-selectable), popup key filter; item 8.6 |
| F-006 | `setQuickLookActive` reset, `retained_line_`; preview_service test |
| F-007, F-008 | `moveCurrentLine`, router j/k mapping; tests |
| F-009, F-010 | `InspectionKeys` arrow normalization, router arrow mapping; tests |
| F-011 | Clamp in `moveCurrentLine`; boundary tests (3-line, truncated) |
| F-012 | `Connections` + `positionViewAtIndex(Contain)`; smoke test |
| F-013 | ListView wheel scroll, no C++ write; smoke synthetic wheel test |
| F-014 | Router Space/Escape, existing focus restore, pin guard; controller test |
| F-015 | `kTextViewerMaxBytes`, `readHead`, `wasTruncated`; size-boundary tests |
| F-016 | `metadataText` `%1 · truncated`; smoke caption test |
| F-017 | `[""]` synthesis for empty bytes; splitter + smoke test |
| F-018 | `TextLines::decodeUtf8`, tail trim; splitter test |
| F-019 | `TextLines::splitLines`; three-fixture test |
| F-020 | `splitLines` terminator rule; splitter test |
| F-021 | Worker `gate_mime` fallback, error caption, `currentLineIndex == -1`; chmod 000 test |
| F-022 | Router no-op when `!canPreview`; gating test asserts no state change |
| F-023 | `PreviewPane.qml` unmodified; item 8.10 |
| NF-001 | `PreviewService` state and methods (4.1); QML has no local current-line state |
| NF-002 | QML (4.5) has only bindings and one `Connections`; review checklist |
| NF-003 | Binding-loop rules in 4.5; 100-press + resize log test |
| NF-004 | Worker-side read/decode/split; `beforeDispatch` latch + timer test |
| C-001 | `quickLookText` (ListView), `quickLookLineNumbers`, `quickLookLine`, `quickLookCurrentLine`, `quickLookLineText`; smoke findChild |
| C-002 | Rewrites listed in section 7 |
| C-003 | New tests listed in section 7 for all seven behaviors |
| C-004 | README / BACKLOG updates (section 7) |
| C-005 | 4.1 interface; naming resolution 8.9 |
| C-006 | Worker-only cap; item 8.10 (no docked consumer exists) |
| C-007 | Router/controller/`PreviewService::quickLookEligible` (C++); no QML MIME checks; item 8.3 |
