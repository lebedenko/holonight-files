#include "path_finder_model.h"

#include "path_scanner.h"

#include <QDir>
#include <QRunnable>

#include <algorithm>

using HolonightSearch::Field;
using HolonightSearch::Index;
using HolonightSearch::Record;

namespace {
QString sourceName(bool directory) { return directory ? QStringLiteral("directories") : QStringLiteral("files"); }

struct RecordBatches {
  QVector<Record> files;
  QVector<Record> directories;
};

RecordBatches recordsForPaths(QVector<PathCandidate> ready) {
  RecordBatches batches;
  batches.files.reserve(ready.size());
  batches.directories.reserve(ready.size());
  for (PathCandidate& candidate : ready) {
    QVector<Field> fields;
    fields.reserve(2);
    fields.append({.name = QStringLiteral("name"),
                   .text = candidate.relativePath.mid(candidate.relativePath.lastIndexOf(u'/') + 1),
                   .weight = 0});
    fields.append({.name = QStringLiteral("path"), .text = std::move(candidate.relativePath), .weight = 0});
    Record record{.id = std::move(candidate.path), .fields = std::move(fields), .boost = 0};
    (candidate.directory ? batches.directories : batches.files).append(std::move(record));
  }
  return batches;
}
}  // namespace

PathFinderModel::PathFinderModel(QObject* parent) : PathFinderModel(scanPaths, parent) {}

PathFinderModel::PathFinderModel(PathScanFunction scanner, QObject* parent)
    : QAbstractListModel(parent), scanner_(std::move(scanner)) {
  pool_.setMaxThreadCount(2);
  rank_timer_.setSingleShot(true);
  rank_timer_.setInterval(50);
  connect(&rank_timer_, &QTimer::timeout, this, &PathFinderModel::rank);
}

PathFinderModel::~PathFinderModel() {
  if (scan_cancel_) {
    scan_cancel_->store(true);
  }
  if (rank_cancel_) {
    rank_cancel_->store(true);
  }
  pool_.waitForDone();
}

QString PathFinderModel::homePath() { return QDir::homePath(); }

int PathFinderModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(rows_.size());
}

QVariant PathFinderModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= rows_.size()) {
    return {};
  }
  const auto& row = rows_[index.row()];
  switch (role) {
    case PathRole:
      return row.path;
    case RelativePathRole:
      return row.relativePath;
    case DirectoryRole:
      return row.directory;
    case PositionsRole:
      return QVariant::fromValue(row.positions);
    default:
      return {};
  }
}

QHash<int, QByteArray> PathFinderModel::roleNames() const {
  return {{PathRole, "path"},
          {RelativePathRole, "relativePath"},
          {DirectoryRole, "directory"},
          {PositionsRole, "positions"}};
}

QString PathFinderModel::pathAt(int row) const { return row >= 0 && row < rows_.size() ? rows_[row].path : QString{}; }

bool PathFinderModel::isDirectoryAt(int row) const { return row >= 0 && row < rows_.size() && rows_[row].directory; }

void PathFinderModel::reportMissing(const QString& path) {
  error_ = tr("Selected result no longer exists. Search again.");
  missing_paths_.insert(path);
  rank_timer_.start(0);
  emit stateChanged();
}

void PathFinderModel::start(const QString& rootPath, bool directoriesOnly) {
  directories_only_ = directoriesOnly;
  query_.clear();
  setRoot(rootPath);
}

void PathFinderModel::stop() {
  if (scan_cancel_) {
    scan_cancel_->store(true);
  }
  if (rank_cancel_) {
    rank_cancel_->store(true);
  }
  ++scan_serial_;
  ++rank_serial_;
  rank_timer_.stop();
  scanning_ = false;
  emit stateChanged();
}

void PathFinderModel::setRoot(const QString& rootPath) {
  if (scan_cancel_) {
    scan_cancel_->store(true);
  }
  if (rank_cancel_) {
    rank_cancel_->store(true);
  }
  ++scan_serial_;
  ++rank_serial_;
  root_path_ = QDir::cleanPath(rootPath);
  error_.clear();
  const auto& cache = include_hidden_ ? hidden_cache_ : visible_cache_;
  const auto& completeRoots = include_hidden_ ? complete_hidden_roots_ : complete_visible_roots_;
  const auto cached = cache.value(root_path_);
  const bool complete = cached && completeRoots.contains(root_path_);
  scanning_ = !complete;
  refresh_started_ = complete;
  indexed_count_ = complete ? static_cast<int>(cached->size()) : 0;
  replaceRows({});
  emit stateChanged();
  rank_timer_.start(0);  // Show any cached paths immediately.
  if (!complete) {
    scan();
  }
}

void PathFinderModel::setIncludeHidden(bool includeHidden) {
  if (include_hidden_ == includeHidden) {
    return;
  }
  include_hidden_ = includeHidden;
  if (!root_path_.isEmpty()) {
    setRoot(root_path_);
  } else {
    emit stateChanged();
  }
}

