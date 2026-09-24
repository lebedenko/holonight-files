#include "navigation_session.h"

#include "directory_controller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QStandardPaths>
#include <QUrl>

void NavigationSession::openInternal(const QString& requestedPath, const QString& fallbackReason,
                                     const QString& restoreName, bool recordHistory) {
  // Cleaned but not symlink-resolved, so history entries dedup by what the breadcrumb shows
  // (REQ-C-003).
  const auto path =
      requestedPath.isEmpty() ? requestedPath : QDir::cleanPath(QFileInfo(requestedPath).absoluteFilePath());
  if (recordHistory) {
    jump_list_.recordVisit(path, outgoingCursorName());
  }
  const auto watched = watcher_.directories() + watcher_.files();
  if (!watched.isEmpty()) {
    watcher_.removePaths(watched);
  }
  ++navigation_serial_;
  controller_.resetForNavigation();
  current_path_ = path;
  controller_.devices_->navigationChanged(path);
  controller_.status_message_ = fallbackReason;
  cursor_row_ = 0;
  // Every navigation replaces whatever restore a superseded one was still waiting on (REQ-F-016).
  awaiting_initial_load_ = false;
  pending_restore_name_ = restoreName;
  model_.load(path);
  // Armed only after load() returns: its synchronous reset emits controller_.changed() against an empty
  // listing, which must not count as the load settling.
  awaiting_initial_load_ = true;
  watcher_.addPath(path);
  emit controller_.navigated();
  emit controller_.changed();
}
QString NavigationSession::outgoingCursorName() const {
  // A listing that never settled has no trustworthy row to come back to (REQ-F-039).
  if (awaiting_initial_load_ || cursor_row_ < 0 || cursor_row_ >= proxy_.rowCount()) {
    return {};
  }
  const auto srcIndex = proxy_.mapToSource(proxy_.index(cursor_row_, 0));
  if (model_.data(srcIndex, DirectoryModel::IsParentRole).toBool()) {
    return {};
  }
  return entryNameAt(cursor_row_);
}
void NavigationSession::traverseHistory(int direction, int count) {
  // Rechecked here, not only in QML bindings, so direct callers get the same gating
  // (REQ-F-021/022). Quick Look pins the file even when history bypasses the command router.
  // The count was already consumed by the caller (REQ-F-023).
  if (controller_.quickLookOpen() || controller_.tasks_.hasPrompt() ||
      controller_.vim_.currentMode() != VimModeController::Mode::Normal) {
    return;
  }
  const auto result = jump_list_.traverse(direction, count, outgoingCursorName(),
                                          [](const QString& path) { return QFileInfo(path).isDir(); });
  const auto skipped =
      result.skipped_paths.isEmpty()
          ? QString{}
          : DirectoryController::tr("Skipped missing: %1").arg(result.skipped_paths.join(QStringLiteral(", ")));
  if (result.moved) {
    openInternal(result.target_path, skipped, result.restore_name, /*recordHistory=*/false);
  } else if (!skipped.isEmpty()) {
    controller_.status_message_ = skipped;
    emit controller_.changed();
  }
}
void NavigationSession::cancelPendingRestore() {
  awaiting_initial_load_ = false;
  pending_restore_name_.clear();
}
void NavigationSession::maybeApplyPendingRestore() {
  // Watcher refreshes also settle through controller_.changed(), but by then the flag is already cleared
  // (REQ-F-017).
  if (!awaiting_initial_load_ || model_.scanning()) {
    return;
  }
  const auto name = pending_restore_name_;
  cancelPendingRestore();
  int row = 0;
  if (!name.isEmpty()) {
    for (int candidate = 0; candidate < proxy_.rowCount(); ++candidate) {
      if (entryNameAt(candidate) == name) {  // case-sensitive (REQ-C-004)
        row = candidate;
        break;
      }
    }
  }
  setCursorRow(row);
}
void NavigationSession::navigateInto(int proxyRow) {
  if (proxyRow < 0 || proxyRow >= proxy_.rowCount()) {
    return;
  }
  const auto sourceIndex = proxy_.mapToSource(proxy_.index(proxyRow, 0));
  if (model_.data(sourceIndex, DirectoryModel::IsParentRole).toBool()) {
    navigateParent();
    return;
  }
  const auto path = model_.data(sourceIndex, DirectoryModel::PathRole).toString();
  if (!path.isEmpty()) {
    controller_.open(path);
  }
}
void NavigationSession::navigateParent() {
  if (current_path_.isEmpty() || QDir(current_path_).isRoot()) {
    return;
  }
  const QFileInfo exited(current_path_);
  // Land on the directory just left (REQ-F-018); row 0 when it is no longer listed (REQ-F-019).
  openInternal(exited.absolutePath(), {}, exited.fileName(), /*recordHistory=*/true);
}
void NavigationSession::openEntry(int proxyRow) {
  if (proxyRow < 0 || proxyRow >= proxy_.rowCount()) {
    return;
  }
  const auto sourceIndex = proxy_.mapToSource(proxy_.index(proxyRow, 0));
  if (model_.data(sourceIndex, DirectoryModel::IsParentRole).toBool()) {
    navigateParent();
    return;
  }
  const auto path = model_.data(sourceIndex, DirectoryModel::PathRole).toString();
  if (path.isEmpty()) {
    return;
  }
  if (model_.data(sourceIndex, DirectoryModel::IsDirRole).toBool()) {
    navigateInto(proxyRow);
  } else {
    QDesktopServices::openUrl(QUrl::fromLocalFile(path));
  }
}
void NavigationSession::setCursorRow(qint64 row) {
  // Any cursor placement other than the restore's own supersedes a pending restore (REQ-F-015);
  // maybeApplyPendingRestore() clears the pending state before calling here.
  cancelPendingRestore();
  const int last = proxy_.rowCount() - 1;
  const bool visual = controller_.vim_.currentMode() == VimModeController::Mode::Visual;
  const bool hasParent = last >= 0 && proxy_.data(proxy_.index(0, 0), DirectoryModel::IsParentRole).toBool();
  if (visual && hasParent && last == 0) {
    controller_.vim_.exitVisual();
  }
  const int first = visual && hasParent && last > 0 ? 1 : 0;
  cursor_row_ = static_cast<int>(qBound(qint64{first}, row, qint64{qMax(first, last)}));
  if (controller_.vim_.currentMode() == VimModeController::Mode::Visual) {
    controller_.vim_.extendVisual(cursor_row_);
  }
  controller_.vim_.updateSearchPositions(entryNameAt(cursor_row_));
  emit controller_.changed();
}
void NavigationSession::clampCursorRow() {
  const int last = proxy_.rowCount() - 1;
  const bool visual = controller_.vim_.currentMode() == VimModeController::Mode::Visual;
  const bool hasParent = last >= 0 && proxy_.data(proxy_.index(0, 0), DirectoryModel::IsParentRole).toBool();
  if (visual && hasParent && last == 0) {
    controller_.vim_.exitVisual();
  }
  const int first = visual && hasParent && last > 0 ? 1 : 0;
  cursor_row_ = qBound(first, cursor_row_, qMax(first, last));
}
QString NavigationSession::entryNameAt(int proxyRow) const {
  if (proxyRow < 0 || proxyRow >= proxy_.rowCount()) {
    return {};
  }
  return model_.data(proxy_.mapToSource(proxy_.index(proxyRow, 0)), DirectoryModel::NameRole).toString();
}
void NavigationSession::activateBookmark(int placesRow) {
  if (controller_.vim_.currentMode() != VimModeController::Mode::Normal || controller_.tasks_.hasPrompt() ||
      controller_.preview_selection_.quick_look_open_) {
    return;
  }
  if (const auto placeId = controller_.places_.recheckBookmark(placesRow); placeId != 0) {
    bookmark_dispatch_navigation_serial_[placeId] = navigation_serial_;
  }
}
void NavigationSession::handleBookmarkRecheckResolved(quint64 placeId, const QString& path, bool available) {
  const auto dispatchSerial = bookmark_dispatch_navigation_serial_.take(placeId);
  if (dispatchSerial != navigation_serial_ || controller_.vim_.currentMode() != VimModeController::Mode::Normal ||
      controller_.tasks_.hasPrompt() || controller_.preview_selection_.quick_look_open_) {
    return;  // Navigation or an interaction guard intervened while the check was pending.
  }
  if (available) {
    controller_.open(path);
  } else {
    controller_.status_message_ = DirectoryController::tr("Location is currently unavailable");
    emit controller_.changed();
  }
}
void NavigationSession::openRestoreCandidate(const QString& path) {
  restore_candidate_ = path;
  restore_serial_ = navigation_serial_;
  model_.validateForRestore(path);
}
void NavigationSession::handleRestoreValidated(const QString& path, RestoreOutcome outcome) {
  if (path != restore_candidate_ || restore_serial_ != navigation_serial_) {
    return;
  }
  restore_candidate_.clear();
  if (outcome == RestoreOutcome::Ok) {
    controller_.open(path);
  } else {
    controller_.open(QStandardPaths::writableLocation(QStandardPaths::HomeLocation), restoreOutcomeReason(outcome));
  }
}
