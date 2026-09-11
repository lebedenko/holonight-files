#pragma once

#include <QStringList>

// The single, unnamed clipboard register (SPEC.md REQ-C-002): the paths from the most recent
// yy/dd/y/d operation and whether it was a cut. No history, no QObject — owned as a plain value
// member of DirectoryController (never by VimModeController, which stays free of filesystem
// paths; see vim_mode_controller.h).
struct ClipboardRegister {
  QStringList paths;
  bool cut = false;
  void clear() {
    paths.clear();
    cut = false;
  }
};
