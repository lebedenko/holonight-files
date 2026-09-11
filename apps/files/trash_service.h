#pragma once

#include "file_operation_service.h"

#include <QString>

#include <optional>

// Freedesktop trash storage. Failure never authorizes permanent deletion.
namespace TrashService {

enum class FailureKind { SourceLookup, DirectoryCreation, Validation, Metadata, Move };
struct TrashError {
  FailureKind kind;
  QString path;
  QString reason;
};
struct TrashResult : FileOperationService::ItemResult {
  std::optional<TrashError> error;
};
struct TrashDirectory {
  std::optional<TrashError> error;
  QString filesDir;
  QString infoDir;
  bool useRelativePath = false;
  QString topdir;
};

TrashDirectory selectTrashDir(const QString& path);
QString uniqueTrashName(const TrashDirectory& dir, const QString& itemName);
std::optional<TrashError> writeTrashInfo(const TrashDirectory& dir, const QString& trashName,
                                         const QString& originalPath);
void removeTrashInfo(const TrashDirectory& dir, const QString& trashName);
TrashResult trashEntry(const QString& source, const FileOperationService::CancelFlag& cancel);

}  // namespace TrashService
