# Quick Look C++ presentation specification

Status: approved by the user's instruction to implement the proposed plan (2026-09-16).
Implementation and automated verification are complete; native 1.5x acceptance
remains pending (see VERIFICATION.md).
This behavior-preserving cycle supersedes the JavaScript implementation choices in
quick-look-redesign; its visual and interaction requirements remain applicable.

- **REQ-001:** The application shall implement Quick Look classification, retained
  layout state and geometry calculations in C++, exposing typed properties to QML.
- **REQ-002:** When preview data changes, the model shall classify it using the existing
  precedence: absent, error, empty MIME, image, text, busy, compact. Empty MIME shall
  remain pending even before the service sets busy.
- **REQ-003:** While the selection is pending or absent, the model shall retain the
  previous layout kind and source dimensions, using compact layout initially. When
  bounds or measurements change, it shall refit those inputs synchronously, including
  while the popup is hidden. Settled data shall replace retained inputs without animation.
- **REQ-004:** The model shall preserve the existing 92% window bounds, aspect-fit,
  caption reserve, compact width, portrait minimum width and pixel rounding formulas.
  Geometry shall not depend on its own output sizes.
- **REQ-005:** After QML construction, when a positive rounded decode size changes or
  another preview service is attached, the model shall submit the available preview
  bounds multiplied by DPR to the QuickLook consumer. Invalid/nonpositive DPR shall
  fall back to 1. Preview changes alone shall not submit requests. The controller
  shall continue to choose the active consumer.
- **REQ-006:** When the service is replaced or destroyed, the model shall disconnect
  safely and reset retained state. Output fields shall be consistent before notifying QML.
- **REQ-007:** QML shall retain visuals, theme bindings, measurement, centering, caption
  formatting and input handling; other JavaScript helpers shall remain outside scope.
- **REQ-008:** Direct C++ tests and existing rendered tests shall verify the refactor.
  Build, formatting, lint, license, staged install/uninstall and isolated runtime
  checks shall be recorded, with unavailable native 1.5x acceptance explicitly pending.

No preview decode redesign, sibling-source changes or app-wide JavaScript migration.
