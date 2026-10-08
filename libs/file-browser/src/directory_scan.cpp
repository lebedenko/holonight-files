#include "directory_scan.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>

#include <HolonightFileBrowser/icon_name_resolver.h>
#include <cerrno>
#include <cstring>
#include <dirent.h>
#include <memory>
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
// "" when there is no map or the exact cleaned path is not a standard place.
QString namedIconFor(const PlaceList::IconMap* places, const QString& cleanedPath) {
  return places == nullptr ? QString() : places->value(cleanedPath);
}
DirectoryEntry readEntry(const QString& path, const QString& name, const PlaceList::IconMap* places,
                         const QByteArray& nativePath = {}) {
  DirectoryEntry entry;
  entry.name = name;
  entry.absolute_path = QDir(path).absoluteFilePath(name);
  const auto encoded = nativePath.isEmpty() ? QFile::encodeName(entry.absolute_path) : nativePath;
  entry.native_path = encoded;
  // One lstat() per entry, ahead of the target-resolving stat() below, so both the success and
  // failure branches can read entry.is_symlink without a second syscall (REQ-F-010/011/012/013).
  struct stat linkInfo{};
  entry.is_symlink = ::lstat(encoded.constData(), &linkInfo) == 0 && S_ISLNK(linkInfo.st_mode);
  struct stat info{};
  if (::stat(encoded.constData(), &info) == 0) {
    entry.is_dir = S_ISDIR(info.st_mode);
    entry.size = entry.is_dir ? -1 : info.st_size;
    entry.mode = info.st_mode;
    entry.modified =
        QDateTime::fromMSecsSinceEpoch((qint64{info.st_mtim.tv_sec} * 1000) + (info.st_mtim.tv_nsec / 1000000));
    // Only the entry's own listed path is looked up: no realpath/readlink, so a symlink to a place stays generic.
    const auto named = entry.is_dir ? namedIconFor(places, entry.absolute_path) : QString();
    entry.icon_name =
        IconNameResolver::candidateIconNames(info.st_mode, name, named).join(IconNameResolver::kChainSeparator);
  } else {
    const int error = errno;
    entry.stat_failed = true;
    // A dangling link's own extension says nothing about a target that doesn't exist (REQ-F-006).
    entry.icon_name = IconNameResolver::genericFallbackName(false);
    const bool dangling = (error == ENOENT || error == ENOTDIR) && entry.is_symlink;
    entry.stat_error = dangling ? QCoreApplication::translate("DirectoryModel", "Broken symbolic link")
                                : QString::fromLocal8Bit(std::strerror(error));
  }
  return entry;
}
// The ".." row stands for the parent folder itself, so it carries the parent's real metadata; the
// synthetic row is kept when the parent cannot be stat'ed.
DirectoryEntry readParentEntry(const QString& path, const PlaceList::IconMap* places, const QByteArray& nativePath) {
  const auto parentPath = QDir::cleanPath(QFileInfo(path).absolutePath());
  const auto nativeParent = nativePath.left(qMax<qsizetype>(1, nativePath.lastIndexOf('/')));
  auto entry = readEntry(parentPath, QStringLiteral("."), nullptr, nativeParent + "/.");
  if (entry.stat_failed) {
    return HolonightFileBrowser::detail::syntheticParentEntry(path, places, nativeParent);
  }
  entry.name = QStringLiteral("..");
  entry.native_path = nativeParent;
  entry.absolute_path = parentPath;
  entry.is_parent = true;
  entry.icon_name = IconNameResolver::candidateIconNames(entry.mode, entry.name, namedIconFor(places, parentPath))
                        .join(IconNameResolver::kChainSeparator);
  return entry;
}

}  // namespace

namespace HolonightFileBrowser::detail {
DirectoryEntry syntheticParentEntry(const QString& path, const PlaceList::IconMap* places,
                                    const QByteArray& nativeParent) {
  DirectoryEntry parent;
  parent.name = QStringLiteral("..");
  parent.absolute_path = QDir::cleanPath(QFileInfo(path).absolutePath());
  parent.native_path = nativeParent.isEmpty() ? QFile::encodeName(parent.absolute_path) : nativeParent;
  parent.is_dir = true;
  parent.is_parent = true;
  parent.mode = S_IFDIR | 0755;
  parent.icon_name =
      IconNameResolver::candidateIconNames(parent.mode, parent.name, namedIconFor(places, parent.absolute_path))
          .join(IconNameResolver::kChainSeparator);
  return parent;
}

void walkDirectory(const QString& path, const std::shared_ptr<std::atomic_bool>& cancel,
                   const std::function<void()>& beforeOpen, int readErrorAfter,
                   const std::shared_ptr<const PlaceList::IconMap>& places, const PublishBatch& publish,
                   const QByteArray& nativePath) {
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
  auto nativeDirectory = nativePath.isEmpty() ? QFile::encodeName(path) : nativePath;
  while (nativeDirectory.size() > 1 && nativeDirectory.endsWith('/')) {
    nativeDirectory.chop(1);
  }
  const std::unique_ptr<DIR, DirectoryCloser> directory(::opendir(nativeDirectory.constData()));
  if (!directory) {
    const int error = errno;
    flush(true, QCoreApplication::translate("DirectoryModel", "Cannot open folder: %1")
                    .arg(QString::fromLocal8Bit(std::strerror(error))));
    return;
  }
  if (!QDir(path).isRoot()) {
    buffer.append(readParentEntry(path, places.get(), nativeDirectory));
  }
  int readCount = 0;
  while (!cancel->load()) {
    auto* item = readNext(directory.get(), readCount, readErrorAfter);
    if (item == nullptr) {
      const int error = errno;
      flush(true, error == 0 ? QString{}
                             : QCoreApplication::translate("DirectoryModel", "Cannot read folder: %1")
                                   .arg(QString::fromLocal8Bit(std::strerror(error))));
      return;
    }
    const auto name = QFile::decodeName(static_cast<const char*>(item->d_name));
    if (name == u"." || name == u"..") {
      continue;
    }
    ++readCount;
    const auto nativeEntry =
        (nativeDirectory == "/" ? nativeDirectory : nativeDirectory + '/') + static_cast<const char*>(item->d_name);
    buffer.append(readEntry(path, name, places.get(), nativeEntry));
    if (buffer.size() >= kBatchEntryThreshold || sinceFlush.elapsed() >= kBatchTimeThresholdMs) {
      flush(false);
    }
  }
  flush(true);
}
}  // namespace HolonightFileBrowser::detail
