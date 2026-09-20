#include "file_command_router.h"

#include <limits>
namespace {
constexpr qint64 kPendingGTimeoutMs = 600;
}
FileCommand FileCommandRouter::route(const QString& key, FileCommandContext context) {
  context_ = context;
  command_ = {};
  command_.consumed = handleKey(key);
  return command_;
}
void FileCommandRouter::reset() {
  pending_count_ = 0;
  has_pending_count_ = pending_g_ = pending_y_ = pending_d_ = false;
}
int FileCommandRouter::takeCount() {
  const int count = has_pending_count_ ? pending_count_ : 1;
  pending_count_ = 0;
  has_pending_count_ = false;
  return count;
}
bool FileCommandRouter::handleCountAndMotionKeys(const QString& key, bool isDigit) {
  if (isDigit) {
    pending_count_ = static_cast<int>(
        qMin<qint64>(std::numeric_limits<int>::max(), (qint64{pending_count_} * 10) + (key.at(0).unicode() - u'0')));
    has_pending_count_ = true;
    return true;
  }
  if (key == u"g") {
    if (pending_g_) {
      pending_g_ = false;
      takeCount();
      command_.kind = FileCommand::Kind::First;
      return true;
    }
    pending_g_ = true;
    pending_g_timer_.start();
    return true;
  }
  pending_g_ = false;
  if (key == u"G") {
    takeCount();
    command_.kind = FileCommand::Kind::Last;
    return true;
  }
  if (key == u"j") {
    command_ = {.kind = FileCommand::Kind::Move, .count = takeCount()};
    return true;
  }
  if (key == u"k") {
    command_ = {.kind = FileCommand::Kind::Move, .count = -takeCount()};
    return true;
  }
  return false;
}
bool FileCommandRouter::handleNormalToggleAndNavigationKey(const QString& key) {
  if (key == u"." || key == u"s") {
    takeCount();
    if (key == u".") {
      command_.kind = FileCommand::Kind::ToggleHidden;
    } else {
      command_.kind = FileCommand::Kind::ToggleSort;
    }
    return true;
  }
  if (key == u"h") {
    takeCount();
    command_.kind = FileCommand::Kind::Parent;
    return true;
  }
  if (key == u"l" || key == u"Return" || key == u"Enter" || key == u"\r") {
    takeCount();
    command_.kind = FileCommand::Kind::Open;
    return true;
  }
  if (key == u" ") {
    takeCount();
    if (!context_.quickLookOpen && !context_.canPreview) {
      return true;
    }
    command_.kind = FileCommand::Kind::ToggleQuickLook;
    return true;
  }
  if (key == u"Escape") {
    if (!context_.quickLookOpen) {
      return false;  // Let the window-level Shortcut handle fullscreen.
    }
    takeCount();
    command_.kind = FileCommand::Kind::CloseQuickLook;
    return true;
  }
  return false;
}
bool FileCommandRouter::handleModeTransitionKey(const QString& key) {
  if (key == u":") {
    takeCount();
    return true;  // REQ-C-006: no command palette this cycle
  }
  if (key == u"v" || key == u"V") {
    takeCount();
    command_.kind = FileCommand::Kind::Visual;
    return true;
  }
  if (key == u"i" || key == u"I") {
    takeCount();
    command_.kind = FileCommand::Kind::RenamePrepend;
    return true;
  }
  if (key == u"a" || key == u"A") {
    takeCount();
    command_.kind = FileCommand::Kind::RenameAppend;
    return true;
  }
  if (key == u"o") {
    takeCount();
    command_.kind = FileCommand::Kind::CreateBelow;
    return true;
  }
  if (key == u"O") {
    takeCount();
    command_.kind = FileCommand::Kind::CreateAbove;
    return true;
  }
  if (key == u"/") {
    takeCount();
    command_.kind = FileCommand::Kind::Search;
    return true;
  }
  if (key == u"n" || key == u"N") {
    takeCount();
    command_.kind = key == u"n" ? FileCommand::Kind::NextMatch : FileCommand::Kind::PreviousMatch;
    return true;
  }
  return false;
}
bool FileCommandRouter::handleNormalOnlyKey(const QString& key) {
  if (key == u"Escape" && !context_.quickLookOpen) {
    return false;  // Let the window-level Shortcut handle fullscreen; count intentionally untouched.
  }
  if (handleNormalToggleAndNavigationKey(key)) {
    return true;
  }
  if (handleModeTransitionKey(key)) {
    return true;
  }
  takeCount();
  return false;
}
bool FileCommandRouter::handleKey(const QString& key) {
  // File-operations (SPEC.md docs/sdd/file-operations/SPEC.md): a pending prompt captures every
  // key exclusively, ahead of mode dispatch entirely (REQ-F-021's "paused... does not proceed
  // until resolved" reads as exclusive key capture — new p/D presses during an open prompt are
  // simply consumed, not queued as additional operations).
  if ((context_.prompt != TaskManager::PromptKind::None)) {
    return handlePromptKey(key);
  }

  const auto mode = context_.mode;
  if (mode == VimModeController::Mode::Insert || mode == VimModeController::Mode::Search) {
    // Keyboard focus lives on the inline editor / search field now; QML never routes their key
    // events through here (REQ-C-001), but stay a safe no-op regardless.
    return false;
  }
  if (context_.quickLookOpen) {
    return handleQuickLookKey(key);
  }

  if (pending_g_ && pending_g_timer_.elapsed() > kPendingGTimeoutMs) {
    pending_g_ = false;
  }
  if (pending_y_ && pending_y_timer_.elapsed() > kPendingGTimeoutMs) {
    pending_y_ = false;
  }
  if (pending_d_ && pending_d_timer_.elapsed() > kPendingGTimeoutMs) {
    pending_d_ = false;
  }

  const bool isVisual = mode == VimModeController::Mode::Visual;
  if (handleFileOperationKey(key, isVisual)) {
    return true;
  }

  const bool isDigit = key.size() == 1 && key.at(0) >= u'0' && key.at(0) <= u'9';
  const bool isMotionKey = key == u"g" || key == u"G" || key == u"j" || key == u"k";
  if (isVisual) {
    if (key == u"Escape") {
      takeCount();
      command_.kind = FileCommand::Kind::ExitVisual;
      return true;
    }
    if (!isDigit && !isMotionKey) {
      // REQ-C-004: VISUAL has no other operation consumer this stage — every key besides a
      // count-prefixed motion, Escape, or a file-operation key above is swallowed as a no-op.
      takeCount();
      return true;
    }
  }

  if (handleCountAndMotionKeys(key, isDigit)) {
    return true;
  }

  // Everything past this point is NORMAL-only: VISUAL already returned above for every key
  // except digits/g/G/j/k, all handled by handleCountAndMotionKeys() by now.
  if (mode == VimModeController::Mode::Normal) {
    return handleNormalOnlyKey(key);
  }
  takeCount();
  return false;
}
// Quick Look pins the previewed file: only close and line-movement keys act, and every other key is
// swallowed so nothing can move or open anything behind the overlay. Count prefixes are ignored.
bool FileCommandRouter::handleQuickLookKey(const QString& key) {
  reset();
  if (key == u" ") {
    command_.kind = FileCommand::Kind::ToggleQuickLook;
  } else if (key == u"Escape") {
    command_.kind = FileCommand::Kind::CloseQuickLook;
  } else if (key == u"j" || key == u"ArrowDown") {
    command_ = {.kind = FileCommand::Kind::MoveQuickLookLine, .count = 1};
  } else if (key == u"k" || key == u"ArrowUp") {
    command_ = {.kind = FileCommand::Kind::MoveQuickLookLine, .count = -1};
  }
  return true;
}
bool FileCommandRouter::handleFileOperationKey(const QString& key, bool isVisual) {
  if (key != u"y") {
    pending_y_ = false;
  }
  if (key != u"d") {
    pending_d_ = false;
  }
  if (isVisual) {
    if (key == u"y") {
      takeCount();
      command_ = {.kind = FileCommand::Kind::Yank, .count = 1, .visual = true};
      return true;
    }
    if (key == u"d") {
      takeCount();
      command_ = {.kind = FileCommand::Kind::Cut, .count = 1, .visual = true};
      return true;
    }
    if (key == u"D") {
      takeCount();
      command_ = {.kind = FileCommand::Kind::Trash, .count = 1, .visual = true};
      return true;
    }
    return false;
  }
  if (key == u"y") {
    if (pending_y_) {
      pending_y_ = false;
      takeCount();
      command_ = {.kind = FileCommand::Kind::Yank, .count = 1, .visual = false};
      return true;
    }
    pending_y_ = true;
    pending_y_timer_.start();
    return true;
  }
  pending_y_ = false;
  if (key == u"d") {
    if (pending_d_) {
      pending_d_ = false;
      takeCount();
      command_ = {.kind = FileCommand::Kind::Cut, .count = 1, .visual = false};
      return true;
    }
    pending_d_ = true;
    pending_d_timer_.start();
    return true;
  }
  pending_d_ = false;
  if (key == u"p") {
    takeCount();
    command_.kind = FileCommand::Kind::Paste;
    return true;
  }
  if (key == u"D") {
    takeCount();
    command_ = {.kind = FileCommand::Kind::Trash, .count = 1, .visual = false};
    return true;
  }
  return false;
}
bool FileCommandRouter::handlePromptKey(const QString& key) {
  switch (context_.prompt) {
    case TaskManager::PromptKind::Conflict:
      if (key == u"s") {
        command_.kind = FileCommand::Kind::Skip;
      } else if (key == u"o") {
        command_.kind = FileCommand::Kind::Overwrite;
      } else if (key == u"r") {
        command_.kind = FileCommand::Kind::AutoRename;
      } else if (key == u"c") {
        command_.kind = FileCommand::Kind::CancelConflict;
      }
      // Any other key (including Escape, REQ-C-008) is swallowed: no effect on the task.
      return true;
    case TaskManager::PromptKind::TrashConfirm:
      // REQ-F-017: any key other than "y" declines, including Escape (REQ-C-008).
      command_ = {.kind = FileCommand::Kind::ConfirmTrash, .count = key == u"y" ? 1 : 0};
      return true;
    case TaskManager::PromptKind::None:
      break;
  }
  return true;
}
