#include "path_finder_model.h"

#include "path_index_store.h"
#include "path_scanner.h"
#include "settings/general_settings.h"
#include "settings/warning_sink.h"
#include "settings/xdg_paths.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
    fields.append({
        .name = QStringLiteral("name"),
        .text = candidate.relative_path.mid(candidate.relative_path.lastIndexOf(u'/') + 1),
        .weight = 0,
    });
    fields.append({.name = QStringLiteral("path"), .text = std::move(candidate.relative_path), .weight = 0});
    Record record{.id = std::move(candidate.path), .fields = std::move(fields), .boost = 0};
    (candidate.directory ? batches.directories : batches.files).append(std::move(record));
  }
  return batches;
}
}  // namespace

PathFinderModel::PathFinderModel(QObject* parent) : PathFinderModel(scanPaths, parent) { check_root_ = true; }

PathFinderModel::PathFinderModel(PathScanFunction scanner, QObject* parent)
    : QAbstractListModel(parent), scanner_(std::move(scanner)) {
  clock_ = [] { return QDateTime::currentMSecsSinceEpoch(); };
  QStringList diagnostics;
  policy_ = SearchExclusionPolicy::compile(SearchSettings(), diagnostics);
  pool_.setMaxThreadCount(1);
  rank_pool_.setMaxThreadCount(1);
  refresh_timer_.setSingleShot(true);
  invalidation_timer_.setSingleShot(true);
  invalidation_timer_.setInterval(250);
  connect(&refresh_timer_, &QTimer::timeout, this, &PathFinderModel::refreshIfNeeded);
  connect(&invalidation_timer_, &QTimer::timeout, this, &PathFinderModel::refreshIfNeeded);
  shutdown_timer_.setInterval(10);
  connect(&shutdown_timer_, &QTimer::timeout, this, [this] {
    if (pool_.waitForDone(0) && rank_pool_.waitForDone(0)) {
      shutdown_timer_.stop();
      emit shutdownFinished();
    }
  });
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
  rank_pool_.waitForDone();
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
      return row.relative_path;
    case DirectoryRole:
      return row.directory;
    case PositionsRole:
      return QVariant::fromValue(row.positions);
    default:
      return {};
  }
}

QHash<int, QByteArray> PathFinderModel::roleNames() const {
  return {
      {PathRole, "path"},
      {RelativePathRole, "relativePath"},
      {DirectoryRole, "directory"},
      {PositionsRole, "positions"},
  };
}

QString PathFinderModel::pathAt(int row) const { return row >= 0 && row < rows_.size() ? rows_[row].path : QString{}; }

bool PathFinderModel::isDirectoryAt(int row) const { return row >= 0 && row < rows_.size() && rows_[row].directory; }

void PathFinderModel::reportMissing(const QString& path) {
  error_ = tr("Selected result no longer exists. Search again.");
  missing_paths_[stateKey(root_path_, include_hidden_)].insert(path);
  invalidatePaths({path});
  rank_timer_.start(0);
  emit stateChanged();
}

void PathFinderModel::start(const QString& rootPath, bool directoriesOnly) {
  if (shutting_down_) {
    return;
  }
  reloadSearchSettings();
  active_ = true;
  directories_only_ = directoriesOnly;
  query_.clear();
  setRoot(rootPath);
}

void PathFinderModel::stop() {
  active_ = false;
  if (rank_cancel_) {
    rank_cancel_->store(true);
  }
  ++rank_serial_;
  rank_timer_.stop();
  refresh_timer_.stop();
  invalidation_timer_.stop();
  scanning_ = false;
  emit stateChanged();
}

QString PathFinderModel::stateKey(const QString& root, bool hidden) {
  return (hidden ? QStringLiteral("h:") : QStringLiteral("v:")) + root;
}

void PathFinderModel::setRoot(const QString& rootPath) {
  if (shutting_down_) {
    return;
  }
  if (rank_cancel_) {
    rank_cancel_->store(true);
  }
  ++rank_serial_;
  root_path_ = QDir::cleanPath(QDir(rootPath).absolutePath());
  error_.clear();
  const auto cached = (include_hidden_ ? hidden_cache_ : visible_cache_).value(root_path_);
  indexed_count_ = cached ? static_cast<int>(cached->size()) : 0;
  const auto selection = ++selection_serial_;
  if (cached && check_root_ && active_) {
    checkSelectedRoot(selection);
  }
  replaceRows({});
  rank_timer_.start(0);
  refreshIfNeeded();
  emit stateChanged();
}