void PathFinderModel::setQuery(const QString& query) {
  if (query_ == query) {
    return;
  }
  query_ = query;
  error_.clear();
  if (rank_cancel_) {
    rank_cancel_->store(true);
  }
  ++rank_serial_;
  rank_timer_.start(0);
  emit stateChanged();
}

void PathFinderModel::scan() {
  const QString root = root_path_;
  const auto serial = scan_serial_;
  const bool includeHidden = include_hidden_;
  auto cancel = std::make_shared<std::atomic_bool>(false);
  auto index = std::make_shared<Index>();
  scan_cancel_ = cancel;
  pool_.start(QRunnable::create([this, root, serial, includeHidden, cancel, index] {
    const bool available = scanner_(
        root, includeHidden, *cancel, [this, root, serial, includeHidden, index](QVector<PathCandidate> ready) {
          const int count = static_cast<int>(ready.size());
          auto batches = recordsForPaths(std::move(ready));
          index->append(sourceName(false), std::move(batches.files));
          index->append(sourceName(true), std::move(batches.directories));
          QMetaObject::invokeMethod(this, [this, root, serial, includeHidden, index, count] {
            publishScanBatch(root, serial, includeHidden, index, count);
          });
        });
    QMetaObject::invokeMethod(this, [this, root, serial, includeHidden, index, available] {
      finishScan(root, serial, includeHidden, index, available);
    });
  }));
}

void PathFinderModel::publishScanBatch(const QString& root, quint64 serial, bool includeHidden,
                                       const std::shared_ptr<Index>& index, int count) {
  if (serial != scan_serial_) {
    return;
  }
  if (!refresh_started_) {
    (includeHidden ? hidden_cache_ : visible_cache_).insert(root, index);
    refresh_started_ = true;
    rank_timer_.start(0);
  } else if (!rank_timer_.isActive()) {
    rank_timer_.start();
  }
  indexed_count_ += count;
  emit stateChanged();
}

void PathFinderModel::finishScan(const QString& root, quint64 serial, bool includeHidden,
                                 const std::shared_ptr<Index>& index, bool available) {
  if (serial != scan_serial_) {
    return;
  }
  if (!refresh_started_) {
    (includeHidden ? hidden_cache_ : visible_cache_).insert(root, index);
  }
  if (available) {
    (includeHidden ? complete_hidden_roots_ : complete_visible_roots_).insert(root);
  }
  error_ = available ? QString{} : tr("Search root is unavailable or unreadable");
  scanning_ = false;
  rank_timer_.start(0);
  emit stateChanged();
}

void PathFinderModel::rank() {
  if (rank_cancel_) {
    rank_cancel_->store(true);
  }
  auto cancel = std::make_shared<std::atomic_bool>(false);
  rank_cancel_ = cancel;
  const auto serial = ++rank_serial_;
  const auto index = (include_hidden_ ? hidden_cache_ : visible_cache_).value(root_path_);
  const auto root = root_path_;
  const auto query = query_;
  const bool directoriesOnly = directories_only_;
  const auto missing = missing_paths_;
  if (!index || query.trimmed().isEmpty()) {
    replaceRows({});
    return;
  }
  pool_.start(QRunnable::create([this, serial, cancel, index, root, query, directoriesOnly, missing] {
    const auto hits = index->search(query, {.limit = 96,
                                            .profile = HolonightSearch::Profile::Path,
                                            .cancelled = cancel.get(),
                                            .source = sourceName(directoriesOnly)});
    if (cancel->load()) {
      return;
    }
    auto ranked = rowsForHits(hits, root, directoriesOnly, missing);
    QMetaObject::invokeMethod(this, [this, serial, ranked = std::move(ranked)] {
      if (serial == rank_serial_) {
        replaceRows(ranked);
      }
    });
  }));
}

QVector<PathFinderModel::Row> PathFinderModel::rowsForHits(const QVector<HolonightSearch::Result>& hits,
                                                           const QString& root, bool directoriesOnly,
                                                           const QSet<QString>& missing) {
  QVector<Row> ranked;
  ranked.reserve(std::min(80, static_cast<int>(hits.size())));
  const QDir rootDirectory(root);
  for (const auto& hit : hits) {
    if (missing.contains(hit.id)) {
      continue;
    }
    Row row;
    row.path = hit.id;
    row.relativePath = rootDirectory.relativeFilePath(hit.id);
    row.directory = directoriesOnly;
    const int basenameStart = static_cast<int>(row.relativePath.lastIndexOf(u'/') + 1);
    for (const auto& highlight : hit.highlights) {
      for (const int position : highlight.positions) {
        row.positions.append(position + (highlight.field == QStringLiteral("name") ? basenameStart : 0));
      }
    }
    std::ranges::sort(row.positions);
    const auto duplicates = std::ranges::unique(row.positions);
    row.positions.erase(duplicates.begin(), duplicates.end());
    ranked.append(std::move(row));
    if (ranked.size() == 80) {
      break;
    }
  }
  return ranked;
}

void PathFinderModel::replaceRows(QVector<Row> rows) {
  beginResetModel();
  rows_ = std::move(rows);
  endResetModel();
}
