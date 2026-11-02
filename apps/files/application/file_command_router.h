#pragma once

#include "task_manager.h"
#include "vim_mode_controller.h"

#include <QElapsedTimer>

#include <cstdint>
#include <holonight/navigation.h>

// Parsing is independent of listing/operation side effects. The application executes commands.
struct FileCommand {
  enum class Kind : std::uint8_t {
    None,
    Move,
    MoveHorizontal,
    First,
    Last,
    ToggleHidden,
    ToggleSort,
    Parent,
    Open,
    ToggleQuickLook,
    CloseQuickLook,
    MoveQuickLookLine,
    Visual,
    ExitVisual,
    RenamePrepend,
    RenameAppend,
    CreateBelow,
    CreateAbove,
    Search,
    NextMatch,
    PreviousMatch,
    Yank,
    Cut,
    Trash,
    Paste,
    Skip,
    Overwrite,
    AutoRename,
    CancelConflict,
    ConfirmTrash,
    HalfPage,
    Page,
    SwapVisualEnd,
    FindFile,
    ChangeDirectory,
  };
  Kind kind = Kind::None;
  int count = 1;
  bool visual = false;
  bool consumed = true;
};
struct FileCommandContext {
  VimModeController::Mode mode = VimModeController::Mode::Normal;
  TaskManager::PromptKind prompt = TaskManager::PromptKind::None;
  bool quick_look_open = false;
  bool can_preview = false;
  bool icon_view = false;
  bool vim_navigation = false;
};
class FileCommandRouter {
 public:
  FileCommand route(const QString& key, FileCommandContext context);
  int takeCount();
  void reset();

 private:
  bool handleNormalToggleAndNavigationKey(const QString& key);
  bool handleModeTransitionKey(const QString& key);
  bool handleNormalOnlyKey(const QString& key);
  void expirePendingCommands();
  bool handleKey(const QString& key);
  bool handleFinderKey(const QString& key);
  bool handleFileOperationKey(const QString& key, bool isVisual);
  bool handlePromptKey(const QString& key);
  bool handleQuickLookKey(const QString& key);
  Holonight::NavigationInterpreter navigation_;
  FileCommand command_;
  FileCommandContext context_{};
  bool pending_y_ = false;
  bool pending_d_ = false;
  bool pending_f_ = false;
  bool pending_c_ = false;
  QElapsedTimer pending_y_timer_;
  QElapsedTimer pending_d_timer_;
  QElapsedTimer pending_f_timer_;
  QElapsedTimer pending_c_timer_;
};
