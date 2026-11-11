#include "trash_service.h"

#include "file_operation_service.h"
#include "native_operation_path.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QUrl>

#include <cerrno>
#include <chrono>
#include <fcntl.h>
#include <linux/magic.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <thread>
#include <unistd.h>

namespace TrashService {
namespace {

QByteArray encode(const QString& path) { return operationBytes(path); }

QString xdgDataHome() {
  const auto env = qEnvironmentVariable("XDG_DATA_HOME");
  return env.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share") : env;
}

bool isPrivateTrashDirectory(const struct stat& info) {
  return S_ISDIR(info.st_mode) && info.st_uid == ::getuid() && (info.st_mode & 05777) == 0700;
}

std::optional<TrashError> ensureDir0700(const QString& path) {
  const auto encoded = encode(path);
  if (::mkdir(encoded.constData(), 0700) != 0 && errno != EEXIST) {
    return TrashError{
        .kind = FailureKind::DirectoryCreation,
        .path = path,
        .reason = FileOperationService::describeErrno(errno),
    };
  }
  struct stat info{};
  if (::lstat(encoded.constData(), &info) != 0) {
    return TrashError{
        .kind = FailureKind::Validation,
        .path = path,
        .reason = FileOperationService::describeErrno(errno),
    };
  }
  if (!isPrivateTrashDirectory(info)) {
    return TrashError{
        .kind = FailureKind::Validation,
        .path = path,
        .reason = QObject::tr("Trash must be a real directory owned by this user with mode 0700 or 2700"),
    };
  }
  return std::nullopt;
}

bool ensureFilesAndInfo(const QString& dirPath, TrashDirectory* out) {
  const auto files = QDir(dirPath).filePath(QStringLiteral("files"));
  const auto info = QDir(dirPath).filePath(QStringLiteral("info"));
  for (const auto& path : {dirPath, files, info}) {
    if (auto error = ensureDir0700(path)) {
      out->error = std::move(error);
      return false;
    }
  }
  out->files_dir = files;
  out->info_dir = info;
  out->error.reset();
  return true;
}

// Walks up path's ancestors, comparing st_dev, until it changes; the last ancestor still on
// device is the mount point (REQ-F-041's "$topdir").
QString findTopdir(const QString& path, dev_t device) {
  const QFileInfo info(path);
  QString current = canonicalOperationPath(info.absolutePath());
  QString last = current;
  while (true) {
    struct stat ancestorStat{};
    if (::stat(encode(current).constData(), &ancestorStat) != 0 || ancestorStat.st_dev != device) {
      break;
    }
    last = current;
    QDir dir(current);
    if (!dir.cdUp()) {
      break;
    }
    const auto parent = dir.absolutePath();
    if (parent == current) {
      break;
    }
    current = parent;
  }
  return last;
}

// REQ-F-042: $topdir/.Trash qualifies only if it exists, is not a symlink, and has the sticky bit
// set — a disqualified .Trash is rejected outright, never used.
bool topLevelTrashQualifies(const QString& topdirTrash) {
  struct stat info{};
  if (::lstat(encode(topdirTrash).constData(), &info) != 0) {
    return false;
  }
  return !S_ISLNK(info.st_mode) && S_ISDIR(info.st_mode) && (info.st_mode & S_ISVTX) != 0;
}

}  // namespace

TrashDirectory selectTrashDir(const QString& path) {
  TrashDirectory result;
  struct stat pathStat{};
  if (::lstat(encode(path).constData(), &pathStat) != 0) {
    result.error = TrashError{
        .kind = FailureKind::SourceLookup,
        .path = path,
        .reason = FileOperationService::describeErrno(errno),
    };
    return result;
  }
  const auto home = xdgDataHome();
  if (!QDir().mkpath(home)) {
    result.error = TrashError{
        .kind = FailureKind::DirectoryCreation,
        .path = home,
        .reason = FileOperationService::describeErrno(errno),
    };
    return result;
  }
  struct stat homeStat{};
  if (::stat(encode(home).constData(), &homeStat) != 0) {
    result.error =
        TrashError{.kind = FailureKind::Validation, .path = home, .reason = FileOperationService::describeErrno(errno)};
    return result;
  }
  if (homeStat.st_dev == pathStat.st_dev) {
    ensureFilesAndInfo(QDir(home).filePath(QStringLiteral("Trash")), &result);  // REQ-F-041
    return result;
  }

  const auto topdir = findTopdir(path, pathStat.st_dev);
  const auto topLevelTrash = QDir(topdir).filePath(QStringLiteral(".Trash"));
  if (topLevelTrashQualifies(topLevelTrash)) {
    const auto uidDir = QDir(topLevelTrash).filePath(QString::number(::getuid()));
    if (ensureFilesAndInfo(uidDir, &result)) {  // REQ-F-042
      result.use_relative_path = true;
      result.topdir = topdir;
      return result;
    }
    result = {};  // creation under a qualifying .Trash still failed — fall through to .Trash-$uid
  }

  const auto fallbackDir = QDir(topdir).filePath(QStringLiteral(".Trash-") + QString::number(::getuid()));
  if (ensureFilesAndInfo(fallbackDir, &result)) {  // REQ-F-043
    result.use_relative_path = true;
    result.topdir = topdir;
    return result;
  }
  return result;  // Both validated locations failed; leave the source untouched.
}

namespace {
QString trashNameCandidate(const QString& name, unsigned int attempt) {
  if (attempt == 1) {
    return name;
  }
  const auto dot = name.lastIndexOf(u'.');
  const auto base = dot > 0 ? name.left(dot) : name;
  const auto ext = dot > 0 ? name.mid(dot) : QString();
  return QStringLiteral("%1_%2%3").arg(base).arg(attempt).arg(ext);
}
}  // namespace

QString uniqueTrashName(const TrashDirectory& dir, const QString& itemName) {
  for (unsigned int attempt = 1;; ++attempt) {
    const auto candidate = trashNameCandidate(itemName, attempt);
    if (!FileOperationService::destinationExists(dir.files_dir, candidate) &&
        !FileOperationService::destinationExists(dir.info_dir, candidate + QStringLiteral(".trashinfo"))) {
      return candidate;
    }
  }
}

namespace {
std::optional<TrashError> reserveTrashInfo(const TrashDirectory& dir, int infoFd, const QString& trashName,
                                           const QString& originalPath, bool* collision) {
  const auto filename = trashName + QStringLiteral(".trashinfo");
  const auto infoPath = QDir(dir.info_dir).filePath(filename);
  const auto name = encode(infoFd == AT_FDCWD ? infoPath : filename);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
  const int descriptor = ::openat(infoFd, name.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
  QFile file;
  if (descriptor < 0) {
    const int error = errno;
    *collision = error == EEXIST;
    return TrashError{
        .kind = FailureKind::Metadata,
        .path = infoPath,
        .reason = FileOperationService::describeErrno(error),
    };
  }
  if (!file.open(descriptor, QIODevice::WriteOnly, QFileDevice::AutoCloseHandle)) {
    ::close(descriptor);
    ::unlinkat(infoFd, name.constData(), 0);
    return TrashError{.kind = FailureKind::Metadata, .path = infoPath, .reason = file.errorString()};
  }
  const auto pathField = dir.use_relative_path ? QDir(dir.topdir).relativeFilePath(originalPath) : originalPath;
  const auto encoded = operationBytes(pathField).toPercentEncoding("/");
  const auto deletionDate = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
  const auto contents =
      QStringLiteral("[Trash Info]\nPath=%1\nDeletionDate=%2\n").arg(QString::fromUtf8(encoded), deletionDate).toUtf8();
  if (file.write(contents) != contents.size() || !file.flush() || ::fsync(file.handle()) != 0) {
    const auto reason =
        file.error() == QFileDevice::NoError ? FileOperationService::describeErrno(errno) : file.errorString();
    file.close();
    ::unlinkat(infoFd, name.constData(), 0);
    return TrashError{.kind = FailureKind::Metadata, .path = infoPath, .reason = reason};
  }
  file.close();
  return std::nullopt;
}
}  // namespace

std::optional<TrashError> writeTrashInfo(const TrashDirectory& dir, const QString& trashName,
                                         const QString& originalPath) {
  bool collision = false;
  return reserveTrashInfo(dir, AT_FDCWD, trashName, originalPath, &collision);
}

void removeTrashInfo(const TrashDirectory& dir, const QString& trashName) {
  ::unlink(encode(QDir(dir.info_dir).filePath(trashName + QStringLiteral(".trashinfo"))).constData());
}

namespace {
TrashResult failedTrash(const TrashError& error) {
  QString kind;
  switch (error.kind) {
    case FailureKind::SourceLookup:
      kind = QObject::tr("Source lookup");
      break;
    case FailureKind::DirectoryCreation:
      kind = QObject::tr("Trash directory creation");
      break;
    case FailureKind::Validation:
      kind = QObject::tr("Trash validation");
      break;
    case FailureKind::Metadata:
      kind = QObject::tr("Trash metadata");
      break;
    case FailureKind::Move:
      kind = QObject::tr("Trash move");
      break;
  }
  TrashResult result;
  result.failed = true;
  result.error = error;
  result.reason = QObject::tr("%1 failed at %2: %3").arg(kind, error.path, error.reason);
  return result;
}

class Descriptor {
 public:
  Descriptor(int parent, const char* name, int flags, mode_t mode = 0)
      // NOLINTNEXTLINE(cppcoreguidelines-pro-type-vararg)
      : value_(::openat(parent, name, flags, mode)) {}
  ~Descriptor() {
    if (value_ >= 0) {
      ::close(value_);
    }
  }
  Descriptor(const Descriptor&) = delete;
  Descriptor& operator=(const Descriptor&) = delete;
  Descriptor(Descriptor&&) = delete;
  Descriptor& operator=(Descriptor&&) = delete;
  [[nodiscard]] int get() const { return value_; }

