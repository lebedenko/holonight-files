#include "file_operation_service.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QUuid>

#include <array>
#include <cerrno>
#include <climits>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <iterator>
#include <sys/stat.h>
#include <unistd.h>
#include <utility>
#include <vector>

namespace FileOperationService {
namespace {

struct DirectoryCloser {
  void operator()(DIR* directory) const { ::closedir(directory); }
};

constexpr qint64 kChunkSize = 1 << 20;  // 1 MiB, streamed — never full-buffer (REQ-NF-005)

QByteArray encode(const QString& path) { return QFile::encodeName(path); }

// Best-effort recursive removal: keeps going past an individual failure so as much as possible is
// cleaned up, but remembers the first errno encountered.
bool removeDirectoryRecursivelyImpl(const QString& path, int* firstErrno) {
  const auto encoded = encode(path);
  const std::unique_ptr<DIR, DirectoryCloser> dir(::opendir(encoded.constData()));
  if (!dir) {
    if (*firstErrno == 0) {
      *firstErrno = errno;
    }
    return false;
  }
  bool succeeded = true;
  while (auto* item = ::readdir(dir.get())) {
    const auto name = QFile::decodeName(static_cast<const char*>(item->d_name));
    if (name == u"." || name == u"..") {
      continue;
    }
    const auto childPath = QDir(path).filePath(name);
    struct stat childStat{};
    if (::lstat(encode(childPath).constData(), &childStat) != 0) {
      if (*firstErrno == 0) {
        *firstErrno = errno;
      }
      succeeded = false;
      continue;
    }
    if (S_ISDIR(childStat.st_mode)) {
      succeeded = removeDirectoryRecursivelyImpl(childPath, firstErrno) && succeeded;
    } else if (::unlink(encode(childPath).constData()) != 0) {
      if (*firstErrno == 0) {
        *firstErrno = errno;
      }
      succeeded = false;
    }
  }
  if (::rmdir(encoded.constData()) != 0) {
    if (*firstErrno == 0) {
      *firstErrno = errno;
    }
    succeeded = false;
  }
  return succeeded;
}

ItemResult failure(const QString& reason) {
  ItemResult result;
  result.failed = true;
  result.reason = reason;
  return result;
}

// Resolve the parent, not the final entry: destination symlinks are entries to replace.
QString resolvedEntryPath(const QString& path) {
  const QFileInfo info(path);
  const auto parent = QFileInfo(info.absolutePath()).canonicalFilePath();
  return parent.isEmpty() ? QString() : QDir(parent).filePath(info.fileName());
}

bool destinationWithinSource(const QString& srcPath, const QString& destPath) {
  const auto canonicalSource = QFileInfo(srcPath).canonicalFilePath();
  // Walk upwards so a not-yet-existing destination still resolves symlinked ancestors.
  QString ancestor = QFileInfo(destPath).absolutePath();
  while (!ancestor.isEmpty()) {
    const auto canonical = QFileInfo(ancestor).canonicalFilePath();
    if (!canonical.isEmpty()) {
      if (canonical == canonicalSource ||
          canonical.startsWith(canonicalSource.endsWith(u'/') ? canonicalSource : canonicalSource + u'/')) {
        return true;
      }
      break;
    }
    const auto parent = QFileInfo(ancestor).absolutePath();
    if (parent == ancestor) {
      break;
    }
    ancestor = parent;
  }
  return false;
}

ItemResult validateEndpoints(const QString& srcPath, const QString& destPath, bool overwrite) {
  struct stat source{};
  if (::lstat(encode(srcPath).constData(), &source) != 0) {
    return failure(describeErrno(errno));
  }
  struct stat destination{};
  const bool exists = ::lstat(encode(destPath).constData(), &destination) == 0;
  const auto sourcePath = resolvedEntryPath(srcPath);
  const auto destinationPath = resolvedEntryPath(destPath);
  if ((!sourcePath.isEmpty() && sourcePath == destinationPath) ||
      (exists && source.st_dev == destination.st_dev && source.st_ino == destination.st_ino)) {
    return failure(QObject::tr("Source and destination are the same entry"));
  }
  if (S_ISDIR(source.st_mode) && destinationWithinSource(srcPath, destPath)) {
    return failure(QObject::tr("Destination is inside the source directory"));
  }
  if (exists && !overwrite) {
    return failure(describeErrno(EEXIST));
  }
  if (exists && (S_ISDIR(source.st_mode) != S_ISDIR(destination.st_mode))) {
    return failure(QObject::tr("Cannot replace incompatible directory entries"));
  }
  return {};
}

QString temporarySibling(const QString& destination) {
  return QDir(QFileInfo(destination).absolutePath())
      .filePath(QStringLiteral(".holonight-transfer-%1").arg(QUuid::createUuid().toString(QUuid::WithoutBraces)));
}

ItemResult commitTemporary(const QString& temporary, const QString& destination, bool overwrite,
                           const CancelFlag& cancel) {
  ItemResult result;
  if (cancel->load()) {
    result.cancelled = true;
  } else if (::renameat2(AT_FDCWD, encode(temporary).constData(), AT_FDCWD, encode(destination).constData(),
                         overwrite ? 0 : RENAME_NOREPLACE) != 0) {
    result = failure(describeErrno(errno));
  }
  if (!result.complete()) {
    ::unlink(encode(temporary).constData());
  } else {
    result.destinationCopiesExist = true;
  }
  return result;
}

ItemResult copySymlink(const QString& srcPath, const QString& destPath, bool overwrite, const CancelFlag& cancel) {
  std::vector<char> buffer(PATH_MAX);
  const ssize_t length = ::readlink(encode(srcPath).constData(), buffer.data(), buffer.size());
  if (length < 0) {
    return failure(describeErrno(errno));
  }
  if (static_cast<size_t>(length) == buffer.size()) {
    return failure(QObject::tr("Symbolic link target is too long"));
  }
  const auto temporary = temporarySibling(destPath);
  if (::symlink(QByteArray(buffer.data(), length).constData(), encode(temporary).constData()) != 0) {
    return failure(describeErrno(errno));
  }
  return commitTemporary(temporary, destPath, overwrite, cancel);
}

ItemResult writeChunk(int destFd, const std::vector<char>& buffer, ssize_t bytesRead, const CancelFlag& cancel) {
  ItemResult result;
  ssize_t written = 0;
  while (written < bytesRead) {
    if (cancel->load()) {
      result.cancelled = true;
      break;
    }
    const ssize_t chunk = ::write(destFd, std::next(buffer.data(), written), static_cast<size_t>(bytesRead - written));
    if (chunk <= 0) {
      if (chunk < 0 && errno == EINTR) {
        continue;
      }
      result = failure(describeErrno(chunk == 0 ? EIO : errno));
      break;
    }
    written += chunk;
  }
  return result;
}

ItemResult streamFile(int srcFd, int destFd, const CancelFlag& cancel) {
  ItemResult result;
  std::vector<char> buffer(static_cast<size_t>(kChunkSize));
  while (result.complete()) {
    if (cancel->load()) {
      result.cancelled = true;
      break;
    }
    const ssize_t bytesRead = ::read(srcFd, buffer.data(), buffer.size());
    if (bytesRead < 0) {
      if (errno == EINTR) {
        continue;
      }
      result = failure(describeErrno(errno));
      break;
    }
    if (bytesRead == 0) {
      break;
    }
    result = writeChunk(destFd, buffer, bytesRead, cancel);
  }
  return result;
}

ItemResult copyRegularFile(const QString& srcPath, const QString& destPath, const struct stat& srcStat, bool overwrite,
                           const CancelFlag& cancel) {
  // Nonblocking + nofollow prevents a replaced FIFO or symlink from making this open hang/follow.
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int srcFd = ::open(encode(srcPath).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW | O_NONBLOCK);
  if (srcFd < 0) {
    return failure(describeErrno(errno));
  }
  struct stat opened{};
  if (::fstat(srcFd, &opened) != 0 || !S_ISREG(opened.st_mode) || opened.st_dev != srcStat.st_dev ||
      opened.st_ino != srcStat.st_ino) {
    ::close(srcFd);
    return failure(QObject::tr("Source changed before copying"));
  }
  const auto temporary = temporarySibling(destPath);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int destFd = ::open(encode(temporary).constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  if (destFd < 0) {
    const int saved = errno;
    ::close(srcFd);
    return failure(describeErrno(saved));
  }
  auto result = streamFile(srcFd, destFd, cancel);
  ::close(srcFd);
  if (result.complete()) {
    const std::array<timespec, 2> times{{srcStat.st_atim, srcStat.st_mtim}};
    if (::futimens(destFd, times.data()) != 0 || ::fchmod(destFd, srcStat.st_mode & 07777) != 0 ||
        ::fsync(destFd) != 0) {
      result = failure(describeErrno(errno));
    }
  }
  if (::close(destFd) != 0 && result.complete()) {
    result = failure(describeErrno(errno));
  }
  if (!result.complete()) {
    ::unlink(encode(temporary).constData());
    return result;
  }
  return commitTemporary(temporary, destPath, overwrite, cancel);
}

ItemResult copyEntryImpl(const QString& srcPath, const QString& destPath, bool overwrite, const CancelFlag& cancel);

// Ensures destPath exists as a directory ready to receive srcStat's children: an existing
// directory is left alone (merge, REQ-F-048's nested-collision auto-skip does the rest).
// Fresh directories stay writable until their children and metadata have been copied.
ItemResult prepareDestinationDirectory(const QString& destPath, bool overwrite) {
  struct stat destination{};
  if (::lstat(encode(destPath).constData(), &destination) == 0) {
    if (overwrite && S_ISDIR(destination.st_mode)) {
      return {};
    }
    return failure(describeErrno(EEXIST));
  }
  if (::mkdir(encode(destPath).constData(), 0700) != 0) {
    return failure(describeErrno(errno));
  }
  ItemResult result;
  result.destinationCopiesExist = true;
  return result;
}

ItemResult copyDirectoryTree(const QString& srcPath, const QString& destPath, const struct stat& srcStat,
                             bool overwrite, const CancelFlag& cancel) {
  auto result = prepareDestinationDirectory(destPath, overwrite);
  const bool created = result.destinationCopiesExist;
  if (result.failed) {
    return result;
  }
  const std::unique_ptr<DIR, DirectoryCloser> dir(::opendir(encode(srcPath).constData()));
  if (!dir) {
    result.failed = true;
    result.reason = describeErrno(errno);
    return result;
  }
  while (true) {
    errno = 0;
    auto* item = ::readdir(dir.get());
    if (item == nullptr) {
      if (errno != 0) {
        result.nestedFailures.append({.path = srcPath, .reason = describeErrno(errno)});
      }
      break;
    }
    if (cancel->load()) {
      result.cancelled = true;
      return result;
    }
    const auto name = QFile::decodeName(static_cast<const char*>(item->d_name));
    if (name == u"." || name == u"..") {
      continue;
    }
    if (destinationExists(destPath, name)) {
      result.skippedChildren.append(QDir(srcPath).filePath(name));
      continue;  // Nested skips are distinct from I/O failures.
    }
    const auto childSrc = QDir(srcPath).filePath(name);
    const auto childDest = QDir(destPath).filePath(name);
    const auto childResult = copyEntryImpl(childSrc, childDest, /*overwrite=*/false, cancel);
    result.destinationCopiesExist = result.destinationCopiesExist || childResult.destinationCopiesExist;
    result.skippedChildren.append(childResult.skippedChildren);
    result.nestedFailures.append(childResult.nestedFailures);
    if (childResult.cancelled) {
      result.cancelled = true;
      return result;
    }
    if (childResult.failed) {
      result.nestedFailures.append({.path = childSrc, .reason = childResult.reason});
    }
  }
  if (created) {
    const std::array<timespec, 2> times{{srcStat.st_atim, srcStat.st_mtim}};
    if (::utimensat(AT_FDCWD, encode(destPath).constData(), times.data(), AT_SYMLINK_NOFOLLOW) != 0 ||
        ::chmod(encode(destPath).constData(), srcStat.st_mode & 07777) != 0) {
      result.nestedFailures.append({.path = destPath, .reason = describeErrno(errno)});
    }
  }
  return result;
}

ItemResult copyEntryImpl(const QString& srcPath, const QString& destPath, bool overwrite, const CancelFlag& cancel) {
  ItemResult result;
  if (cancel->load()) {
    result.cancelled = true;
    return result;
  }
  struct stat srcStat{};
  if (::lstat(encode(srcPath).constData(), &srcStat) != 0) {
    result.failed = true;
    result.reason = describeErrno(errno);
    return result;
  }
  if (S_ISLNK(srcStat.st_mode)) {
    return copySymlink(srcPath, destPath, overwrite, cancel);
  }
  if (S_ISDIR(srcStat.st_mode)) {
    return copyDirectoryTree(srcPath, destPath, srcStat, overwrite, cancel);
  }
  if (!S_ISREG(srcStat.st_mode)) {
    return failure(QObject::tr("Unsupported file type for copying"));
  }
  return copyRegularFile(srcPath, destPath, srcStat, overwrite, cancel);
}

}  // namespace

QString describeErrno(int err) {
  switch (err) {
    case EACCES:
    case EPERM:
      return QObject::tr("Permission denied");
    case ENOSPC:
      return QObject::tr("No space left on device");
    case EIO:
      return QObject::tr("I/O error");
    case ENOENT:
      return QObject::tr("No such file or directory");
    case EEXIST:
      return QObject::tr("File exists");
    case ENOTEMPTY:
      return QObject::tr("Directory not empty");
    case EROFS:
      return QObject::tr("Read-only file system");
    case EDQUOT:
      return QObject::tr("Disk quota exceeded");
    case 0:
      return QObject::tr("Unknown error");
    default:
      return QString::fromLocal8Bit(std::strerror(err));
  }
}

bool destinationExists(const QString& destDir, const QString& itemName) {
  struct stat entryStat{};
  return ::lstat(encode(QDir(destDir).filePath(itemName)).constData(), &entryStat) == 0;
}

QString autoRenameCandidate(const QString& destDir, const QString& itemName) {
  const auto dot = itemName.lastIndexOf(u'.');
  const auto base = dot > 0 ? itemName.left(dot) : itemName;
  const auto ext = dot > 0 ? itemName.mid(dot) : QString();
  for (int attempt = 2;; ++attempt) {
    const auto candidate = QStringLiteral("%1 (%2)%3").arg(base).arg(attempt).arg(ext);
    if (!destinationExists(destDir, candidate)) {
      return candidate;
    }
  }
}

ItemResult copyEntry(const QString& srcPath, const QString& destPath, bool overwrite, const CancelFlag& cancel) {
  if (cancel->load()) {
    ItemResult result;
    result.cancelled = true;
    return result;
  }
  auto validation = validateEndpoints(srcPath, destPath, overwrite);
  if (validation.failed) {
    return validation;
  }
  return copyEntryImpl(srcPath, destPath, overwrite, cancel);
}

namespace {
ItemResult removeCompletedSource(const QString& path) {
  ItemResult result;
  struct stat entryStat{};
  if (::lstat(encode(path).constData(), &entryStat) != 0) {
    result.failed = true;
    result.reason = describeErrno(errno);
    return result;
  }
  if (S_ISDIR(entryStat.st_mode)) {
    int firstErrno = 0;
    if (!removeDirectoryRecursivelyImpl(path, &firstErrno)) {
      result.failed = true;
      result.reason = describeErrno(firstErrno);
    }
    return result;
  }
  if (::unlink(encode(path).constData()) != 0) {
    result.failed = true;
    result.reason = describeErrno(errno);
  }
  return result;
}

}  // namespace

ItemResult moveEntry(const QString& srcPath, const QString& destPath, bool overwrite, const CancelFlag& cancel) {
  ItemResult result;
  if (cancel->load()) {
    result.cancelled = true;
    return result;
  }
  auto validation = validateEndpoints(srcPath, destPath, overwrite);
  if (validation.failed) {
    return validation;
  }
  struct stat srcStat{};
  if (::lstat(encode(srcPath).constData(), &srcStat) != 0) {
    return failure(describeErrno(errno));
  }
  struct stat destStat{};
  const bool destExists = ::lstat(encode(destPath).constData(), &destStat) == 0;
  const bool needsMerge = overwrite && destExists && S_ISDIR(srcStat.st_mode) && S_ISDIR(destStat.st_mode);
  if (!needsMerge) {
    if (cancel->load()) {
      result.cancelled = true;
      return result;
    }
    if (::renameat2(AT_FDCWD, encode(srcPath).constData(), AT_FDCWD, encode(destPath).constData(),
                    overwrite ? 0 : RENAME_NOREPLACE) == 0) {
      return result;
    }
    if (errno != EXDEV) {
      return failure(describeErrno(errno));
    }
  }
  result = copyEntry(srcPath, destPath, overwrite, cancel);
  if (cancel->load()) {
    result.cancelled = true;
  }
  if (!result.complete()) {
    result.sourceRetained = true;
    const auto retention = result.destinationCopiesExist
                               ? QObject::tr("source retained; some destination copies exist")
                               : QObject::tr("source retained; no destination copies were committed");
    result.reason = QStringLiteral("%1; %2").arg(
        result.reason.isEmpty() ? QObject::tr("Transfer incomplete") : result.reason, retention);
    return result;
  }
  const auto removed = removeCompletedSource(srcPath);
  if (removed.failed) {
    result.failed = true;
    result.reason = QObject::tr("Moved (via copy); source deletion failed: %1").arg(removed.reason);
  }
  return result;
}

}  // namespace FileOperationService
