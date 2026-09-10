#include "directory_controller.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <array>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <sys/stat.h>

namespace {
constexpr qint64 kPendingGTimeoutMs = 600;
}

DirectoryController::DirectoryController(QObject* parent) : QObject(parent) {
  proxy_.setSourceModel(&model_);
  connect(&model_, &DirectoryModel::changed, this, &DirectoryController::changed);
  connect(&model_, &DirectoryModel::shutdownFinished, this, &DirectoryController::handleWorkerShutdown);
  connect(&preview_, &PreviewService::shutdownFinished, this, &DirectoryController::handleWorkerShutdown);
  connect(&vim_, &VimModeController::changed, this, &DirectoryController::changed);
  // changed() already fires on every cursor move, batch flush, watcher-driven refresh, and
  // toggle; syncPreviewTarget() guards on the resolved path so it's cheap when nothing
  // preview-relevant actually changed.
  connect(this, &DirectoryController::changed, this, &DirectoryController::syncPreviewTarget);
  connect(&proxy_, &QAbstractItemModel::rowsInserted, this, &DirectoryController::listingChanged);
  connect(&proxy_, &QAbstractItemModel::rowsRemoved, this, &DirectoryController::listingChanged);
  connect(&proxy_, &QAbstractItemModel::modelReset, this, &DirectoryController::listingChanged);
  connect(&proxy_, &QAbstractItemModel::layoutChanged, this, &DirectoryController::listingChanged);
  connect(&proxy_, &QAbstractItemModel::dataChanged, this, &DirectoryController::listingChanged);
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, this, [this] {
    ++preview_revision_;
    model_.refresh();
  });
  connect(&watcher_, &QFileSystemWatcher::fileChanged, this, [this] {
    ++preview_revision_;
    model_.refresh();
  });
}
void DirectoryController::open(const QString& path, const QString& fallbackReason) {
  const auto watched = watcher_.directories() + watcher_.files();
  if (!watched.isEmpty()) {
    watcher_.removePaths(watched);
  }
  resetForNavigation();
  current_path_ = path;
  status_message_ = fallbackReason;
  cursor_row_ = 0;
  model_.load(path);
  watcher_.addPath(path);
  emit navigated();
  emit changed();
}
void DirectoryController::navigateInto(int proxyRow) {
  const auto sourceIndex = proxy_.mapToSource(proxy_.index(proxyRow, 0));
  const auto path = model_.data(sourceIndex, DirectoryModel::PathRole).toString();
  if (!path.isEmpty()) {
    open(path);
  }
}
void DirectoryController::navigateParent() {
  if (current_path_.isEmpty()) {
    return;
  }
  open(QFileInfo(current_path_).absolutePath());
}
void DirectoryController::openEntry(int proxyRow) {
  if (proxyRow < 0 || proxyRow >= proxy_.rowCount()) {
    return;
  }
  const auto sourceIndex = proxy_.mapToSource(proxy_.index(proxyRow, 0));
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
void DirectoryController::toggleHidden() {
  if (vim_.currentMode() != VimModeController::Mode::Insert) {
    proxy_.setHiddenVisible(!proxy_.hiddenVisible());
  }
}
void DirectoryController::toggleSortDirection() {
  if (vim_.currentMode() != VimModeController::Mode::Insert) {
    proxy_.setSortDescending(!proxy_.sortDescending());
  }
}
int DirectoryController::takeCount() {
  const int count = has_pending_count_ ? pending_count_ : 1;
  pending_count_ = 0;
  has_pending_count_ = false;
  return count;
}
void DirectoryController::setCursorRow(qint64 row) {
  const int last = proxy_.rowCount() - 1;
  cursor_row_ = static_cast<int>(qBound(qint64{0}, row, qint64{qMax(0, last)}));
  if (vim_.currentMode() == VimModeController::Mode::Visual) {
    vim_.extendVisual(cursor_row_);
  }
  vim_.updateSearchPositions(entryNameAt(cursor_row_));
  emit changed();
}
void DirectoryController::clampCursorRow() {
  const int last = proxy_.rowCount() - 1;
  cursor_row_ = qBound(0, cursor_row_, qMax(0, last));
}
bool DirectoryController::canPreviewSelection() const {
  return cursor_row_ >= 0 && cursor_row_ < proxy_.rowCount() && preview_.hasEntry() &&
         !model_.data(proxy_.mapToSource(proxy_.index(cursor_row_, 0)), DirectoryModel::StatFailedRole).toBool();
}
QString DirectoryController::entryNameAt(int proxyRow) const {
  if (proxyRow < 0 || proxyRow >= proxy_.rowCount()) {
    return {};
  }
  return model_.data(proxy_.mapToSource(proxy_.index(proxyRow, 0)), DirectoryModel::NameRole).toString();
}
void DirectoryController::beginRename(VimModeController::InsertKind kind) {
  if (cursor_row_ < 0 || cursor_row_ >= proxy_.rowCount()) {
    return;  // nothing selected to rename; the key is still consumed by the caller
  }
  proxy_.setEditing(true);
  model_.suspendUpdates();
  vim_.enterInsert(kind, cursor_row_, current_path_, entryNameAt(cursor_row_));
}
void DirectoryController::beginCreate(VimModeController::InsertKind kind) {
  proxy_.setEditing(true);
  model_.suspendUpdates();
  const bool below = kind == VimModeController::InsertKind::CreateBelow;
  QString anchorName;
  bool anchorIsDir = false;
  if (proxy_.rowCount() > 0) {
    const int anchorProxyRow = qBound(0, cursor_row_, proxy_.rowCount() - 1);
    const auto anchorSourceIndex = proxy_.mapToSource(proxy_.index(anchorProxyRow, 0));
    anchorName = model_.data(anchorSourceIndex, DirectoryModel::NameRole).toString();
    anchorIsDir = model_.data(anchorSourceIndex, DirectoryModel::IsDirRole).toBool();
  }
  const int placeholderSourceRow = model_.rowCount();
  proxy_.setPlaceholder(placeholderSourceRow, anchorName, anchorIsDir, below);
  model_.insertPlaceholderRow();
  active_placeholder_source_row_ = placeholderSourceRow;
  const int visualRow = proxy_.mapFromSource(model_.index(placeholderSourceRow)).row();
  setCursorRow(visualRow);
  vim_.enterInsert(kind, visualRow, current_path_, {});
}
void DirectoryController::removeActivePlaceholderIfAny() {
  if (active_placeholder_source_row_ < 0) {
    return;
  }
  model_.removePlaceholderRow(active_placeholder_source_row_);
  proxy_.setPlaceholder(-1, {}, false, false);
  active_placeholder_source_row_ = -1;
}
bool DirectoryController::handleCountAndMotionKeys(const QString& key, bool isDigit) {
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
      setCursorRow(0);
      return true;
    }
    pending_g_ = true;
    pending_g_timer_.start();
    return true;
  }
  pending_g_ = false;
  if (key == u"G") {
    takeCount();
    setCursorRow(proxy_.rowCount() - 1);
    return true;
  }
  if (key == u"j") {
    setCursorRow(qint64{cursor_row_} + takeCount());
    return true;
  }
  if (key == u"k") {
    setCursorRow(qint64{cursor_row_} - takeCount());
    return true;
  }
  return false;
}
bool DirectoryController::handleNormalToggleAndNavigationKey(const QString& key) {
  if (key == u"." || key == u"s") {
    takeCount();
    if (key == u".") {
      toggleHidden();
    } else {
      toggleSortDirection();
    }
    return true;
  }
  if (key == u"h") {
    takeCount();
    navigateParent();
    return true;
  }
  if (key == u"l" || key == u"Return" || key == u"Enter" || key == u"\r") {
    takeCount();
    openEntry(cursor_row_);
    return true;
  }
  if (key == u" ") {
    takeCount();
    if (!quick_look_open_ && !canPreviewSelection()) {
      return true;  // REQ-F-013: consumed, but explicitly a no-op
    }
    quick_look_open_ = !quick_look_open_;
    preview_.setQuickLookActive(quick_look_open_);
    emit changed();
    return true;
  }
  if (key == u"Escape") {
    if (!quick_look_open_) {
      return false;  // Let the window-level Shortcut handle fullscreen.
    }
    takeCount();
    quick_look_open_ = false;
    preview_.setQuickLookActive(false);
    emit changed();
    return true;
  }
  return false;
}
bool DirectoryController::handleModeTransitionKey(const QString& key) {
  if (key == u":") {
    takeCount();
    return true;  // REQ-C-006: no command palette this cycle
  }
  if (key == u"v" || key == u"V") {
    takeCount();
    vim_.enterVisual(cursor_row_);
    return true;
  }
  if (key == u"i" || key == u"I") {
    takeCount();
    beginRename(VimModeController::InsertKind::Prepend);
    return true;
  }
  if (key == u"a" || key == u"A") {
    takeCount();
    beginRename(VimModeController::InsertKind::Append);
    return true;
  }
  if (key == u"o") {
    takeCount();
    beginCreate(VimModeController::InsertKind::CreateBelow);
    return true;
  }
  if (key == u"O") {
    takeCount();
    beginCreate(VimModeController::InsertKind::CreateAbove);
    return true;
  }
  if (key == u"/") {
    takeCount();
    pre_search_name_ = entryNameAt(cursor_row_);
    vim_.enterSearch(cursor_row_);
    return true;
  }
  if (key == u"n" || key == u"N") {
    takeCount();
    ensureSearchCurrent();
    const int row = vim_.advanceSearchMatch(cursor_row_, key == u"n");
    if (row >= 0) {
      setCursorRow(row);
    }
    return true;
  }
  return false;
}
bool DirectoryController::handleNormalOnlyKey(const QString& key) {
  if (key == u"Escape" && !quick_look_open_) {
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
bool DirectoryController::handleKey(const QString& key) {
  const auto mode = vim_.currentMode();
  if (mode == VimModeController::Mode::Insert || mode == VimModeController::Mode::Search) {
    // Keyboard focus lives on the inline editor / search field now; QML never routes their key
    // events through here (REQ-C-001), but stay a safe no-op regardless.
    return false;
  }

  if (pending_g_ && pending_g_timer_.elapsed() > kPendingGTimeoutMs) {
    pending_g_ = false;
  }

  const bool isDigit = key.size() == 1 && key.at(0) >= u'0' && key.at(0) <= u'9';
  const bool isMotionKey = key == u"g" || key == u"G" || key == u"j" || key == u"k";
  if (mode == VimModeController::Mode::Visual) {
    if (key == u"Escape") {
      takeCount();
      vim_.exitVisual();
      return true;
    }
    if (!isDigit && !isMotionKey) {
      // REQ-F-023/REQ-C-004: VISUAL has no operation consumer this stage — every key besides a
      // count-prefixed motion or Escape is swallowed as a deliberate no-op.
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
void DirectoryController::updateInsertText(const QString& text) { vim_.setInsertText(text); }
void DirectoryController::commitInsertEditing() {
  const auto commit = vim_.commitInsert();
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
      message = tr("An entry with that name already exists");
    } else if (operationError == EACCES || operationError == EPERM) {
      message = tr("Permission denied");
    } else if (operationError == ENOENT) {
      message = tr("Entry or parent folder no longer exists");
    } else {
      message = tr("Operation failed: %1")
                    .arg(detail.isEmpty() ? QString::fromLocal8Bit(std::strerror(operationError)) : detail);
    }
    vim_.reportCommitFailed(message);
    return;
  }

  const bool wasCreate = vim_.editingIsCreate();
  vim_.reportCommitSucceeded();
  if (wasCreate) {
    // The now-real entry reappears via the immediate refresh below; remove the stand-in first so
    // it isn't briefly duplicated.
    removeActivePlaceholderIfAny();
  }
  proxy_.setEditing(false);
  model_.resumeUpdates();
}
void DirectoryController::cancelInsertEditing() {
  vim_.cancelInsert();
  removeActivePlaceholderIfAny();
  proxy_.setEditing(false);
  model_.resumeUpdates();
}
void DirectoryController::listingChanged() {
  ++listing_revision_;
  clampCursorRow();
  if (vim_.currentMode() == VimModeController::Mode::Search) {
    ensureSearchCurrent();
    vim_.updateSearchPositions(entryNameAt(cursor_row_));
  }
  emit changed();
}
void DirectoryController::ensureSearchCurrent() {
  if (search_revision_ == listing_revision_) {
    return;
  }
  QStringList names;
  for (int row = 0; row < proxy_.rowCount(); ++row) {
    names.append(entryNameAt(row));
  }
  search_revision_ = listing_revision_;
  vim_.refreshSearch(names);
}
void DirectoryController::updateSearchQuery(const QString& query) {
  if (vim_.currentMode() != VimModeController::Mode::Search) {
    return;
  }
  QStringList names;
  for (int row = 0; row < proxy_.rowCount(); ++row) {
    names.append(entryNameAt(row));
  }
  vim_.setSearchQuery(query, names);
  search_revision_ = listing_revision_;
  const int best = vim_.searchBestRow();
  if (best >= 0) {
    setCursorRow(best);
  }
  vim_.updateSearchPositions(entryNameAt(cursor_row_));
}
void DirectoryController::commitSearchEditing() { vim_.commitSearch(); }
void DirectoryController::cancelSearchEditing() {
  int restoreRow = vim_.cancelSearch();
  for (int row = 0; row < proxy_.rowCount(); ++row) {
    if (entryNameAt(row) == pre_search_name_) {
      restoreRow = row;
      break;
    }
  }
  ++listing_revision_;  // The cached rows belonged to the cancelled query.
  setCursorRow(restoreRow);
}
void DirectoryController::resetForNavigation() {
  vim_.cancelInsert();
  removeActivePlaceholderIfAny();
  proxy_.setEditing(false);
  vim_.exitVisual();
  vim_.resetSearch();
  pre_search_name_.clear();
  pending_count_ = 0;
  has_pending_count_ = false;
  pending_g_ = false;
  quick_look_open_ = false;
  preview_.setQuickLookActive(false);
  ++listing_revision_;
}
void DirectoryController::syncPreviewTarget() {
  if (cursor_row_ < 0 || cursor_row_ >= proxy_.rowCount()) {
    if (quick_look_open_) {
      quick_look_open_ = false;
      preview_.setQuickLookActive(false);
      emit changed();
    }
    if (!preview_target_path_.isEmpty()) {
      preview_target_path_.clear();
      preview_.clear();
    }
    return;
  }
  const auto sourceIndex = proxy_.mapToSource(proxy_.index(cursor_row_, 0));
  const auto path = model_.data(sourceIndex, DirectoryModel::PathRole).toString();
  if (path != preview_target_path_) {
    const auto files = watcher_.files();
    if (!files.isEmpty()) {
      watcher_.removePaths(files);
    }
  }
  const auto mode = model_.data(sourceIndex, DirectoryModel::ModeRole).toUInt();
  if (S_ISREG(mode) && !watcher_.files().contains(path)) {
    watcher_.addPath(path);
    emit navigated();
  }
  preview_target_path_ = path;
  preview_.setTarget(path, model_.data(sourceIndex, DirectoryModel::IsDirRole).toBool(),
                     model_.data(sourceIndex, DirectoryModel::SizeRole).toLongLong(),
                     model_.data(sourceIndex, DirectoryModel::ModifiedRole).toDateTime(),
                     model_.data(sourceIndex, DirectoryModel::ModeRole).toUInt(),
                     model_.data(sourceIndex, DirectoryModel::StatFailedRole).toBool(),
                     model_.data(sourceIndex, DirectoryModel::StatErrorRole).toString(), preview_revision_);
}
void DirectoryController::handleWorkerShutdown() {
  if (++workers_finished_ == 2) {
    emit shutdownFinished();
  }
}
void DirectoryController::shutdown() {
  model_.shutdown();
  preview_.shutdown();
}
