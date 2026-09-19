#pragma once

#include "vim_mode_controller.h"

#include <QString>

#include <functional>

class DirectoryController;

class EditingSession {
 public:
  explicit EditingSession(DirectoryController& controller) : controller_(controller) {}
  void beginRename(VimModeController::InsertKind kind);
  void beginCreate(VimModeController::InsertKind kind);
  void removeActivePlaceholderIfAny();
  void updateInsertText(const QString& text);
  void commitInsertEditing();
  void cancelInsertEditing();
  void listingChanged();
  void ensureSearchCurrent();
  void updateSearchQuery(const QString& query);
  void commitSearchEditing();
  void cancelSearchEditing();

 private:
  friend class DirectoryController;
  friend struct DirectoryControllerTestAccess;
  DirectoryController& controller_;
  quint64 listing_revision_ = 0;
  quint64 search_revision_ = 0;
  QString pre_search_name_;
  int active_placeholder_source_row_ = -1;
  std::function<void(const VimModeController::InsertCommitResult&)> before_commit_for_test_;
};
