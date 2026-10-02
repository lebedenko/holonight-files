#pragma once

#include "path_scanner.h"

#include <QCryptographicHash>
#include <QSaveFile>

// Private, bounded-memory snapshot codec. All methods run on the traversal worker.
class PathIndexStore {
 public:
  static QString cachePath();
  static bool load(const QString& path, const QString& root, const QByteArray& policy,
                   const std::atomic_bool& cancelled, const PathBatchReady& batchReady, qint64& completed);
  PathIndexStore(const QString& path, const QString& root, const QByteArray& policy);
  void append(const QVector<PathCandidate>& batch);
  bool commit(qint64 completed, const std::atomic_bool& cancelled);

 private:
  void write(const QByteArray& bytes);
  QSaveFile file_;
  QCryptographicHash hash_{QCryptographicHash::Sha256};
  bool valid_ = false;
};
