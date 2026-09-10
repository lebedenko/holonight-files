#pragma once

#include <QString>

// Pure validation logic for names typed in INSERT mode (rename or create), covering SPEC.md
// REQ-F-030 through REQ-F-038's nine ordered checks (order matters: the first violation wins).
// No QObject, no worker thread, no filesystem I/O beyond a single QDir::exists() collision
// check — kept free-standing so it's unit-testable without constructing VimModeController,
// DirectoryController, or any QML (docs/sdd/vim-modal-editing/DESIGN.md).
struct NameValidationResult {
  bool valid = false;
  // "./" prefix stripped, trailing "/" directory marker stripped. Empty when !valid.
  QString normalizedName;
  bool createsDirectory = false;
  // Empty when valid.
  QString errorMessage;
};

// directoryPath: the directory the name would be created/renamed into (used only for the
// collision check).
// selfName: the entry's own current name when renaming (empty when creating via o/O). A
// committed name equal to selfName is accepted rather than rejected as a collision — that's the
// unchanged-name "touch" case (REQ-F-012/REQ-F-038), decided by the caller, not by this function.
NameValidationResult validateName(const QString& rawInput, const QString& directoryPath, const QString& selfName = {});
