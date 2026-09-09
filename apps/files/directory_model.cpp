#include "directory_model.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QSemaphore>

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <utility>

namespace {
constexpr int kBatchEntryThreshold = 250;
constexpr int kBatchTimeThresholdMs = 25;

struct DirectoryCloser {
  void operator()(DIR* directory) const { ::closedir(directory); }
};
dirent* readNext(DIR* directory, int readCount, int readErrorAfter) {
  errno = 0;
  if (readErrorAfter >= 0 && readCount >= readErrorAfter) {
    errno = EIO;
    return nullptr;
  }
  return ::readdir(directory);
}
DirectoryEntry readEntry(const QString& path, const QString& name) {
  DirectoryEntry entry;
  entry.name = name;
  entry.absolute_path = QDir(path).absoluteFilePath(name);
  const auto encoded = QFile::encodeName(entry.absolute_path);
  struct stat info{};
  if (::stat(encoded.constData(), &info) == 0) {
    entry.is_dir = S_ISDIR(info.st_mode);
    entry.size = entry.is_dir ? -1 : info.st_size;
    entry.modified =
        QDateTime::fromMSecsSinceEpoch((qint64{info.st_mtim.tv_sec} * 1000) + (info.st_mtim.tv_nsec / 1000000));
  } else {
    const int error = errno;
    entry.stat_failed = true;
    struct stat linkInfo{};
    const bool dangling = (error == ENOENT || error == ENOTDIR) && ::lstat(encoded.constData(), &linkInfo) == 0 &&
                          S_ISLNK(linkInfo.st_mode);
    entry.stat_error =
        dangling ? DirectoryModel::tr("Broken symbolic link") : QString::fromLocal8Bit(std::strerror(error));
  }
  return entry;
}

// At most two batch deliveries may be queued. Cancellation makes the worker wait
// interruptible, including when the model destructor stops processing GUI events.
std::shared_ptr<QSemaphore> acquireBatchSlot(const std::shared_ptr<QSemaphore>& deliverySlots,
                                             const std::shared_ptr<std::atomic_bool>& cancel) {
  while (!cancel->load()) {
    if (deliverySlots->tryAcquire(1, 5)) {
      return {deliverySlots.get(), [deliverySlots](QSemaphore*) { deliverySlots->release(); }};
    }
  }
  return {};
}
using PublishBatch = std::function<void(QList<DirectoryEntry>, bool, QString)>;
void walkDirectory(const QString& path, const std::shared_ptr<std::atomic_bool>& cancel,
                   const std::function<void()>& beforeOpen, int readErrorAfter, const PublishBatch& publish) {
  QList<DirectoryEntry> buffer;
  QElapsedTimer sinceFlush;
  sinceFlush.start();
  auto flush = [&](bool finished, QString error = {}) {
    publish(std::exchange(buffer, {}), finished, std::move(error));
    sinceFlush.restart();
  };
  if (beforeOpen) {
    beforeOpen();
  }
  if (cancel->load()) {
    flush(true);
    return;
  }
  const std::unique_ptr<DIR, DirectoryCloser> directory(::opendir(QFile::encodeName(path).constData()));
  if (!directory) {
    const int error = errno;
    flush(true, DirectoryModel::tr("Cannot open folder: %1").arg(QString::fromLocal8Bit(std::strerror(error))));
    return;
  }
  int readCount = 0;
  while (!cancel->load()) {
    auto* item = readNext(directory.get(), readCount, readErrorAfter);
    if (item == nullptr) {
      const int error = errno;
      flush(true, error == 0
                      ? QString{}
                      : DirectoryModel::tr("Cannot read folder: %1").arg(QString::fromLocal8Bit(std::strerror(error))));
      return;
    }
    const auto name = QFile::decodeName(static_cast<const char*>(item->d_name));
    if (name == u"." || name == u"..") {
      continue;
    }
    ++readCount;
    buffer.append(readEntry(path, name));
    if (buffer.size() >= kBatchEntryThreshold || sinceFlush.elapsed() >= kBatchTimeThresholdMs) {
      flush(false);
    }
  }
  flush(true);
}
}  // namespace