 private:
  int value_;
};

std::optional<TrashError> validateDirectory(int descriptor, const QString& path) {
  struct stat info{};
  if (descriptor < 0 || ::fstat(descriptor, &info) != 0) {
    return TrashError{
        .kind = FailureKind::Validation,
        .path = path,
        .reason = FileOperationService::describeErrno(errno),
    };
  }
  if (!isPrivateTrashDirectory(info)) {
    return TrashError{
        .kind = FailureKind::Validation,
        .path = path,
        .reason = QObject::tr("Trash must be a real directory owned by this user with mode 0700 or 2700"),
    };
  }
  return std::nullopt;
}

bool lockTrash(int descriptor, const FileOperationService::CancelFlag& cancel) {
  struct stat info{};
  if (::fstat(descriptor, &info) != 0 || !S_ISREG(info.st_mode) || info.st_uid != ::getuid()) {
    return false;
  }
  while (!cancel->load()) {
    if (::flock(descriptor, LOCK_EX | LOCK_NB) == 0) {
      return true;
    }
    if (errno != EWOULDBLOCK && errno != EINTR) {
      return false;
    }
    std::this_thread::sleep_for(std::chrono::milliseconds(20));
  }
  return false;
}

struct MoveOutcome {
  enum class Status : std::uint8_t { Moved, Collision, Failed, Uncertain };
  Status status = Status::Failed;
  int error = 0;
};

MoveOutcome verifyNfsMove(const QByteArray& source, const struct stat& before, int filesFd, const QByteArray& payload,
                          int error) {
  // The server may have completed the rename even if the client received an error.
  const auto sameItem = [&before](const struct statx& current) {
    return current.stx_dev_major == major(before.st_dev) && current.stx_dev_minor == minor(before.st_dev) &&
           current.stx_ino == before.st_ino;
  };
  constexpr int flags = AT_SYMLINK_NOFOLLOW | AT_STATX_FORCE_SYNC;
  struct statx currentSource{};
  const int sourceStatus = ::statx(AT_FDCWD, source.constData(), flags, STATX_INO, &currentSource);
  const bool sourceGone = sourceStatus != 0 && errno == ENOENT;
  struct statx currentPayload{};
  const int payloadStatus = ::statx(filesFd, payload.constData(), flags, STATX_INO, &currentPayload);
  const bool payloadGone = payloadStatus != 0 && errno == ENOENT;
  if (sourceGone && payloadStatus == 0 && sameItem(currentPayload)) {
    return {.status = MoveOutcome::Status::Moved};
  }
  return {
      .status = sourceStatus == 0 && sameItem(currentSource) && payloadGone ? MoveOutcome::Status::Failed
                                                                            : MoveOutcome::Status::Uncertain,
      .error = error,
  };
}

MoveOutcome movePayload(const QByteArray& source, const struct stat& before, int filesFd, const QByteArray& payload,
                        bool isNfs) {
  using Status = MoveOutcome::Status;
  if (::renameat2(AT_FDCWD, source.constData(), filesFd, payload.constData(), RENAME_NOREPLACE) == 0) {
    return {.status = Status::Moved};
  }
  const int error = errno;
  if (error == EEXIST) {
    return {.status = Status::Collision};
  }
  if (!isNfs) {
    return {.status = Status::Failed, .error = error};
  }
  if (error != EINVAL && error != EOPNOTSUPP) {
    return verifyNfsMove(source, before, filesFd, payload, error);
  }

  // The sidecar reservation gives cooperating Trash writers exclusive use of this name.
  struct stat existing{};
  if (::fstatat(filesFd, payload.constData(), &existing, AT_SYMLINK_NOFOLLOW) == 0) {
    return {.status = Status::Collision};
  }
  if (errno != ENOENT) {
    return {.status = Status::Failed, .error = errno};
  }
  if (::renameat(AT_FDCWD, source.constData(), filesFd, payload.constData()) == 0) {
    return {.status = Status::Moved};
  }
  return verifyNfsMove(source, before, filesFd, payload, errno);
}

TrashResult moveToReservedTrash(const QString& source, const TrashDirectory& directory, int filesFd, int infoFd,
                                const FileOperationService::CancelFlag& cancel) {
  struct statfs filesystem{};
  const bool isNfs = ::fstatfs(filesFd, &filesystem) == 0 && filesystem.f_type == NFS_SUPER_MAGIC;
  const QFileInfo sourceInfo(source);
  const auto absoluteSource = QDir(sourceInfo.absolutePath()).filePath(sourceInfo.fileName());
  const auto sourceBytes = encode(absoluteSource);
  struct stat before{};
  if (::lstat(sourceBytes.constData(), &before) != 0) {
    return failedTrash(
        {.kind = FailureKind::SourceLookup, .path = source, .reason = FileOperationService::describeErrno(errno)});
  }

  for (unsigned int attempt = 1; !cancel->load(); ++attempt) {
    const auto name = trashNameCandidate(sourceInfo.fileName(), attempt);
    bool collision = false;
    if (const auto error = reserveTrashInfo(directory, infoFd, name, absoluteSource, &collision)) {
      if (collision) {
        continue;
      }
      return failedTrash(*error);
    }
    const auto sidecar = encode(name + QStringLiteral(".trashinfo"));
    if (cancel->load()) {
      ::unlinkat(infoFd, sidecar.constData(), 0);
      break;
    }
    using Status = MoveOutcome::Status;
    const auto outcome = movePayload(sourceBytes, before, filesFd, encode(name), isNfs);
    if (outcome.status == Status::Moved) {
      return {};
    }
    if (outcome.status != Status::Uncertain) {
      ::unlinkat(infoFd, sidecar.constData(), 0);
    }
    if (outcome.status == Status::Collision) {
      continue;
    }
    auto reason = FileOperationService::describeErrno(outcome.error);
    if (outcome.status == Status::Uncertain) {
      reason = QObject::tr("%1. The move outcome could not be confirmed. Trash metadata was kept.").arg(reason);
    }
    return failedTrash({.kind = FailureKind::Move, .path = QDir(directory.files_dir).filePath(name), .reason = reason});
  }
  TrashResult result;
  result.cancelled = true;
  return result;
}
}  // namespace

TrashResult trashEntry(const QString& source, const FileOperationService::CancelFlag& cancel) {
  if (cancel->load()) {
    TrashResult result;
    result.cancelled = true;
    return result;
  }
  const auto directory = selectTrashDir(source);
  if (directory.error) {
    return failedTrash(*directory.error);
  }
  const auto root = QFileInfo(directory.files_dir).absolutePath();
  constexpr int directoryFlags = O_RDONLY | O_DIRECTORY | O_NOFOLLOW | O_CLOEXEC;
  const Descriptor rootFd(AT_FDCWD, encode(root).constData(), directoryFlags);
  if (const auto error = validateDirectory(rootFd.get(), root)) {
    return failedTrash(*error);
  }
  const Descriptor filesFd(rootFd.get(), "files", directoryFlags);
  if (const auto error = validateDirectory(filesFd.get(), directory.files_dir)) {
    return failedTrash(*error);
  }
  const Descriptor infoFd(rootFd.get(), "info", directoryFlags);
  if (const auto error = validateDirectory(infoFd.get(), directory.info_dir)) {
    return failedTrash(*error);
  }

  // Keep this lock protocol compatible with holonightd's Trash library.
  const Descriptor lockFd(rootFd.get(), ".holonight.lock", O_RDWR | O_CREAT | O_NOFOLLOW | O_NONBLOCK | O_CLOEXEC,
                          0600);
  if (!lockTrash(lockFd.get(), cancel)) {
    if (cancel->load()) {
      TrashResult result;
      result.cancelled = true;
      return result;
    }
    return failedTrash({.kind = FailureKind::Validation, .path = root, .reason = QObject::tr("Cannot lock Trash")});
  }
  return moveToReservedTrash(source, directory, filesFd.get(), infoFd.get(), cancel);
}

}  // namespace TrashService
