# DESIGN: Consistent key hints

Adopt HnKeyHint.keyGroups using explicit Qt key constants after the provider is published and pinned. Remove local keycap painting and symbol sizing. Use shared wrapping for constrained help. Keep existing action handlers and validation messages; update size measurements and accessibility composition to match the shared badge.

## Adoption sites

- `apps/files/qml/inspection/QuickLookOverlay.qml`: replace the literal Space/Esc
  closing instruction with shared alternatives and a translated Close label.
  Recalculate caption and overlay measurements using the badge's implicit height.
- `apps/files/qml/status/ModeStatusBar.qml`: valid insert guidance uses Return and
  Escape badges plus translated Confirm/Cancel action labels. Invalid input still
  presents the original validation error message, error color and available width.

No browsing, modal-state or filesystem operation logic changes. Preserve keyboard
handlers, error precedence, elision and mode-dependent visibility.
Verification: focused inspection and insert-mode regressions, `task check`, required
isolated runtime acceptance and manual native review. No system installation.
