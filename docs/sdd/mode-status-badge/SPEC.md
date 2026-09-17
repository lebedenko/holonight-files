# Mode Status Badge — SPEC

## Overview

HoloNight Files shall display a persistent, mode-aware status badge in the footer status line (`ModeStatusBar.qml`) that visually indicates the current vim-style mode (NORMAL, VISUAL, INSERT, or SEARCH). The badge is a pill-shaped, non-interactive label positioned at the left edge of the status bar, with a fill color and text that change according to the active mode. It replaces redundant mode-name prefixes in other footer labels and provides at-a-glance mode feedback without consuming keyboard focus or altering input dispatch.

## Scope & Non-goals

### In Scope
- Mode badge component in `ModeStatusBar.qml` (footer status line)
- Inset pill styling (rounded rectangle with padding and fill)
- Four mode labels: "NORMAL", "VISUAL", "INSERT", "SEARCH" (uppercase, not translated)
- Mode-specific fill colors (accentBlue, accentViolet, success, accentYellow)
- Binding to `controller.vim.currentMode` for real-time mode updates
- Removal of redundant "VISUAL  ·  " and "INSERT  ·  " prefixes from existing footer labels
- Smoke-test coverage for all four modes
- Vertical centering within existing footer padding

### Not in Scope
- Tilde-abbreviated path display
- Item count or selection summary in badge
- Selected-file size summary
- Clickable or hoverable badge interaction
- Corner-flush or edge-aligned badge geometry (inset only)
- Translated mode names (English only)
- Changes to `VimModeController`, palette definition, or theme engine
- New standalone `.qml` file (changes only to `ModeStatusBar.qml`)
- C++ production code changes

## Functional Requirements

### REQ-F-001 — Badge Presence and Positioning
**Ubiquitous**: The footer status bar shall include a mode status badge as the first (leftmost) child of its RowLayout, positioned before all existing mode-contextual labels.

**Acceptance**: Verify via QML inspection and smoke test that the badge object (objectName "modeBadge") is the first child and is visible at module load.

---

### REQ-F-002 — Mode Label Display
**Ubiquitous**: The badge shall display one of exactly four text labels—"NORMAL", "VISUAL", "INSERT", or "SEARCH"—each exactly 6 characters in uppercase, rendered in `HolonightTheme.monospaceFont`.

**State-driven**: While the mode is NORMAL, the label text shall be "NORMAL"; while VISUAL, "VISUAL"; while INSERT, "INSERT"; while SEARCH, "SEARCH".

**Acceptance**: Smoke test verifies that when `vim.currentMode` is set to each mode, the displayed badge text matches the expected label. Verify no translation strings (no `qsTr()`) are applied to the label text.

---

### REQ-F-003 — Mode-Specific Fill Colors
**State-driven**: While the mode is NORMAL, the badge fill shall be `HoloniightPalette.accentBlue`; while VISUAL, `accentViolet`; while INSERT, `success`; while SEARCH, `accentYellow`.

**Acceptance**: Smoke test reads the badge fill color (via QML Color property) after each mode switch and asserts it equals the expected palette color constant. The expected colors are read from the live `HoloniightPalette` singleton in the test, not hardcoded.

---

### REQ-F-004 — Text Color and Contrast
**Ubiquitous**: The badge text color shall be `HoloniightPalette.background` (a dark color) in all modes to ensure consistent readability.

**Constraints**: The contrast ratio between text and fill shall be ≥ 4.5:1 (WCAG AA level) for every active fill color in the deployed palette.

**Acceptance**: Measure the computed contrast ratio for each mode fill color against the proposed text color; document results in an appendix. Perform manual visual verification on Hyprland @1.5 scale that text is legible in all four modes.

---

### REQ-F-005 — Badge Reactivity to Mode Changes
**Event-driven**: When `controller.vim.currentMode` changes to a new mode (via keyboard input, a completed search, a visual selection, or mode-exit), the badge text and fill color shall update in the same event-loop turn, with no change to badge width or x-position.

**Acceptance**: Smoke test programmatically changes the mode four times in sequence and asserts, after each change and a single event-loop spin, that the badge text and color already match the new mode.

---

### REQ-F-006 — Visibility During Task Prompts
**State-driven**: While a TaskManager task is busy or a conflict/trash-confirm prompt is open, the badge shall remain visible and display the current mode (NORMAL in practice, because task execution is asynchronous but starts from and leaves VimModeController in NORMAL).

**Acceptance**: Smoke test opens a trash-confirm prompt (`tasks.hasPrompt === true`) and asserts `modeBadge.visible === true` and its text is "NORMAL".

---

### REQ-F-007 — Pill Styling and Centering
**Ubiquitous**: The badge shall be styled as an inset pill—a rounded rectangle with horizontal and vertical padding, rounded using `HnAppearance.roundedRadius(HnSurfaceRole.Pill, w, h, HnAppearance.revision)`, and vertically centered within the footer's padding.

