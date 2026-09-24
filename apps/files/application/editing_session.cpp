#include "editing_session.h"

#include "directory_controller.h"

#include <QFile>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <sys/stat.h>

void EditingSession::beginRename(VimModeController::InsertKind kind) {
  // Entering a mode commits to the current cursor; suspendUpdates() below would otherwise settle
  // the partial listing and apply the restore under the editor.
  controller_.cancelPendingRestore();
  if (controller_.navigation_.cursor_row_ < 0 ||
      controller_.navigation_.cursor_row_ >= controller_.navigation_.proxy_.rowCount()) {
    return;  // nothing selected to rename; the key is still consumed by the caller
  }
  const auto sourceIndex = controller_.navigation_.proxy_.mapToSource(
      controller_.navigation_.proxy_.index(controller_.navigation_.cursor_row_, 0));
  if (controller_.navigation_.model_.data(sourceIndex, DirectoryModel::IsParentRole).toBool()) {
    return;  // cannot rename parent ".." entry
  }
  controller_.navigation_.proxy_.setEditing(true);
  controller_.navigation_.model_.suspendUpdates();
  controller_.vim_.enterInsert(kind, controller_.navigation_.cursor_row_, controller_.navigation_.current_path_,
                               controller_.entryNameAt(controller_.navigation_.cursor_row_));
}
void EditingSession::beginCreate(VimModeController::InsertKind kind) {
  controller_.cancelPendingRestore();
  controller_.navigation_.proxy_.setEditing(true);
  controller_.navigation_.model_.suspendUpdates();
  const bool below = kind == VimModeController::InsertKind::CreateBelow;
  QString anchorName;
  bool anchorIsDir = false;
  if (controller_.navigation_.proxy_.rowCount() > 0) {
    const int anchorProxyRow =
        qBound(0, controller_.navigation_.cursor_row_, controller_.navigation_.proxy_.rowCount() - 1);
    const auto anchorSourceIndex =
        controller_.navigation_.proxy_.mapToSource(controller_.navigation_.proxy_.index(anchorProxyRow, 0));
    anchorName = controller_.navigation_.model_.data(anchorSourceIndex, DirectoryModel::NameRole).toString();
    anchorIsDir = controller_.navigation_.model_.data(anchorSourceIndex, DirectoryModel::IsDirRole).toBool();
  }
  const int placeholderSourceRow = controller_.navigation_.model_.rowCount();
  controller_.navigation_.proxy_.setPlaceholder(placeholderSourceRow, anchorName, anchorIsDir, below);
  controller_.navigation_.model_.insertPlaceholderRow();
  active_placeholder_source_row_ = placeholderSourceRow;
  const int visualRow =
      controller_.navigation_.proxy_.mapFromSource(controller_.navigation_.model_.index(placeholderSourceRow)).row();
  controller_.setCursorRow(visualRow);
  controller_.vim_.enterInsert(kind, visualRow, controller_.navigation_.current_path_, {});
}
void EditingSession::removeActivePlaceholderIfAny() {
  if (active_placeholder_source_row_ < 0) {
    return;
  }
  controller_.navigation_.model_.removePlaceholderRow(active_placeholder_source_row_);
  controller_.navigation_.proxy_.setPlaceholder(-1, {}, false, false);
  active_placeholder_source_row_ = -1;
}
void EditingSession::updateInsertText(const QString& text) { controller_.vim_.setInsertText(text); }
void EditingSession::commitInsertEditing() {
  const auto commit = controller_.vim_.commitInsert();
  if (commit.action == VimModeController::InsertCommitAction::None) {
    return;  // REQ-F-037/038: validation failed; stays in INSERT for correction
  }

  if (before_commit_for_test_) {
    before_commit_for_test_(commit);
  }
  bool succeeded = false;
  int operationError = 0;
  QString detail;
  switch (commit.action) {
    case VimModeController::InsertCommitAction::Touch: {
      const std::array<timespec, 2> times{{{.tv_sec = 0, .tv_nsec = UTIME_OMIT}, {.tv_sec = 0, .tv_nsec = UTIME_NOW}}};
      succeeded =
          ::utimensat(AT_FDCWD, QFile::encodeName(commit.oldPath).constData(), times.data(), AT_SYMLINK_NOFOLLOW) == 0;
      operationError = succeeded ? 0 : errno;
      break;
    }
    case VimModeController::InsertCommitAction::Rename: {
      succeeded = ::renameat2(AT_FDCWD, QFile::encodeName(commit.oldPath).constData(), AT_FDCWD,
                              QFile::encodeName(commit.newPath).constData(), RENAME_NOREPLACE) == 0;
      operationError = succeeded ? 0 : errno;
      break;
    }
    case VimModeController::InsertCommitAction::CreateDirectory:
      succeeded = ::mkdir(QFile::encodeName(commit.newPath).constData(), 0777) == 0;
      operationError = succeeded ? 0 : errno;
      break;
    case VimModeController::InsertCommitAction::CreateFile: {
      QFile file(commit.newPath);
      succeeded = file.open(QIODevice::WriteOnly | QIODevice::NewOnly);
      operationError = succeeded ? 0 : errno;
      detail = file.errorString();
      break;
    }
    case VimModeController::InsertCommitAction::None:
      break;
  }

  if (!succeeded) {
    QString message;
    if (operationError == EEXIST) {
      message = DirectoryController::tr("An entry with that name already exists");
    } else if (operationError == EACCES || operationError == EPERM) {
      message = DirectoryController::tr("Permission denied");
    } else if (operationError == ENOENT) {
      message = DirectoryController::tr("Entry or parent folder no longer exists");
    } else {
      message = DirectoryController::tr("Operation failed: %1")
                    .arg(detail.isEmpty() ? QString::fromLocal8Bit(std::strerror(operationError)) : detail);
    }
    controller_.vim_.reportCommitFailed(message);
    return;
  }

  const bool wasCreate = controller_.vim_.editingIsCreate();
  controller_.vim_.reportCommitSucceeded();
  if (wasCreate) {
    // The now-real entry reappears via the immediate refresh below; remove the stand-in first so
    // it isn't briefly duplicated.
    removeActivePlaceholderIfAny();
  }
  controller_.navigation_.proxy_.setEditing(false);
  controller_.navigation_.model_.resumeUpdates();
}
void EditingSession::cancelInsertEditing() {
  controller_.vim_.cancelInsert();
  removeActivePlaceholderIfAny();
  controller_.navigation_.proxy_.setEditing(false);
  controller_.navigation_.model_.resumeUpdates();
}
void EditingSession::listingChanged() {
  ++listing_revision_;
  controller_.clampCursorRow();
  if (controller_.vim_.currentMode() == VimModeController::Mode::Search) {
    ensureSearchCurrent();
    controller_.vim_.updateSearchPositions(controller_.entryNameAt(controller_.navigation_.cursor_row_));
  }
  emit controller_.changed();
}
void EditingSession::ensureSearchCurrent() {
  if (search_revision_ == listing_revision_) {
    return;
  }
  QStringList names;
  for (int row = 0; row < controller_.navigation_.proxy_.rowCount(); ++row) {
    const auto sourceIndex = controller_.navigation_.proxy_.mapToSource(controller_.navigation_.proxy_.index(row, 0));
    const bool isParent = controller_.navigation_.model_.data(sourceIndex, DirectoryModel::IsParentRole).toBool();
    names.append(isParent ? QString{} : controller_.entryNameAt(row));
  }
  search_revision_ = listing_revision_;
  controller_.vim_.refreshSearch(names);
}
void EditingSession::updateSearchQuery(const QString& query) {
  if (controller_.vim_.currentMode() != VimModeController::Mode::Search) {
    return;
  }
  QStringList names;
  for (int row = 0; row < controller_.navigation_.proxy_.rowCount(); ++row) {
    const auto sourceIndex = controller_.navigation_.proxy_.mapToSource(controller_.navigation_.proxy_.index(row, 0));
    const bool isParent = controller_.navigation_.model_.data(sourceIndex, DirectoryModel::IsParentRole).toBool();
    names.append(isParent ? QString{} : controller_.entryNameAt(row));
  }
  controller_.vim_.setSearchQuery(query, names);
  search_revision_ = listing_revision_;
  const int best = controller_.vim_.searchBestRow();
  if (best >= 0) {
    controller_.setCursorRow(best);
  }
  controller_.vim_.updateSearchPositions(controller_.entryNameAt(controller_.navigation_.cursor_row_));
}
void EditingSession::commitSearchEditing() { controller_.vim_.commitSearch(); }
void EditingSession::cancelSearchEditing() {
  int restoreRow = controller_.vim_.cancelSearch();
  for (int row = 0; row < controller_.navigation_.proxy_.rowCount(); ++row) {
    if (controller_.entryNameAt(row) == pre_search_name_) {
      restoreRow = row;
      break;
    }
  }
  ++listing_revision_;  // The cached rows belonged to the cancelled query.
  controller_.setCursorRow(restoreRow);
}
