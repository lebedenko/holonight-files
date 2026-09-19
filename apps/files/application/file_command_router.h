#pragma once
#include "task_manager.h"
#include "vim_mode_controller.h"

#include <QElapsedTimer>

// Parsing is independent of listing/operation side effects. The application executes commands.
struct FileCommand {
  enum class Kind {
    None,
    Move,
    First,
    Last,
    ToggleHidden,
    ToggleSort,
    Parent,
    Open,
    ToggleQuickLook,
    CloseQuickLook,
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
    ConfirmTrash
  };
  Kind kind = Kind::None;
  int count = 1;
  bool visual = false;
  bool consumed = true;
};
struct FileCommandContext {
  VimModeController::Mode mode;
  TaskManager::PromptKind prompt;
  bool quickLookOpen;
  bool canPreview;
};
class FileCommandRouter {
 public:
  FileCommand route(const QString& key, FileCommandContext context);
  int takeCount();
  void reset();

 private:
  bool handleCountAndMotionKeys(const QString& key, bool isDigit);
  bool handleNormalToggleAndNavigationKey(const QString& key);
  bool handleModeTransitionKey(const QString& key);
  bool handleNormalOnlyKey(const QString& key);
  bool handleKey(const QString& key);
  bool handleFileOperationKey(const QString& key, bool isVisual);
  bool handlePromptKey(const QString& key);
  FileCommand command_;
  FileCommandContext context_{};
  int pending_count_ = 0;
  bool has_pending_count_ = false;
  bool pending_g_ = false;
  bool pending_y_ = false;
  bool pending_d_ = false;
  QElapsedTimer pending_g_timer_;
  QElapsedTimer pending_y_timer_;
  QElapsedTimer pending_d_timer_;
};