**Acceptance**: Smoke test asserts the badge's vertical center is within 1 px of the ModeStatusBar's vertical center and its radius equals the `HnSurfaceRole.Pill` radius. Crispness at 1.5× is confirmed by the manual native Hyprland check (offscreen rendering does not reproduce DPR issues).

---

### REQ-F-008 — Removal of Redundant Prefixes
**Ubiquitous**: The footer shall remove the "VISUAL  ·  " prefix from `visualStatusLabel` (keeping only the selection count) and the "INSERT  ·  " prefix from `insertStatusLabel` (keeping the Enter/Esc hint and validation error message).

**Acceptance**: Smoke test verifies that in VISUAL mode, the label text does not begin with "VISUAL  ·  " and still contains the selection count. In INSERT mode, the label does not begin with "INSERT  ·  " and still shows the hint text or error.

---

### REQ-F-009 — Accessibility and ARIA Properties
**Ubiquitous**: The badge shall have an accessible name matching the translated pattern "%1 mode" (e.g., "NORMAL mode") and shall declare its role as `StaticText`. The surrounding accessible-name text shall use `qsTr("%1 mode").arg(label)`; only the mode label remains untranslated.

**State-driven**: When the mode changes, the accessible name shall update to match the new mode (e.g., from "NORMAL mode" to "VISUAL mode").

**Acceptance**: Verify via QML Accessible properties that `name` and `role` properties are set correctly and change with mode updates. Inspect that the accessible-name template uses `qsTr()` while the mode label stays literal. Use an accessibility inspector to confirm the label is announced correctly.

---

### REQ-F-010 — Display-Only Interaction Model
**Ubiquitous**: The badge shall not respond to mouse events, shall not accept keyboard focus, and shall not alter keyboard input dispatch (all keystroke routing remains unchanged).

**Acceptance**: Smoke test simulates mouse clicks on the badge and verifies no event handler fires and focus remains unaffected. Also verify by inspection that the badge contains no MouseArea/TapHandler and has `activeFocusOnTab: false`.

---

## Non-Functional Requirements

### REQ-NF-001 — Constant Width Across Mode Switches
**Ubiquitous**: The badge width and x-position (layout column offset) shall remain identical when switching between any two modes.

**Acceptance**: Smoke test measures the badge width and x-position in each of the four modes and asserts all widths are equal and all x-positions are equal. Store baseline values after the first mode load and compare on every transition.

---

### REQ-NF-002 — Code Quality and Style Conformance
**Ubiquitous**: The badge implementation shall pass all existing project linters (QML format-check and qml-lint) without new errors or warnings.

**Acceptance**: Run `task format-check` and `task qml-lint` after implementation; verify exit code is 0 and no new diagnostics appear in the affected file.

---

### REQ-NF-003 — Test Coverage
**Ubiquitous**: The badge implementation shall be covered by at least one smoke test per mode, plus a cross-mode jitter test and a prompt-visibility test.

**Acceptance**: Verify that `tests/smoke.cpp` contains at least 4 test cases (one per mode) plus 2 additional integration tests (jitter, prompt visibility). All tests pass with GoogleTest framework assertions (`EXPECT_*`).

---

## Constraints

### REQ-C-001 — File and Architecture Scope
**Ubiquitous**: The mode badge implementation shall be contained entirely within `apps/files/ModeStatusBar.qml`. No new `.qml` files, no C++ changes to production code, no modifications to palette definitions or theme engine.

**Acceptance**: Verify via git diff that only `apps/files/ModeStatusBar.qml`, `tests/smoke.cpp`, affected baseline files, and supporting documentation are modified. No new source files are created.

---

### REQ-C-002 — Use of Standard Palette and Theme
**Ubiquitous**: The badge shall use only existing palette colors (`HoloniightPalette.accentBlue`, `accentViolet`, `success`, `accentYellow`, `background`) and the standard monospace font (`HolonightTheme.monospaceFont`).

**Acceptance**: Grep the implementation for any color literal or hardcoded values; verify all fill colors and text color reference the palette singleton. Verify font is `HolonightTheme.monospaceFont`.

---

### REQ-C-003 — ObjectName for Testability
**Ubiquitous**: The badge root Control shall have `objectName: "modeBadge"` to enable QML test discovery by path (`find(objectName: "modeBadge")`).

**Acceptance**: Verify in smoke test that the badge is found via `find(objectName: "modeBadge")` and that the found object is a Control with an HnLabel content item and a Rectangle background.

---

### REQ-C-004 — Label Constants and Translation Exclusion
**Ubiquitous**: Each mode label ("NORMAL", "VISUAL", "INSERT", "SEARCH") shall be a plain string literal, not wrapped in `qsTr()` or any translation function.

**Acceptance**: Grep the implementation for "NORMAL", "VISUAL", "INSERT", "SEARCH"; verify each is a plain string constant, not a function call. Verify no `qsTr()` or `i18n()` wrapper is applied.

---

## Traceability & Acceptance Summary

