#pragma once

#include <QList>
#include <QString>
#include <QStringList>

#include <functional>

struct JumpListEntry {
  QString path;
  // Name under the cursor when this directory was last left; empty until then (REQ-F-038/039).
  QString cursor_entry_name;

  bool operator==(const JumpListEntry&) const = default;
};

struct TraverseResult {
  bool moved = false;
  QString target_path;
  QString restore_name;
  // Candidates removed as missing during this traversal, in the order they were examined.
  QStringList skipped_paths;
};

// Vim-style directory jump list (SPEC.md docs/sdd/navigation-history): QObject-free and without
// filesystem access of its own, so the owner injects the existence check and every rule is
// unit-testable in isolation (REQ-NF-003). Unlike a browser history, visiting a path never
// truncates the entries after the current one; it moves the path to the end (REQ-F-003).
class JumpList {
 public:
  static constexpr int kCapacity = 100;

  using DirectoryPredicate = std::function<bool(const QString&)>;

  [[nodiscard]] int size() const { return static_cast<int>(entries_.size()); }
  [[nodiscard]] int currentIndex() const { return index_; }
  [[nodiscard]] const QList<JumpListEntry>& entries() const { return entries_; }
  [[nodiscard]] QString currentPath() const;
  // Index-only, never touching the filesystem (REQ-F-028).
  [[nodiscard]] bool canGoBack() const { return index_ > 0; }
  [[nodiscard]] bool canGoForward() const { return index_ >= 0 && index_ < size() - 1; }

  // No-op when newPath is already current (REQ-F-004). Otherwise stores outgoingCursorName on the
  // entry being left, moves or appends newPath to the end, and evicts the oldest entry beyond
  // kCapacity (REQ-F-003/005).
  void recordVisit(const QString& newPath, const QString& outgoingCursorName);

  // direction is -1 (back) or +1 (forward); count is clamped to at least 1. Calls isDirectory once
  // per examined candidate; candidates it rejects are removed and not counted as steps (REQ-F-025),
  // even when no valid step exists and the traversal does not move (REQ-F-027). Lands on the
  // count-th valid entry, or the furthest valid one when fewer remain (REQ-F-007/011).
  TraverseResult traverse(int direction, int count, const QString& outgoingCursorName,
                          const DirectoryPredicate& isDirectory);

 private:
  QList<JumpListEntry> entries_;
  int index_ = -1;
};
