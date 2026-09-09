#include "directory_controller.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

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
  // changed() already fires on every cursor move, batch flush, watcher-driven refresh, and
  // toggle; syncPreviewTarget() guards on the resolved path so it's cheap when nothing
  // preview-relevant actually changed.
  connect(this, &DirectoryController::changed, this, &DirectoryController::syncPreviewTarget);
  // Row-count/order changes (batches landing, a toggle, a watcher-driven refresh) all funnel
  // through here so cursorRow never points past the end and QML always hears about it.
  connect(&proxy_, &QAbstractItemModel::rowsInserted, this, [this] {
    clampCursorRow();
    emit changed();
  });
  connect(&proxy_, &QAbstractItemModel::rowsRemoved, this, [this] {
    clampCursorRow();
    emit changed();
  });
  connect(&proxy_, &QAbstractItemModel::modelReset, this, [this] {
    clampCursorRow();
    emit changed();
  });
  connect(&proxy_, &QAbstractItemModel::layoutChanged, this, [this] { emit changed(); });
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
  current_path_ = path;
  status_message_ = fallbackReason;
  cursor_row_ = 0;
  model_.load(path);
  watcher_.addPath(path);
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
void DirectoryController::toggleHidden() { proxy_.setHiddenVisible(!proxy_.hiddenVisible()); }
void DirectoryController::toggleSortDirection() { proxy_.setSortDescending(!proxy_.sortDescending()); }
int DirectoryController::takeCount() {
  const int count = has_pending_count_ ? pending_count_ : 1;
  pending_count_ = 0;
  has_pending_count_ = false;
  return count;
}
void DirectoryController::setCursorRow(qint64 row) {
  const int last = proxy_.rowCount() - 1;
  cursor_row_ = static_cast<int>(qBound(qint64{0}, row, qint64{qMax(0, last)}));
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
bool DirectoryController::handleKey(const QString& key) {
  if (pending_g_ && pending_g_timer_.elapsed() > kPendingGTimeoutMs) {
    pending_g_ = false;
  }
  if (key.size() == 1 && key.at(0) >= u'0' && key.at(0) <= u'9') {
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
  if (key == u"." || key == u"s") {
    takeCount();
    if (key == u".") {
      toggleHidden();
    } else {
      toggleSortDirection();
    }
    return true;
  }
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
  takeCount();
  return false;
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
