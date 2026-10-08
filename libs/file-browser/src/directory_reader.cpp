#include "directory_scan.h"

#include <QFile>
#include <QSemaphore>

#include <HolonightFileBrowser/directory_reader.h>
#include <utility>

namespace HolonightFileBrowser {
DirectoryReader::DirectoryReader(QObject* parent) : QAbstractListModel(parent), worker_(new QObject) {
  worker_->moveToThread(&thread_);
  connect(&thread_, &QThread::finished, worker_, &QObject::deleteLater);
  connect(&thread_, &QThread::finished, this, &DirectoryReader::shutdownFinished);
  thread_.start();
}
DirectoryReader::~DirectoryReader() {
  shutdown();
  thread_.wait();
}
int DirectoryReader::rowCount(const QModelIndex& parent) const {
  return parent.isValid() ? 0 : static_cast<int>(entries_.size());
}
QVariant DirectoryReader::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.model() != this || index.row() >= entries_.size()) {
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
QHash<int, QByteArray> DirectoryReader::roleNames() const {
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

void DirectoryReader::setPlaceIcons(std::shared_ptr<const PlaceList::IconMap> icons) { icons_ = std::move(icons); }
void DirectoryReader::load(const QString& path) { loadNative(QFile::encodeName(path)); }
void DirectoryReader::loadNative(const QByteArray& path) {
  if (stopping_) {
    return;
  }
  if (cancel_) {
    cancel_->store(true);
  }
  ++generation_;
  beginResetModel();
  entries_.clear();
  endResetModel();
  path_ = QFile::decodeName(path);
  native_path_ = path;
  error_.clear();
  if (!path.startsWith('/') || path.contains('\0')) {
    scanning_ = false;
    error_ = tr("Folder path must be absolute");
    emit changed();
    return;
  }
  scanning_ = true;
  startWalk();
  emit changed();
}
void DirectoryReader::startWalk() {
  const auto path = path_;
  const auto nativePath = native_path_;
  const auto generation = generation_;
  cancel_ = std::make_shared<std::atomic_bool>(false);
  const auto cancel = cancel_;
  const auto icons = icons_;
  const auto deliverySlots = std::make_shared<QSemaphore>(2);
  QMetaObject::invokeMethod(
      worker_,
      [this, path, nativePath, generation, cancel, icons, deliverySlots] {
        detail::walkDirectory(
            path, cancel, {}, -1, icons,
            [this, generation, cancel, deliverySlots](QList<DirectoryEntry> entries, bool finished, QString error) {
              while (!cancel->load()) {
                if (!deliverySlots->tryAcquire(1, 5)) {
                  continue;
                }
                auto slot = std::shared_ptr<QSemaphore>(deliverySlots.get(),
                                                        [deliverySlots](QSemaphore*) { deliverySlots->release(); });
                QMetaObject::invokeMethod(
                    this,
                    [this, generation, entries = std::move(entries), finished, error = std::move(error), slot] {
                      if (generation != generation_ || stopping_) {
                        return;
                      }
                      if (!entries.isEmpty()) {
                        const int first = static_cast<int>(entries_.size());
                        beginInsertRows({}, first, first + static_cast<int>(entries.size()) - 1);
                        entries_.append(entries);
                        endInsertRows();
                      }
                      if (finished) {
                        scanning_ = false;
                        error_ = error;
                      }
                      emit changed();
                    },
                    Qt::QueuedConnection);
                return;
              }
            },
            nativePath);
      },
      Qt::QueuedConnection);
}
void DirectoryReader::shutdown() {
  if (stopping_) {
    return;
  }
  stopping_ = true;
  ++generation_;
  if (cancel_) {
    cancel_->store(true);
  }
  scanning_ = false;
  thread_.quit();
  emit changed();
}
}  // namespace HolonightFileBrowser