void PathFinderModel::setClock(std::function<qint64()> clock) { clock_ = std::move(clock); }

void PathFinderModel::checkSelectedRoot(quint64 selection) {
  const auto root = root_path_;
  const auto hidden = include_hidden_;
  rank_pool_.start(QRunnable::create([this, root, hidden, selection] {
    const QFileInfo info(root);
    if (info.isDir() && info.isReadable()) {
      return;
    }
    QMetaObject::invokeMethod(this, [this, root, hidden, selection] { clearUnavailableRoot(root, hidden, selection); });
  }));
}

void PathFinderModel::clearUnavailableRoot(const QString& root, bool hidden, quint64 selection) {
  if (!active_ || shutting_down_ || selection != selection_serial_) {
    return;
  }
  if (job_running_ && job_root_ == root && job_hidden_ == hidden) {
    scan_cancel_->store(true);
    ++scan_serial_;
  }
  (hidden ? hidden_cache_ : visible_cache_).remove(root);
  (hidden ? complete_hidden_roots_ : complete_visible_roots_).remove(root);
  freshness_[stateKey(root, hidden)] = {.completed = clock_(), .dirty = false};
  if (root == homePath() && !hidden) {
    warming_ = false;
  }
  if (rank_cancel_) {
    rank_cancel_->store(true);
  }
  ++rank_serial_;
  replaceRows({});
  error_ = tr("Search root is unavailable or unreadable");
  indexed_count_ = 0;
  scanning_ = false;
  refresh_timer_.start(refresh_interval_);
  emit stateChanged();
}

void PathFinderModel::setRefreshInterval(int milliseconds) { refresh_interval_ = std::max(1, milliseconds); }

void PathFinderModel::warmUp() {
  if (warmed_ || shutting_down_) {
    return;
  }
  reloadSearchSettings();
  warmed_ = true;
  warming_ = true;
  scheduleWork();
}

void PathFinderModel::shutdown() {
  if (shutting_down_) {
    return;
  }
  stop();
  shutting_down_ = true;
  if (scan_cancel_) {
    scan_cancel_->store(true);
  }
  ++scan_serial_;
  pending_root_.clear();
  warming_ = false;
  shutdown_timer_.start();
}

void PathFinderModel::invalidatePaths(const QStringList& paths) {
  if (shutting_down_) {
    return;
  }
  auto overlaps = [&](const QString& root) {
    for (const auto& path : paths) {
      const auto clean = QDir::cleanPath(QDir(path).absolutePath());
      auto within = [](const QString& child, const QString& parent) {
        return child == parent || child.startsWith(parent == QStringLiteral("/") ? parent : parent + u'/');
      };
      if (within(clean, root) || within(root, clean)) {
        return true;
      }
    }
    return false;
  };
  for (auto it = freshness_.begin(); it != freshness_.end(); ++it) {
    if (overlaps(it.key().mid(2))) {
      it->dirty = true;
    }
  }
  if (job_running_ && overlaps(job_root_)) {
    scan_cancel_->store(true);
    ++scan_serial_;
  }
  if (overlaps(homePath())) {
    const auto path = PathIndexStore::cachePath();
    pool_.start(QRunnable::create([path] {
      if (QFile::exists(path) && !QFile::remove(path)) {
        qWarning() << "Cannot invalidate Files search cache:" << path;
      }
    }));
  }
  if (active_ && overlaps(root_path_)) {
    invalidation_timer_.start();
  }
}

