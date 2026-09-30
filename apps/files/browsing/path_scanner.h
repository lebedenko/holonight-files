#pragma once

#include <QString>
#include <QVector>

#include <atomic>
#include <functional>

struct PathCandidate {
  QString path;
  QString relativePath;
  bool directory = false;
};

using PathBatchReady = std::function<void(QVector<PathCandidate>)>;
using PathScanFunction = std::function<bool(const QString&, bool, const std::atomic_bool&, const PathBatchReady&)>;

// Filesystem traversal for a worker thread. File symlinks are included;
// symlinked directories are never traversed.
bool scanPaths(const QString& root, bool includeHidden, const std::atomic_bool& cancelled,
               const PathBatchReady& batchReady);
