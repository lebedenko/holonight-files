#include "trash_service.h"

#include "file_operation_service.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QObject>
#include <QUrl>

#include <cerrno>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

namespace TrashService {
namespace {

QByteArray encode(const QString& path) { return QFile::encodeName(path); }

QString xdgDataHome() {
  const auto env = qEnvironmentVariable("XDG_DATA_HOME");
  return env.isEmpty() ? QDir::homePath() + QStringLiteral("/.local/share") : env;
}

std::optional<TrashError> ensureDir0700(const QString& path) {
  const auto encoded = encode(path);
  if (::mkdir(encoded.constData(), 0700) != 0 && errno != EEXIST) {
    return TrashError{
        .kind = FailureKind::DirectoryCreation, .path = path, .reason = FileOperationService::describeErrno(errno)};
  }
  struct stat info{};
  if (::lstat(encoded.constData(), &info) != 0) {
    return TrashError{
        .kind = FailureKind::Validation, .path = path, .reason = FileOperationService::describeErrno(errno)};
  }
  if (!S_ISDIR(info.st_mode) || info.st_uid != ::getuid() || (info.st_mode & 07777) != 0700) {
    return TrashError{.kind = FailureKind::Validation,
                      .path = path,
                      .reason = QObject::tr("Trash must be a real directory owned by this user with mode 0700")};
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
  out->filesDir = files;
  out->infoDir = info;
  out->error.reset();
  return true;
}

// Walks up path's ancestors, comparing st_dev, until it changes; the last ancestor still on
// device is the mount point (REQ-F-041's "$topdir").
QString findTopdir(const QString& path, dev_t device) {
  const QFileInfo info(path);
  QString current = QFileInfo(info.absolutePath()).canonicalFilePath();
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
        .kind = FailureKind::SourceLookup, .path = path, .reason = FileOperationService::describeErrno(errno)};
    return result;
  }
  const auto home = xdgDataHome();
  if (!QDir().mkpath(home)) {
    result.error = TrashError{
        .kind = FailureKind::DirectoryCreation, .path = home, .reason = FileOperationService::describeErrno(errno)};
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
      result.useRelativePath = true;
      result.topdir = topdir;
      return result;
    }
    result = {};  // creation under a qualifying .Trash still failed — fall through to .Trash-$uid
  }

  const auto fallbackDir = QDir(topdir).filePath(QStringLiteral(".Trash-") + QString::number(::getuid()));
  if (ensureFilesAndInfo(fallbackDir, &result)) {  // REQ-F-043
    result.useRelativePath = true;
    result.topdir = topdir;
    return result;
  }
  return result;  // Both validated locations failed; leave the source untouched.
}

QString uniqueTrashName(const TrashDirectory& dir, const QString& itemName) {
  if (!FileOperationService::destinationExists(dir.filesDir, itemName) &&
      !FileOperationService::destinationExists(dir.infoDir, itemName + QStringLiteral(".trashinfo"))) {
    return itemName;
  }
  const auto dot = itemName.lastIndexOf(u'.');
  const auto base = dot > 0 ? itemName.left(dot) : itemName;
  const auto ext = dot > 0 ? itemName.mid(dot) : QString();
  for (int attempt = 2;; ++attempt) {
    const auto candidate = QStringLiteral("%1_%2%3").arg(base).arg(attempt).arg(ext);
    if (!FileOperationService::destinationExists(dir.filesDir, candidate) &&
        !FileOperationService::destinationExists(dir.infoDir, candidate + QStringLiteral(".trashinfo"))) {
      return candidate;
    }
  }
}

std::optional<TrashError> writeTrashInfo(const TrashDirectory& dir, const QString& trashName,
                                         const QString& originalPath) {
  QFile file(QDir(dir.infoDir).filePath(trashName + QStringLiteral(".trashinfo")));
  if (!file.open(QIODevice::WriteOnly | QIODevice::NewOnly)) {
    return TrashError{.kind = FailureKind::Metadata, .path = file.fileName(), .reason = file.errorString()};
  }
  const auto pathField = dir.useRelativePath ? QDir(dir.topdir).relativeFilePath(originalPath) : originalPath;
  const auto encoded = QUrl::toPercentEncoding(pathField, "/");
  const auto deletionDate = QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-ddTHH:mm:ss"));
  const auto contents =
      QStringLiteral("[Trash Info]\nPath=%1\nDeletionDate=%2\n").arg(QString::fromUtf8(encoded), deletionDate).toUtf8();
  if (file.write(contents) != contents.size() || !file.flush() || ::fsync(file.handle()) != 0) {
    const auto reason =
        file.error() == QFileDevice::NoError ? FileOperationService::describeErrno(errno) : file.errorString();
    file.close();
    ::unlink(encode(file.fileName()).constData());
    return TrashError{.kind = FailureKind::Metadata, .path = file.fileName(), .reason = reason};
  }
  file.close();
  return std::nullopt;
}

void removeTrashInfo(const TrashDirectory& dir, const QString& trashName) {
  QFile::remove(QDir(dir.infoDir).filePath(trashName + QStringLiteral(".trashinfo")));
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
}  // namespace

TrashResult trashEntry(const QString& source, const FileOperationService::CancelFlag& cancel) {
  TrashResult result;
  if (cancel->load()) {
    result.cancelled = true;
    return result;
  }
  const auto directory = selectTrashDir(source);
  if (directory.error) {
    return failedTrash(*directory.error);
  }
  const auto name = uniqueTrashName(directory, QFileInfo(source).fileName());
  const auto absoluteSource = QDir(QFileInfo(source).absolutePath()).filePath(QFileInfo(source).fileName());
  if (auto error = writeTrashInfo(directory, name, absoluteSource)) {
    return failedTrash(*error);
  }
  if (cancel->load()) {
    result.cancelled = true;
  } else {
    const auto destination = QDir(directory.filesDir).filePath(name);
    if (::renameat2(AT_FDCWD, encode(source).constData(), AT_FDCWD, encode(destination).constData(),
                    RENAME_NOREPLACE) != 0) {
      result = failedTrash(
          {.kind = FailureKind::Move, .path = destination, .reason = FileOperationService::describeErrno(errno)});
    }
  }
  if (!result.complete()) {
    removeTrashInfo(directory, name);
  }
  return result;
}

}  // namespace TrashService
