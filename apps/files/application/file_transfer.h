#pragma once
#include "task_manager.h"

#include <QDir>
#include <QMimeData>
#include <QUrl>

// Application transfer policy; filesystem validation remains with the task worker.
namespace FileTransfer {
inline QStringList localPaths(const QList<QUrl>& urls) {
  QStringList paths;
  for (const auto& url : urls) {
    if (!url.isValid() || !url.isLocalFile() || !url.host().isEmpty() || url.hasQuery() || url.hasFragment() ||
        !QDir::isAbsolutePath(url.toLocalFile()) || url.toLocalFile().contains(QChar::Null)) {
      return {};
    }
    const auto path = QDir::cleanPath(url.toLocalFile());
    if (!paths.contains(path)) {
      paths.append(path);
    }
  }
  return paths;
}
inline QList<QUrl> parseUriList(const QByteArray& data) {
  QList<QUrl> urls;
  const auto lines = data.split('\n');
  for (auto line : lines) {
    if (line.endsWith('\r')) {
      line.chop(1);
    }
    if (line.isEmpty() || line.startsWith('#')) {
      continue;
    }
    const auto url = QUrl::fromEncoded(line, QUrl::StrictMode);
    if (!url.isValid()) {
      return {};
    }
    urls.append(url);
  }
  return urls;
}
inline QList<QUrl> mimeUrls(const QMimeData& mime) {
  if (!mime.hasFormat(QStringLiteral("text/uri-list"))) {
    return {};
  }
  return parseUriList(mime.data(QStringLiteral("text/uri-list")));
}
inline int action(Qt::KeyboardModifiers modifiers, Qt::DropActions supported) {
  if (modifiers.testFlag(Qt::AltModifier) ||
      (modifiers.testFlag(Qt::ControlModifier) && modifiers.testFlag(Qt::ShiftModifier))) {
    return Qt::IgnoreAction;
  }
  const auto requested = modifiers.testFlag(Qt::ShiftModifier) ? Qt::MoveAction : Qt::CopyAction;
  return supported.testFlag(requested) ? requested : Qt::IgnoreAction;
}
inline bool submit(TaskManager& tasks, const QStringList& paths, const QString& destination, int action) {
  if (destination.isEmpty()) {
    return false;
  }
  if (action == Qt::CopyAction) {
    tasks.enqueueCopy(paths, destination);
  } else if (action == Qt::MoveAction) {
    tasks.enqueueMove(paths, destination);
  } else {
    return false;
  }
  return true;
}
}  // namespace FileTransfer