| Requirement ID | Component | Acceptance Criterion | Test Type |
|---|---|---|---|
| REQ-F-001 | Presence & Position | Badge is first RowLayout child, visible, objectName "modeBadge" | Smoke test + QML inspection |
| REQ-F-002 | Label Display | Text matches expected 6-char uppercase string per mode, monospace font | Smoke test (4 modes) |
| REQ-F-003 | Fill Colors | Color equals palette constant for each mode | Smoke test (4 modes, Color comparison) |
| REQ-F-004 | Text Color & Contrast | Text is background color, contrast ratio ≥ 4.5:1 vs. all fills | Manual visual check + contrast calculator |
| REQ-F-005 | Reactivity | Mode change updates text and color without jitter | Smoke test (4-mode cycle) |
| REQ-F-006 | Visibility During Tasks | Badge remains visible and correct during task/prompt | Smoke test (task + prompt scenario) |
| REQ-F-007 | Pill Styling & Centering | Rounded corners, vertical centering, crisp rendering | Manual native Hyprland check |
| REQ-F-008 | Remove Redundant Prefixes | VISUAL and INSERT labels no longer prefixed, counts/hints remain | Smoke test (VISUAL and INSERT mode) |
| REQ-F-009 | Accessibility | Accessible name "<Mode> mode", role StaticText, updates with mode | Accessibility inspector + smoke test |
| REQ-F-010 | Display-Only | No mouse response, no focus capture, no keyboard dispatch | Smoke test (click + focus simulation) |
| REQ-NF-001 | Constant Width | Width and x-position identical across all 4 modes | Smoke test (measure + assert equality) |
| REQ-NF-002 | Linter Compliance | format-check and qml-lint pass with no new errors | Task run (format-check, qml-lint) |
| REQ-NF-003 | Test Coverage | ≥ 4 mode tests + 2 integration tests (jitter, prompt) in smoke.cpp | GoogleTest grep + ctest run |
| REQ-C-001 | File Scope | Only ModeStatusBar.qml, smoke.cpp, baseline files, and supporting documentation modified | Git diff inspection |
| REQ-C-002 | Palette & Theme Reuse | All colors and font from HoloniightPalette and HolonightTheme | Grep inspection + visual check |
| REQ-C-003 | ObjectName | modeBadge objectName present and discoverable | Smoke test (find by objectName) |
| REQ-C-004 | Translation Exclusion | Label strings are plain literals, no qsTr() wrapper | Grep inspection |

### Smoke Test Coverage
The following smoke tests shall be added to `tests/smoke.cpp`:

1. **test_ModeBadge_NormalMode** — Verify badge shows "NORMAL" text with accentBlue fill.
2. **test_ModeBadge_VisualMode** — Verify badge shows "VISUAL" text with accentViolet fill.
3. **test_ModeBadge_InsertMode** — Verify badge shows "INSERT" text with success fill.
4. **test_ModeBadge_SearchMode** — Verify badge shows "SEARCH" text with accentYellow fill.
5. **test_ModeBadge_WidthConstant** — Cycle through all modes, verify width and x-position are unchanged.
6. **test_ModeBadge_VisibleDuringPrompt** — Simulate a task prompt, verify badge remains visible.
7. **test_ModeBadge_RemovesPrefixes** — Verify VISUAL and INSERT mode labels no longer contain "VISUAL  ·  " or "INSERT  ·  " prefix.

### Manual Verification Checklist

Status and recorded evidence: [VERIFICATION.md](VERIFICATION.md). Native checks remain pending.
- [ ] Render on Hyprland @1.5 DPR and confirm pill is crisp and vertically centered.
- [ ] Perform four-mode cycle (NORMAL → VISUAL → SEARCH → INSERT → NORMAL) and visually confirm no jitter.
- [ ] Verify contrast ratio (text on fill) meets WCAG AA (≥ 4.5:1) in all four mode colors.
- [ ] Confirm mode names are uppercase and exactly 6 characters in monospace rendering.
- [ ] Check that file-picker footer does not display "VISUAL  ·  " or "INSERT  ·  " prefixes after removal.

---

## Notes

### Design Rationale
The mode badge provides immediate, glanceable feedback on the current vim-style mode without requiring the user to read a changing label elsewhere in the footer. By removing redundant "VISUAL  ·  " and "INSERT  ·  " prefixes from other labels and consolidating mode indication in a single badge, the footer is simplified and focus is directed to mode-specific information (selection count, prompt, search term).

### Palette Color Justification
The choice of fill colors aligns with vim conventions and the HoloNight design system:
- **NORMAL** (accentBlue): Neutral, "ready" state.
- **VISUAL** (accentViolet): Active selection, contrasts with normal.
- **INSERT** (success): Positive, "entering data" state.
- **SEARCH** (accentYellow): Attention-grabbing, query active.

The text color (`background`, a dark tone) is chosen to maximize contrast against all four fills in the current palette and remain stable if palette colors are adjusted in the future.

### Future Extensibility
If the badge is later extended to include counts or additional text, the 6-character label ensures the base width is predictable. The objectName "modeBadge" allows future CSS-like styling rules or animation bindings without breaking the current layout.
