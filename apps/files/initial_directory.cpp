#include "initial_directory.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QStandardPaths>

ResolvedDirectory resolveInitialDirectory(const QStringList& arguments) {
  const auto home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  if (arguments.isEmpty()) {
    return {.path = home, .fallback_reason = QCoreApplication::translate("main", "no folder given")};
  }
  const QFileInfo info(arguments.first());
  if (!info.exists()) {
    return {.path = home, .fallback_reason = QCoreApplication::translate("main", "path does not exist")};
  }
  if (!info.isDir()) {
    return {.path = home, .fallback_reason = QCoreApplication::translate("main", "not a directory")};
  }
  if (!info.isReadable()) {
    return {.path = home, .fallback_reason = QCoreApplication::translate("main", "permission denied")};
  }
  return {.path = info.absoluteFilePath(), .fallback_reason = {}};
}
