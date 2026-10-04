#include "path_scanner.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>

namespace {
void scanFolder(const QString& folder, const QDir& root, QVector<QString>& pending, QVector<PathCandidate>& batch,
                bool includeHidden, const SearchExclusionPolicy& policy, const std::atomic_bool& cancelled,
                const PathBatchReady& batchReady) {
  auto filters = QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Readable;
  if (includeHidden) {
    filters |= QDir::Hidden;
  }
  QDirIterator iterator(folder, filters);
  while (iterator.hasNext() && !cancelled.load()) {
    iterator.next();
    const QFileInfo info = iterator.fileInfo();
    if ((!includeHidden && info.isHidden()) || (info.isSymLink() && info.isDir())) {
      continue;
    }
    const bool directory = info.isDir();
    if (policy.excludes(info.absoluteFilePath(), directory, root.absolutePath())) {
      continue;
    }
    batch.append({
        .path = info.absoluteFilePath(),
        .relative_path = root.relativeFilePath(info.absoluteFilePath()),
        .directory = directory,
    });
    if (directory) {
      pending.append(info.absoluteFilePath());
    }
    if (batch.size() >= 256) {
      batchReady(std::move(batch));
      batch.clear();
    }
  }
}
}  // namespace

bool scanPaths(const QString& root, bool includeHidden, const SearchExclusionPolicy& policy,
               const std::atomic_bool& cancelled, const PathBatchReady& batchReady) {
  const QFileInfo rootInfo(root);
  if (!rootInfo.isDir() || !rootInfo.isReadable()) {
    return false;
  }
  QVector<QString> pending{root};
  QVector<PathCandidate> batch;
  const QDir rootDirectory(root);
  while (!pending.isEmpty() && !cancelled.load()) {
    scanFolder(pending.takeLast(), rootDirectory, pending, batch, includeHidden, policy, cancelled, batchReady);
  }
  if (!cancelled.load() && !batch.isEmpty()) {
    batchReady(std::move(batch));
  }
  return true;
}
