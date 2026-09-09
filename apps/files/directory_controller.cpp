#include "directory_controller.h"

#include <QDesktopServices>
#include <QFileInfo>
#include <QUrl>

#include <limits>

namespace {
constexpr qint64 kPendingGTimeoutMs = 600;
}

DirectoryController::DirectoryController(QObject* parent) : QObject(parent) {
  proxy_.setSourceModel(&model_);
  connect(&model_, &DirectoryModel::changed, this, &DirectoryController::changed);
  connect(&model_, &DirectoryModel::shutdownFinished, this, &DirectoryController::shutdownFinished);
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
  connect(&watcher_, &QFileSystemWatcher::directoryChanged, &model_, &DirectoryModel::refresh);
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
  takeCount();
  return false;
}
void DirectoryController::shutdown() { model_.shutdown(); }
