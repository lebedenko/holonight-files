#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <atomic>
#include <memory>

// Safe copy/move primitives, called on TaskManager's worker thread. Recursive source
// removal is private to completed moves; trash has no permanent-delete fallback.
namespace FileOperationService {

struct FailedItem {
  QString path;
  QString reason;
};

// Cooperative cancellation token, checked before opening each item and after every read/write
// chunk — the same shape DirectoryModel::walkDirectory()/PreviewService::runPreviewJob() already
// use for their own worker-thread cancellation.
using CancelFlag = std::shared_ptr<std::atomic_bool>;

struct ItemResult {
  bool sourceRetained = false;
  bool destinationCopiesExist = false;
  QStringList skippedChildren;
  bool complete() const { return !failed && !cancelled && nestedFailures.isEmpty() && skippedChildren.isEmpty(); }
  bool failed = false;     // top-level I/O or endpoint failure
  bool cancelled = false;  // cooperative cancellation, separate from I/O errors
  QString reason;
  QList<FailedItem> nestedFailures;  // descendant I/O failures, distinct from skippedChildren
};

// Converts an errno value into the human-readable reason text REQ-F-035's examples use
// ("Permission denied", "No space left on device", "I/O error", ...).
QString describeErrno(int err);

// lstat()s destDir/itemName; true if something (file, directory, or dangling symlink) already
// exists there (REQ-F-021/048).
bool destinationExists(const QString& destDir, const QString& itemName);

// REQ-F-024: "name (2).ext", "name (3).ext", ... in destDir until a free name is found.
QString autoRenameCandidate(const QString& destDir, const QString& itemName);

// Copies srcPath to destPath — a fully resolved destination path; the top-level conflict (if any)
// has already been decided by the caller. If srcPath is a directory, recurses; a name collision
// below the top level auto-skips (REQ-F-048) rather than prompting and records skippedChildren separately. Symlinks
// anywhere in the tree (including srcPath itself) are duplicated via readlink+symlink, never dereferenced
// (REQ-F-012/039). `overwrite` is the top-level conflict resolution: for a file destination it is atomically replaced;
// for a directory destination the existing directory is merged into (kept, recursed into) rather than recreated.
ItemResult copyEntry(const QString& srcPath, const QString& destPath, bool overwrite, const CancelFlag& cancel);

// Moves srcPath to destPath. Tries rename() first; on EXDEV, transparently falls back to
// copyEntry()-then-remove-source, verifying the copy fully succeeded before deleting the source
// (REQ-F-036/037/038). A directory-vs-directory overwrite always takes the copy-then-remove path
// (a merge, not a single atomic rename) so nested collisions still auto-skip consistently.
ItemResult moveEntry(const QString& srcPath, const QString& destPath, bool overwrite, const CancelFlag& cancel);

}  // namespace FileOperationService
