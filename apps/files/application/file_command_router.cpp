#include "file_command_router.h"

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
  navigation_.reset();
  pending_y_ = pending_d_ = pending_f_ = pending_c_ = false;
}
int FileCommandRouter::takeCount() { return navigation_.takeCount(); }
bool FileCommandRouter::handleNormalToggleAndNavigationKey(const QString& key) {
  if (key == u"s") {
    takeCount();
    command_.kind = FileCommand::Kind::ToggleSort;
    return true;
  }
  if (key == u"Return" || key == u"Enter" || key == u"\r") {
    takeCount();
    command_.kind = FileCommand::Kind::Open;
    return true;
  }
  if (key == u" ") {
    takeCount();
    if (!context_.quick_look_open && !context_.can_preview) {
      return true;
    }
    command_.kind = FileCommand::Kind::ToggleQuickLook;
    return true;
  }
  if (key == u"Escape") {
    if (!context_.quick_look_open) {
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
  if (key == u"Escape" && !context_.quick_look_open) {
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
void FileCommandRouter::expirePendingCommands() {
  if (pending_y_ && pending_y_timer_.elapsed() > kPendingGTimeoutMs) {
    pending_y_ = false;
  }
  if (pending_d_ && pending_d_timer_.elapsed() > kPendingGTimeoutMs) {
    pending_d_ = false;
  }

  if (pending_f_ && pending_f_timer_.elapsed() > kPendingGTimeoutMs) {
    pending_f_ = false;
  }
  if (pending_c_ && pending_c_timer_.elapsed() > kPendingGTimeoutMs) {
    pending_c_ = false;
  }
}
bool FileCommandRouter::handleKey(const QString& key) {
  // File-operations (SPEC.md docs/sdd/file-operations/SPEC.md): a pending prompt captures every
  // key exclusively, ahead of mode dispatch entirely (REQ-F-021's "paused... does not proceed
  // until resolved" reads as exclusive key capture — new p/D presses during an open prompt are
  // simply consumed, not queued as additional operations).
  if ((context_.prompt != TaskManager::PromptKind::None)) {
    reset();
    return handlePromptKey(key);
  }

  const auto mode = context_.mode;
  if (mode == VimModeController::Mode::Insert || mode == VimModeController::Mode::Search) {
    // Keyboard focus lives on the inline editor / search field now; QML never routes their key
    // events through here (REQ-C-001), but stay a safe no-op regardless.
    reset();
    return false;
  }
  if (context_.quick_look_open) {
    return handleQuickLookKey(key);
  }

  expirePendingCommands();

  const bool isVisual = mode == VimModeController::Mode::Visual;
  if (handleFinderKey(key)) {
    navigation_.reset();
    return true;
  }
  if (key != u"f") {
    pending_f_ = false;
  }
  if (key != u"c") {
    pending_c_ = false;
  }
  if (handleFileOperationKey(key, isVisual)) {
    navigation_.reset();
    return true;
  }

  const auto movement = navigation_.route(key, context_.vim_navigation, context_.icon_view, true);
  using N = Holonight::NavigationCommand;
  if (movement.consumed()) {
    // Visual mode permits movement, while browsing actions remain Normal-only.
    if (isVisual && movement.command >= N::Parent) {
      return true;
    }
    command_.count = movement.count;
    switch (movement.command) {
      case N::Previous:
        command_.count = -movement.count;
        [[fallthrough]];
      case N::Next:
        command_.kind = FileCommand::Kind::Move;
        break;
      case N::Left:
        command_.count = -movement.count;
        [[fallthrough]];
      case N::Right:
        command_.kind = FileCommand::Kind::MoveHorizontal;
        break;
      case N::First:
        command_.kind = FileCommand::Kind::First;
        break;
      case N::Last:
        command_.kind = FileCommand::Kind::Last;
        break;
      case N::HalfPageUp:
        command_.count = -movement.count;
        [[fallthrough]];
      case N::HalfPageDown:
        command_.kind = FileCommand::Kind::HalfPage;
        break;
      case N::PageUp:
        command_.count = -movement.count;
        [[fallthrough]];
      case N::PageDown:
        command_.kind = FileCommand::Kind::Page;
        break;
      case N::Parent:
        command_.kind = FileCommand::Kind::Parent;
        break;
      case N::Open:
        command_.kind = FileCommand::Kind::Open;
        break;
      case N::ToggleHidden:
        command_.kind = FileCommand::Kind::ToggleHidden;
        break;
      default:
        break;  // Focus/location intentions are routed by the window scope.
    }
    return true;
  }
  if (isVisual && key == u"o") {
    takeCount();
    command_.kind = FileCommand::Kind::SwapVisualEnd;
    return true;
  }
  if (isVisual) {
    if (key == u"Escape") {
      takeCount();
      command_.kind = FileCommand::Kind::ExitVisual;
      return true;
    }
    {
      // REQ-C-004: VISUAL has no other operation consumer this stage — every key besides a
      // count-prefixed motion, Escape, or a file-operation key above is swallowed as a no-op.
      takeCount();
      return true;
    }
  }

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
  } else if ((context_.vim_navigation && key == u"j") || key == u"ArrowDown") {
    command_ = {.kind = FileCommand::Kind::MoveQuickLookLine, .count = 1};
  } else if ((context_.vim_navigation && key == u"k") || key == u"ArrowUp") {
    command_ = {.kind = FileCommand::Kind::MoveQuickLookLine, .count = -1};
  }
  return true;
}
// Normal mode only: `ff` opens the file finder and `cd` the directory jumper. The first key of a
// pair is held for kPendingGTimeoutMs; any other key discards it, like gg/yy/dd.
bool FileCommandRouter::handleFinderKey(const QString& key) {
  if (context_.mode != VimModeController::Mode::Normal) {
    return false;
  }
  if (key == u"f") {
    pending_y_ = pending_d_ = pending_c_ = false;
    if (pending_f_) {
      pending_f_ = false;
      takeCount();
      command_.kind = FileCommand::Kind::FindFile;
      return true;
    }
    pending_f_ = true;
    pending_f_timer_.start();
    return true;
  }
  if (key == u"c") {
    pending_y_ = pending_d_ = pending_f_ = false;
    pending_c_ = true;
    pending_c_timer_.start();
    return true;
  }
  if (key == u"d" && pending_c_) {
    pending_c_ = false;
    takeCount();
    command_.kind = FileCommand::Kind::ChangeDirectory;
    return true;
  }
  return false;
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
