#include "directory_model.h"

#include "directory_scan.h"
#include "icon_name_resolver.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QSemaphore>

#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>

namespace {
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
}  // namespace

DirectoryModel::DirectoryModel(QObject* parent)
    : QAbstractListModel(parent), classifier_(std::make_shared<RealLocationClassifier>()), worker_(new QObject) {
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
    case NativePathRole:
      return entry.native_path;
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
    case ModeRole:
      return entry.mode;
    case IsHiddenRole:
      return entry.is_parent ? false : entry.name.startsWith(u'.');
    case StatFailedRole:
      return entry.stat_failed;
    case StatErrorRole:
      return entry.stat_error;
    case IconNameRole:
      return entry.icon_name;
    case IsParentRole:
      return entry.is_parent;
    case IsSymlinkRole:
      return entry.is_symlink;
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
      {ModeRole, "mode"},
      {IsHiddenRole, "isHidden"},
      {StatFailedRole, "statFailed"},
      {StatErrorRole, "statError"},
      {IconNameRole, "iconName"},
      {IsParentRole, "isParent"},
      {IsSymlinkRole, "isSymlink"},
      {NativePathRole, "nativePath"},
  };
}
DirectoryEntry DirectoryModel::syntheticParentEntry(const QString& path, const PlaceList::IconMap* places) {
  return HolonightFileBrowser::detail::syntheticParentEntry(path, places);
}
void DirectoryModel::setPlaceIcons(std::shared_ptr<const PlaceList::IconMap> icons) { place_icons_ = std::move(icons); }
void DirectoryModel::load(const QString& path) {
  if (stopping_) {
    return;
  }
  updates_suspended_ = false;
  ++generation_;
  if (cancellation_) {
    cancellation_->store(true);
  }
  beginResetModel();
  entries_.clear();
  name_to_row_.clear();
  seen_this_refresh_.clear();
  if (!path.isEmpty() && !QDir(path).isRoot()) {
    entries_.append(syntheticParentEntry(path, place_icons_.get()));
    name_to_row_.insert(entries_.first().name, 0);
  }
  endResetModel();
  directory_path_ = path;
  directory_error_.clear();
  scanning_ = true;
  emit changed();
  startWalk(path, /*diff=*/false);
}
void DirectoryModel::refresh() {
  if (stopping_ || updates_suspended_ || directory_path_.isEmpty()) {
    return;
  }
  ++generation_;
  if (cancellation_) {
    cancellation_->store(true);
  }
  seen_this_refresh_.clear();
  if (!entries_.isEmpty() && entries_[0].is_parent) {
    seen_this_refresh_.insert(QStringLiteral(".."));
  }
  directory_error_.clear();
  scanning_ = true;
  emit changed();
  startWalk(directory_path_, /*diff=*/true);
}
void DirectoryModel::suspendUpdates() {
  updates_suspended_ = true;
  if (cancellation_) {
    cancellation_->store(true);
  }
  scanning_ = false;
  emit changed();
}
void DirectoryModel::resumeUpdates() {
  if (!updates_suspended_) {
    return;
  }
  updates_suspended_ = false;
  refresh();
}
int DirectoryModel::insertPlaceholderRow() {
  const int row = static_cast<int>(entries_.size());
  DirectoryEntry placeholder;
  placeholder.is_placeholder = true;
  placeholder.icon_name = IconNameResolver::genericFallbackName(false);  // REQ-F-011: never a folder
  beginInsertRows({}, row, row);
  entries_.append(placeholder);
  endInsertRows();
  return row;
}
void DirectoryModel::removePlaceholderRow(int row) {
  if (row < 0 || row >= entries_.size() || !entries_[row].is_placeholder) {
    return;
  }
  beginRemoveRows({}, row, row);
  entries_.remove(row);
  endRemoveRows();
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
void DirectoryModel::validateForRestore(const QString& path) {
  if (stopping_) {
    return;
  }
  const auto classifier = classifier_;
  QMetaObject::invokeMethod(
      worker_,
      [this, path, classifier] {
        const auto encoded = QFile::encodeName(path);
        struct stat info{};
        auto outcome = RestoreOutcome::Ok;
        if (::stat(encoded.constData(), &info) != 0) {
          // A dangling or permission-blocked path is indistinguishable here; neither can be opened.
          outcome = errno == EACCES ? RestoreOutcome::NotReadable : RestoreOutcome::DoesNotExist;
        } else if (!S_ISDIR(info.st_mode)) {
          outcome = RestoreOutcome::NotDirectory;
        } else if (::access(encoded.constData(), R_OK | X_OK) != 0) {
          outcome = RestoreOutcome::NotReadable;
        } else if (classifier->classify(path) != LocationClassifier::Classification::Local) {
          outcome = RestoreOutcome::NotLocal;
        }
        QMetaObject::invokeMethod(
            this,
            [this, path, outcome] {
              if (!stopping_) {
                emit restoreValidated(path, outcome);
              }
            },
            Qt::QueuedConnection);
      },
      Qt::QueuedConnection);
}
void DirectoryModel::resolveLocation(const QString& path, quint64 generation) {
  // Runs on the existing filesystem worker, never on the GUI thread.
  const auto canonicalPath = QFileInfo(path).canonicalFilePath();
  QMetaObject::invokeMethod(
      this,
      [this, path, canonicalPath, generation] {
        if (generation == generation_ && !stopping_ && !updates_suspended_) {
          emit locationResolved(path, canonicalPath);
        }
      },
      Qt::QueuedConnection);
}
void DirectoryModel::startWalk(const QString& path, bool diff) {
  const auto generation = generation_;
  cancellation_ = std::make_shared<std::atomic_bool>(false);
  const auto cancel = cancellation_;
  walk_in_flight_ = true;
  const auto beforeOpen = before_open_for_test_;
  const int readErrorAfter = read_error_after_for_test_;
  const auto deliverySlots = std::make_shared<QSemaphore>(2);
  // Refreshes of the same folder are never reclassified (DESIGN.md §12).
  const auto classifier = diff ? nullptr : classifier_;
  // Snapshot like classifier_: the worker never touches a member, and an in-flight walk keeps its own map alive.
  const auto placeIcons = place_icons_;
  // Accessed only by GUI-thread deliveries. Once accepted, a successful load remains a
  // tracking candidate even if a refresh, navigation or shutdown happens during classification.
  const auto acceptedLoad = std::make_shared<bool>(false);
  QMetaObject::invokeMethod(
      worker_,
      [this, path, generation, diff, cancel, beforeOpen, readErrorAfter, deliverySlots, classifier, acceptedLoad,
       placeIcons] {
        if (!cancel->load()) {
          resolveLocation(path, generation);
        }
        HolonightFileBrowser::detail::walkDirectory(
            path, cancel, beforeOpen, readErrorAfter, placeIcons,
            [this, path, generation, diff, cancel, deliverySlots, classifier, acceptedLoad](
                QList<DirectoryEntry> entries, bool finished, QString error) {
              const auto slot = acquireBatchSlot(deliverySlots, cancel);
              if (!slot && !finished) {
                return;
              }
              const bool succeeded = finished && classifier && error.isEmpty() && !cancel->load();
              QMetaObject::invokeMethod(
                  this,
                  [this, generation, diff, entries = std::move(entries), finished, error = std::move(error), slot,
                   acceptedLoad] {
                    if (finished && !diff && error.isEmpty() && generation == generation_ && !stopping_ &&
                        !updates_suspended_) {
                      *acceptedLoad = true;
                    }
                    applyBatch(Batch{
                        .generation = generation,
                        .diff = diff,
                        .entries = entries,
                        .finished = finished,
                        .directory_error = error,
                    });
                  },
                  Qt::QueuedConnection);
              if (succeeded) {
                // Posted after the final batch, so the listing is never held back by a slow mount.
                const auto classification = classifier->classify(path);
                QMetaObject::invokeMethod(
                    this,
                    [this, path, classification, acceptedLoad] {
                      if (*acceptedLoad) {
                        emit loadSucceeded(path, classification);
                      }
                    },
                    Qt::QueuedConnection);
              }
            });
      },
      Qt::QueuedConnection);
}
void DirectoryModel::applyBatch(const Batch& batch) {
  if (batch.generation != generation_) {
    return;
  }
  if (batch.finished) {
    walk_in_flight_ = false;
    if (stopping_) {
      thread_.quit();
    }
  }
  if (updates_suspended_ || stopping_) {
    return;
  }
  if (batch.diff) {
    applyDiffEntries(batch.entries);
  } else {
    appendEntries(replaceParentRow(batch.entries));
  }
  if (!batch.directory_error.isEmpty()) {
    if (entries_.size() == 1 && entries_[0].is_parent) {
      beginResetModel();
      entries_.clear();
      name_to_row_.clear();
      endResetModel();
    }
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
QList<DirectoryEntry> DirectoryModel::replaceParentRow(QList<DirectoryEntry> entries) {
  if (entries.isEmpty() || !entries.first().is_parent) {
    return entries;
  }
  const auto parent = entries.takeFirst();
  if (!entries_.isEmpty() && entries_.first().is_parent && entries_.first() != parent) {
    entries_.first() = parent;
    emit dataChanged(index(0), index(0));
  }
  return entries;
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