DirectoryModel::DirectoryModel(QObject* parent) : QAbstractListModel(parent), worker_(new QObject) {
  worker_->moveToThread(&thread_);
  connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(&thread_, &QThread::finished, this, &DirectoryModel::shutdownFinished);
  thread_.start();
}
DirectoryModel::~DirectoryModel() {
  stopping_ = true;
  if (cancellation_) {
    cancellation_->store(true);
  }
  thread_.quit();
  thread_.wait();
}
int DirectoryModel::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}
QVariant DirectoryModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.model() != this || index.row() >= entries_.size()) {
    return {};
  }
  const auto& entry = entries_[index.row()];
  switch (role) {
    case NameRole:
      return entry.name;
    case PathRole:
      return entry.absolute_path;
    case IsDirRole:
      return entry.is_dir;
    case SizeRole:
      return entry.size;
    case ModifiedRole:
      return entry.modified;
    case IsHiddenRole:
      return entry.name.startsWith(u'.');
    case StatFailedRole:
      return entry.stat_failed;
    case StatErrorRole:
      return entry.stat_error;
    default:
      return {};
  }
}
QHash<int, QByteArray> DirectoryModel::roleNames() const {
  return {
      {NameRole, "name"},
      {PathRole, "path"},
      {IsDirRole, "isDir"},
      {SizeRole, "size"},
      {ModifiedRole, "modified"},
      {IsHiddenRole, "isHidden"},
      {StatFailedRole, "statFailed"},
      {StatErrorRole, "statError"},
  };
}
void DirectoryModel::load(const QString& path) {
  if (stopping_) {
    return;
  }
  ++generation_;
  if (cancellation_) {
    cancellation_->store(true);
  }
  beginResetModel();
  entries_.clear();
  name_to_row_.clear();
  seen_this_refresh_.clear();
  endResetModel();
  directory_path_ = path;
  directory_error_.clear();
  scanning_ = true;
  emit changed();
  startWalk(path, /*diff=*/false);
}
void DirectoryModel::refresh() {
  if (stopping_ || directory_path_.isEmpty()) {
    return;
  }
  ++generation_;
  if (cancellation_) {
    cancellation_->store(true);
  }
  seen_this_refresh_.clear();
  directory_error_.clear();
  scanning_ = true;
  emit changed();
  startWalk(directory_path_, /*diff=*/true);
}
void DirectoryModel::shutdown() {
  if (stopping_) {
    return;
  }
  stopping_ = true;
  if (cancellation_) {
    cancellation_->store(true);
  }
  scanning_ = false;
  if (!walk_in_flight_) {
    thread_.quit();
  }
}
void DirectoryModel::startWalk(const QString& path, bool diff) {
  const auto generation = generation_;
  cancellation_ = std::make_shared<std::atomic_bool>(false);
  const auto cancel = cancellation_;
  walk_in_flight_ = true;
  const auto beforeOpen = before_open_for_test_;
  const int readErrorAfter = read_error_after_for_test_;
  const auto deliverySlots = std::make_shared<QSemaphore>(2);
  QMetaObject::invokeMethod(
      worker_,
      [this, path, generation, diff, cancel, beforeOpen, readErrorAfter, deliverySlots] {
        walkDirectory(
            path, cancel, beforeOpen, readErrorAfter,
            [this, generation, diff, cancel, deliverySlots](QList<DirectoryEntry> entries, bool finished,
                                                            QString error) {
              const auto slot = acquireBatchSlot(deliverySlots, cancel);
              if (!slot && !finished) {
                return;
              }
              QMetaObject::invokeMethod(
                  this,
                  [this, generation, diff, entries = std::move(entries), finished, error = std::move(error), slot] {
                    applyBatch(Batch{.generation = generation,
                                     .diff = diff,
                                     .entries = entries,
                                     .finished = finished,
                                     .directory_error = error});
                  },
                  Qt::QueuedConnection);
            });
      },
      Qt::QueuedConnection);
}
void DirectoryModel::applyBatch(const Batch& batch) {
  if (batch.generation != generation_) {
    return;
  }
  if (batch.diff) {
    applyDiffEntries(batch.entries);
  } else {
    appendEntries(batch.entries);
  }
  if (!batch.directory_error.isEmpty()) {
    directory_error_ = batch.directory_error;
    scanning_ = false;
    walk_in_flight_ = false;
    if (stopping_) {
      thread_.quit();
    }
    emit changed();
    return;
  }
  if (batch.finished) {
    scanning_ = false;
    walk_in_flight_ = false;
    if (batch.diff) {
      finishDiff();
    }
    if (stopping_) {
      thread_.quit();
    }
  }
  emit changed();
}
void DirectoryModel::appendEntries(const QList<DirectoryEntry>& entries) {
  if (entries.isEmpty()) {
    return;
  }
  const int first = static_cast<int>(entries_.size());
  beginInsertRows({}, first, first + static_cast<int>(entries.size()) - 1);
  for (const auto& entry : entries) {
    name_to_row_.insert(entry.name, static_cast<int>(entries_.size()));
    entries_.append(entry);
  }
  endInsertRows();
}
void DirectoryModel::applyDiffEntries(const QList<DirectoryEntry>& entries) {
  QList<DirectoryEntry> additions;
  for (const auto& entry : entries) {
    seen_this_refresh_.insert(entry.name);
    const auto existing = name_to_row_.constFind(entry.name);
    if (existing != name_to_row_.constEnd()) {
      const int row = existing.value();
      if (entries_[row] != entry) {
        entries_[row] = entry;
        const auto changedIndex = index(row);
        emit dataChanged(changedIndex, changedIndex);
      }
    } else {
      additions.append(entry);
    }
  }
  appendEntries(additions);
}
void DirectoryModel::finishDiff() {
  for (int last = static_cast<int>(entries_.size()) - 1; last >= 0;) {
    if (seen_this_refresh_.contains(entries_[last].name)) {
      --last;
      continue;
    }
    int first = last;
    while (first > 0 && !seen_this_refresh_.contains(entries_[first - 1].name)) {
      --first;
    }
    beginRemoveRows({}, first, last);
    entries_.remove(first, last - first + 1);
    endRemoveRows();
    last = first - 1;
  }
  name_to_row_.clear();
  for (int row = 0; row < entries_.size(); ++row) {
    name_to_row_.insert(entries_[row].name, row);
  }
  seen_this_refresh_.clear();
}
