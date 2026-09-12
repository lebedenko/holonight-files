#include "icon_name_resolver.h"

#include <QMimeDatabase>
#include <QMimeType>

#include <sys/stat.h>

namespace {
void appendUnique(QStringList& names, const QString& name) {
  if (!name.isEmpty() && !names.contains(name)) {
    names.append(name);
  }
}
}  // namespace

namespace IconNameResolver {

QStringList candidateIconNames(quint32 mode, const QString& fileName) {
  if (S_ISDIR(mode)) {
    return {QStringLiteral("folder"), QStringLiteral("inode-directory")};
  }
  if (S_ISFIFO(mode)) {
    return {QStringLiteral("inode-fifo")};
  }
  if (S_ISSOCK(mode)) {
    return {QStringLiteral("inode-socket")};
  }
  if (S_ISCHR(mode)) {
    return {QStringLiteral("inode-chardevice")};
  }
  if (S_ISBLK(mode)) {
    return {QStringLiteral("inode-blockdevice")};
  }
  const QMimeDatabase database;
  const auto mime = database.mimeTypeForFile(fileName, QMimeDatabase::MatchExtension);
  QStringList names;
  if (mime.isDefault()) {
    // Unregistered extension: application/octet-stream carries no type information (REQ-F-007).
    return {genericFallbackName(false)};
  }
  appendUnique(names, mime.iconName());
  appendUnique(names, mime.genericIconName());
  for (const auto& ancestor : mime.allAncestors()) {
    // Every non-inode type implicitly descends from application/octet-stream, whose generic icon
    // name is the final fallback itself; walking it here would end the chain before later, more
    // specific ancestors such as text/plain (breadth-first order puts it first).
    if (ancestor == u"application/octet-stream") {
      continue;
    }
    const auto parent = database.mimeTypeForName(ancestor);
    appendUnique(names, parent.iconName());
    appendUnique(names, parent.genericIconName());
  }
  appendUnique(names, genericFallbackName(false));
  return names;
}

QString genericFallbackName(bool isDir) {
  return isDir ? QStringLiteral("folder") : QStringLiteral("application-x-generic");
}

}  // namespace IconNameResolver
