#include "jump_list.h"

#include <QtGlobal>

#include <utility>

QString JumpList::currentPath() const { return index_ >= 0 ? entries_[index_].path : QString{}; }

void JumpList::recordVisit(const QString& newPath, const QString& outgoingCursorName) {
  if (index_ >= 0 && entries_[index_].path == newPath) {
    return;
  }
  if (index_ >= 0) {
    entries_[index_].cursor_entry_name = outgoingCursorName;
  }
  entries_.removeIf([&](const JumpListEntry& entry) { return entry.path == newPath; });
  entries_.append(JumpListEntry{.path = newPath, .cursor_entry_name = {}});
  if (entries_.size() > kCapacity) {
    entries_.removeFirst();
  }
  index_ = size() - 1;
}

TraverseResult JumpList::traverse(int direction, int count, const QString& outgoingCursorName,
                                  const DirectoryPredicate& isDirectory) {
  TraverseResult result;
  if (index_ < 0 || (direction != -1 && direction != 1)) {
    return result;
  }
  entries_[index_].cursor_entry_name = outgoingCursorName;

  // Examine first, remove afterwards in one compaction pass, keeping the whole traversal O(n).
  QList<bool> missing(entries_.size(), false);
  const int wanted = qMax(1, count);
  int steps = 0;
  int landing = -1;
  for (int candidate = index_ + direction; candidate >= 0 && candidate < size() && steps < wanted;
       candidate += direction) {
    if (isDirectory(entries_[candidate].path)) {
      ++steps;
      landing = candidate;
    } else {
      missing[candidate] = true;
      result.skipped_paths.append(entries_[candidate].path);
    }
  }
  if (!result.skipped_paths.isEmpty()) {
    QList<JumpListEntry> kept;
    kept.reserve(entries_.size() - result.skipped_paths.size());
    int newIndex = index_;
    int newLanding = landing;
    for (int row = 0; row < size(); ++row) {
      if (!missing[row]) {
        kept.append(entries_[row]);
        continue;
      }
      // Removals before a position shift it down; the current entry itself is never examined.
      newIndex -= row < index_ ? 1 : 0;
      newLanding -= row < landing ? 1 : 0;
    }
    entries_ = std::move(kept);
    index_ = newIndex;
    landing = landing >= 0 ? newLanding : -1;
  }
  if (landing < 0) {
    return result;
  }
  index_ = landing;
  result.moved = true;
  result.target_path = entries_[landing].path;
  result.restore_name = entries_[landing].cursor_entry_name;
  return result;
}