void PathFinderModel::refreshIfNeeded() {
  if (!active_ || shutting_down_) {
    return;
  }
  const auto key = stateKey(root_path_, include_hidden_);
  const auto state = freshness_.value(key);
  const qint64 age = clock_() - state.completed;
  if (state.dirty || age >= refresh_interval_) {
    scan();
  } else {
    scanning_ = job_running_ && job_root_ == root_path_ && job_hidden_ == include_hidden_;
    refresh_timer_.start(static_cast<int>(refresh_interval_ - age));
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
  scanning_ = true;
  if (job_running_ && job_root_ == root_path_ && job_hidden_ == include_hidden_ && !scan_cancel_->load()) {
    emit stateChanged();
    return;
  }
  pending_root_ = root_path_;
  pending_hidden_ = include_hidden_;
  if (job_running_) {
    scan_cancel_->store(true);
    ++scan_serial_;
  }
  scheduleWork();
}

void PathFinderModel::scheduleWork() {
  if (shutting_down_ || job_running_ || invalidation_timer_.isActive()) {
    return;
  }
  if (!pending_root_.isEmpty()) {
    const auto root = pending_root_;
    pending_root_.clear();
    launchScan(root, pending_hidden_);
  } else if (warming_) {
    launchScan(homePath(), false);
  }
}

void PathFinderModel::launchScan(const QString& root, bool includeHidden) {
  const auto serial = ++scan_serial_;
  job_running_ = true;
  job_root_ = root;
  job_hidden_ = includeHidden;
  freshness_[stateKey(root, includeHidden)];
  auto cancel = std::make_shared<std::atomic_bool>(false);
  scan_cancel_ = cancel;
  const auto policy = policy_;
  const bool persistent = root == homePath() && !includeHidden;
  const bool load = persistent && !visible_cache_.contains(root);
  const auto cachePath = PathIndexStore::cachePath();
  pool_.start(QRunnable::create([this, root, serial, includeHidden, cancel, policy, persistent, load, cachePath] {
    runScan(root, serial, includeHidden, cancel, policy, persistent, load, cachePath);
  }));
}

void PathFinderModel::loadSnapshot(const QString& root, quint64 serial, const SearchExclusionPolicy& policy,
                                   const std::shared_ptr<std::atomic_bool>& cancel, const QString& cachePath) {
  auto restored = std::make_shared<Index>();
  qint64 completed = 0;
  const bool valid = PathIndexStore::load(
      cachePath, root, policy.fingerprint(), *cancel,
      [restored](QVector<PathCandidate> ready) {
        auto batches = recordsForPaths(std::move(ready));
        restored->append(sourceName(false), std::move(batches.files));
        restored->append(sourceName(true), std::move(batches.directories));
      },
      completed);
  if (valid) {
    QMetaObject::invokeMethod(this, [this, root, serial, restored, completed] {
      if (serial != scan_serial_ || shutting_down_) {
        return;
      }
      visible_cache_.insert(root, restored);
      complete_visible_roots_.insert(root);
      freshness_[stateKey(root, false)].completed = completed;
      if (active_ && root_path_ == root && !include_hidden_) {
        indexed_count_ = static_cast<int>(restored->size());
        emit stateChanged();
        rank_timer_.start(0);
      }
    });
  }
}

void PathFinderModel::runScan(const QString& root, quint64 serial, bool includeHidden,
                              const std::shared_ptr<std::atomic_bool>& cancel, const SearchExclusionPolicy& policy,
                              bool persistent, bool load, const QString& cachePath) {
  if (load) {
    loadSnapshot(root, serial, policy, cancel, cachePath);
  }
  auto index = std::make_shared<Index>();
  std::unique_ptr<PathIndexStore> store;
  if (persistent && !cancel->load()) {
    store = std::make_unique<PathIndexStore>(cachePath, root, policy.fingerprint());
  }
  const bool available =
      !cancel->load() && scanner_(root, includeHidden, policy, *cancel,
                                  [this, root, serial, includeHidden, index, &store](QVector<PathCandidate> ready) {
                                    const int count = static_cast<int>(ready.size());
                                    if (store) {
                                      store->append(ready);
                                    }
                                    auto batches = recordsForPaths(std::move(ready));
                                    index->append(sourceName(false), std::move(batches.files));
                                    index->append(sourceName(true), std::move(batches.directories));
                                    QMetaObject::invokeMethod(this, [this, root, serial, includeHidden, index, count] {
                                      publishScanBatch(root, serial, includeHidden, index, count);
                                    });
                                  });
  if (store && available && !cancel->load()) {
    store->commit(QDateTime::currentMSecsSinceEpoch(), *cancel);
  }
  QMetaObject::invokeMethod(this, [this, root, serial, includeHidden, index, available, cancel] {
    job_running_ = false;
    if (!cancel->load()) {
      finishScan(root, serial, includeHidden, index, available);
    }
    if (!shutting_down_) {
      if (active_ && !invalidation_timer_.isActive()) {
        refreshIfNeeded();
      }
      scheduleWork();
    }
  });
}

void PathFinderModel::publishScanBatch(const QString& root, quint64 serial, bool includeHidden,
                                       const std::shared_ptr<Index>& index, int count) {
  Q_UNUSED(count);
  if (serial != scan_serial_ || shutting_down_) {
    return;
  }
  auto& cache = includeHidden ? hidden_cache_ : visible_cache_;
  const auto& complete = includeHidden ? complete_hidden_roots_ : complete_visible_roots_;
  if (!complete.contains(root)) {
    cache.insert(root, index);
  }
  if (active_ && root == root_path_ && includeHidden == include_hidden_) {
    indexed_count_ = static_cast<int>(index->size());
    if (!rank_timer_.isActive()) {
      rank_timer_.start();
    }
    emit stateChanged();
  }
}

void PathFinderModel::finishScan(const QString& root, quint64 serial, bool includeHidden,
                                 const std::shared_ptr<Index>& index, bool available) {
  if (serial != scan_serial_ || shutting_down_) {
    return;
  }
  auto& cache = includeHidden ? hidden_cache_ : visible_cache_;
  auto& complete = includeHidden ? complete_hidden_roots_ : complete_visible_roots_;
  if (available) {
    cache.insert(root, index);
    complete.insert(root);
    missing_paths_.remove(stateKey(root, includeHidden));
  } else {
    cache.remove(root);
    complete.remove(root);
  }
  freshness_[stateKey(root, includeHidden)] = {.completed = clock_(), .dirty = false};
  if (root == homePath() && !includeHidden) {
    warming_ = false;
  }
  if (active_ && root == root_path_ && includeHidden == include_hidden_) {
    error_ = available ? QString{} : tr("Search root is unavailable or unreadable");
    scanning_ = false;
    indexed_count_ = available ? static_cast<int>(index->size()) : 0;
    rank_timer_.start(0);
    refresh_timer_.start(refresh_interval_);
    emit stateChanged();
  }
}

void PathFinderModel::rank() {
  if (!active_ || shutting_down_) {
    return;
  }
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
  const auto missing = missing_paths_.value(stateKey(root_path_, include_hidden_));
  if (!index || query.trimmed().isEmpty()) {
    replaceRows({});
    return;
  }
  rank_pool_.start(QRunnable::create([this, serial, cancel, index, root, query, directoriesOnly, missing] {
    const auto hits = index->search(query, {
                                               .limit = 96,
                                               .profile = HolonightSearch::Profile::Path,
                                               .cancelled = cancel.get(),
                                               .source = sourceName(directoriesOnly),
                                           });
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
    row.relative_path = rootDirectory.relativeFilePath(hit.id);
    row.directory = directoriesOnly;
    const int basenameStart = static_cast<int>(row.relative_path.lastIndexOf(u'/') + 1);
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

void PathFinderModel::reloadSearchSettings() {
  const QString path = XdgPaths::configFilePath();
  const auto parsed = TomlDocument::parseFile(path);
  QStringList diagnostics;
  for (const auto& diagnostic : parsed.diagnostics) {
    diagnostics.append(
        QStringLiteral("%1:%2: %3")
            .arg(path)
            .arg(diagnostic.line)
            .arg(
                tr("Cannot read search configuration (%1). Retaining the last valid policy.").arg(diagnostic.message)));
  }
  if (parsed.diagnostics.empty()) {
    SettingsRegistry registry;
    GeneralSettings::declare(registry);
    SearchSettings::declare(registry);
    for (const auto& diagnostic : registry.apply(parsed.document)) {
      diagnostics.append(QStringLiteral("%1:%2: %3").arg(path).arg(diagnostic.line).arg(diagnostic.message));
    }
    const auto policy = SearchExclusionPolicy::compile(SearchSettings::read(registry), diagnostics);
    if (!(policy == policy_)) {
      if (scan_cancel_) {
        scan_cancel_->store(true);
      }
      if (rank_cancel_) {
        rank_cancel_->store(true);
      }
      ++scan_serial_;
      ++rank_serial_;
      freshness_.clear();
      if (policy_loaded_) {
        const auto path = PathIndexStore::cachePath();
        pool_.start(QRunnable::create([path] {
          if (QFile::exists(path) && !QFile::remove(path)) {
            qWarning() << "Cannot invalidate Files search cache:" << path;
          }
        }));
      }
      visible_cache_.clear();
      hidden_cache_.clear();
      complete_visible_roots_.clear();
      complete_hidden_roots_.clear();
      policy_ = policy;
    }
  }
  policy_loaded_ = true;
  StderrWarningSink warnings;
  for (const QString& diagnostic : diagnostics) {
    warnings.warn(diagnostic);
  }
  config_error_ = diagnostics.join(u'\n');
}
