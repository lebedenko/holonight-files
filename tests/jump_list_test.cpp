#include "jump_list.h"

#include <QSet>

#include <gtest/gtest.h>

// SPEC.md docs/sdd/navigation-history. Paths are opaque strings here: the existence predicate is
// injected, so nothing in this file touches the filesystem (REQ-NF-003).

namespace {
QStringList paths(const JumpList& list) {
  QStringList result;
  for (const auto& entry : list.entries()) {
    result.append(entry.path);
  }
  return result;
}

JumpList visited(const QStringList& order) {
  JumpList list;
  for (const auto& path : order) {
    list.recordVisit(path, {});
  }
  return list;
}

// Moves the current index to `index` over an all-valid list without changing its entries.
void seek(JumpList& list, int index) {
  const auto allValid = [](const QString&) { return true; };
  const int delta = index - list.currentIndex();
  if (delta != 0) {
    list.traverse(delta < 0 ? -1 : 1, qAbs(delta), {}, allValid);
  }
}

JumpList::DirectoryPredicate missing(const QSet<QString>& gone) {
  return [gone](const QString& path) { return !gone.contains(path); };
}
}  // namespace

TEST(JumpList, FreshInstanceIsEmptyAndCannotTraverse) {
  JumpList list;
  EXPECT_EQ(list.size(), 0);
  EXPECT_EQ(list.currentIndex(), -1);
  EXPECT_TRUE(list.currentPath().isEmpty());
  EXPECT_FALSE(list.canGoBack());
  EXPECT_FALSE(list.canGoForward());
  bool examined = false;
  const auto result = list.traverse(-1, 1, "x", [&](const QString&) { return examined = true; });
  EXPECT_FALSE(result.moved);
  EXPECT_FALSE(examined);
  EXPECT_FALSE(list.traverse(1, 1, "x", [&](const QString&) { return examined = true; }).moved);
  EXPECT_FALSE(examined);
}

TEST(JumpList, RecordVisitDedupsWithoutTruncatingForwardEntries) {
  auto list = visited({"A", "B", "C", "D"});
  seek(list, 1);
  list.recordVisit("C", {});
  EXPECT_EQ(paths(list), (QStringList{"A", "B", "D", "C"}));
  EXPECT_EQ(list.currentIndex(), 3);

  list = visited({"A", "B", "C"});
  list.recordVisit("A", {});
  EXPECT_EQ(paths(list), (QStringList{"B", "C", "A"}));
  EXPECT_EQ(list.currentIndex(), 2);

  list = visited({"A", "B"});
  list.recordVisit("E", {});
  EXPECT_EQ(paths(list), (QStringList{"A", "B", "E"}));
  EXPECT_EQ(list.currentIndex(), 2);
  EXPECT_EQ(list.currentPath(), "E");
}

TEST(JumpList, RecordVisitOfCurrentPathIsNoOp) {
  auto list = visited({"A", "B", "C"});
  seek(list, 1);
  list.recordVisit("B", "ignored");
  EXPECT_EQ(paths(list), (QStringList{"A", "B", "C"}));
  EXPECT_EQ(list.currentIndex(), 1);
  EXPECT_TRUE(list.canGoForward());
  EXPECT_TRUE(list.entries()[1].cursor_entry_name.isEmpty());

  list = visited({"A"});
  list.recordVisit("A", {});
  EXPECT_EQ(list.size(), 1);
}

TEST(JumpList, RecordVisitStoresOutgoingCursorNameOnTheEntryLeft) {
  JumpList list;
  list.recordVisit("A", "stale");  // nothing is being left yet
  EXPECT_TRUE(list.entries()[0].cursor_entry_name.isEmpty());
  list.recordVisit("B", "Pictures");
  EXPECT_EQ(list.entries()[0].cursor_entry_name, "Pictures");
  EXPECT_TRUE(list.entries()[1].cursor_entry_name.isEmpty());
}

TEST(JumpList, CapacityEvictsOldestEntryAtOneHundredOne) {
  JumpList list;
  for (int i = 0; i < JumpList::kCapacity; ++i) {
    list.recordVisit(QString::number(i), {});
  }
  ASSERT_EQ(list.size(), 100);
  list.recordVisit("new", {});
  EXPECT_EQ(list.size(), 100);
  EXPECT_EQ(list.entries().front().path, "1");
  EXPECT_EQ(list.entries().back().path, "new");
  EXPECT_EQ(list.currentIndex(), 99);
  // Re-visiting an existing path at capacity moves it instead of evicting anything.
  list.recordVisit("50", {});
  EXPECT_EQ(list.size(), 100);
  EXPECT_EQ(list.entries().front().path, "1");
}

TEST(JumpList, TraverseBackOneStepMovesIndexAndReturnsCursorName) {
  JumpList list;
  list.recordVisit("A", {});
  list.recordVisit("B", "in-a");
  list.recordVisit("C", "in-b");
  list.recordVisit("D", "in-c");
  const auto result = list.traverse(-1, 1, "in-d", missing({}));
  EXPECT_TRUE(result.moved);
  EXPECT_EQ(result.target_path, "C");
  EXPECT_EQ(result.restore_name, "in-c");
  EXPECT_TRUE(result.skipped_paths.isEmpty());
  EXPECT_EQ(list.currentIndex(), 2);
  EXPECT_EQ(list.entries()[3].cursor_entry_name, "in-d");
}

TEST(JumpList, TraverseBackWithCountSkipsMissingAndLandsOnValidStep) {
  auto list = visited({"A", "B", "C", "D", "E", "F"});
  auto result = list.traverse(-1, 3, {}, missing({}));
  EXPECT_EQ(result.target_path, "C");
  EXPECT_EQ(list.currentIndex(), 2);

  list = visited({"A", "B", "C", "D", "E", "F"});
  result = list.traverse(-1, 3, {}, missing({"D", "E"}));
  EXPECT_TRUE(result.moved);
  EXPECT_EQ(result.target_path, "A");
  EXPECT_EQ(result.skipped_paths, (QStringList{"E", "D"}));
  EXPECT_EQ(paths(list), (QStringList{"A", "B", "C", "F"}));
  EXPECT_EQ(list.currentIndex(), 0);

  list = visited({"A", "B", "C"});
  result = list.traverse(-1, 3, {}, missing({}));
  EXPECT_EQ(result.target_path, "A");  // as far as possible
  EXPECT_EQ(list.currentIndex(), 0);

  // A missing entry beyond the landing step is never examined, so it stays.
  list = visited({"A", "B", "C", "D"});
  result = list.traverse(-1, 1, {}, missing({"A"}));
  EXPECT_EQ(result.target_path, "C");
  EXPECT_EQ(list.size(), 4);
}

TEST(JumpList, BackThenForwardRestoresWithoutChangingListSize) {
  auto list = visited({"A", "B", "C"});
  auto result = list.traverse(-1, 1, "c-name", missing({}));
  EXPECT_EQ(result.target_path, "B");
  EXPECT_EQ(paths(list), (QStringList{"A", "B", "C"}));
  result = list.traverse(1, 1, {}, missing({}));
  EXPECT_EQ(result.target_path, "C");
  EXPECT_EQ(result.restore_name, "c-name");
  EXPECT_EQ(list.currentIndex(), 2);
  EXPECT_EQ(list.size(), 3);
}

TEST(JumpList, TraverseBackAtIndexZeroDoesNothing) {
  auto list = visited({"A"});
  auto result = list.traverse(-1, 1, {}, missing({}));
  EXPECT_FALSE(result.moved);
  EXPECT_EQ(list.currentIndex(), 0);

  list = visited({"A", "B"});
  EXPECT_EQ(list.traverse(-1, 2, {}, missing({})).target_path, "A");
  result = list.traverse(-1, 1, {}, missing({}));
  EXPECT_FALSE(result.moved);
  EXPECT_TRUE(result.skipped_paths.isEmpty());
  EXPECT_EQ(paths(list), (QStringList{"A", "B"}));
  EXPECT_EQ(list.currentIndex(), 0);
}

TEST(JumpList, TraverseForwardOneStep) {
  auto list = visited({"A", "B", "C", "D"});
  seek(list, 2);
  const auto result = list.traverse(1, 1, {}, missing({}));
  EXPECT_TRUE(result.moved);
  EXPECT_EQ(result.target_path, "D");
  EXPECT_EQ(list.currentIndex(), 3);
}

TEST(JumpList, TraverseForwardWithCountLandsOnHighestAvailable) {
  const QStringList ten{"0", "1", "2", "3", "4", "5", "6", "7", "8", "9"};
  auto list = visited(ten);
  seek(list, 2);
  EXPECT_EQ(list.traverse(1, 5, {}, missing({})).target_path, "7");
  EXPECT_EQ(list.currentIndex(), 7);

  list = visited(ten);
  seek(list, 2);
  const auto result = list.traverse(1, 5, {}, missing({"4", "6", "8"}));
  EXPECT_EQ(result.skipped_paths, (QStringList{"4", "6", "8"}));
  EXPECT_EQ(result.target_path, "9");  // only 3, 5, 7, 9 remain valid ahead
  EXPECT_EQ(paths(list), (QStringList{"0", "1", "2", "3", "5", "7", "9"}));
  EXPECT_EQ(list.currentIndex(), 6);
}

TEST(JumpList, TraverseForwardAtLastIndexDoesNothing) {
  auto list = visited({"A", "B", "C", "D", "E"});
  EXPECT_FALSE(list.canGoForward());
  EXPECT_FALSE(list.traverse(1, 1, {}, missing({})).moved);
  EXPECT_EQ(list.currentIndex(), 4);
}

TEST(JumpList, TraverseRemovesAllMissingInDirectionWithoutMoving) {
  auto list = visited({"A", "B"});
  auto result = list.traverse(-1, 1, "b-name", missing({"A"}));
  EXPECT_FALSE(result.moved);
  EXPECT_EQ(result.skipped_paths, (QStringList{"A"}));
  EXPECT_EQ(paths(list), (QStringList{"B"}));
  EXPECT_EQ(list.currentIndex(), 0);
  EXPECT_FALSE(list.canGoBack());

  list = visited({"A", "B", "C", "D"});
  seek(list, 1);
  result = list.traverse(1, 2, {}, missing({"C", "D"}));
  EXPECT_FALSE(result.moved);
  EXPECT_EQ(paths(list), (QStringList{"A", "B"}));
  EXPECT_EQ(list.currentIndex(), 1);

  // REQ-F-026's example: skipped entries on both sides of the landing entry's shift.
  list = visited({"A", "B", "C", "D"});
  result = list.traverse(-1, 1, {}, missing({"B", "C"}));
  EXPECT_EQ(result.target_path, "A");
  EXPECT_EQ(paths(list), (QStringList{"A", "D"}));
  EXPECT_EQ(list.currentIndex(), 0);
}

TEST(JumpList, CanGoBackAndForwardIgnoreFilesystemState) {
  auto list = visited({"A", "B"});
  // A is "missing" as far as any predicate would say, yet the getters only consult the index.
  EXPECT_TRUE(list.canGoBack());
  EXPECT_FALSE(list.canGoForward());
  seek(list, 0);
  EXPECT_FALSE(list.canGoBack());
  EXPECT_TRUE(list.canGoForward());
}

TEST(JumpList, TraverseInvokesPredicateOncePerExaminedCandidate) {
  auto list = visited({"A", "B", "C", "D", "E", "F"});
  QStringList examined;
  const auto result = list.traverse(-1, 2, {}, [&](const QString& path) {
    examined.append(path);
    return path != "E" && path != "C";
  });
  EXPECT_EQ(result.target_path, "B");
  EXPECT_EQ(examined, (QStringList{"E", "D", "C", "B"}));  // never the current entry, nothing past B
}

TEST(JumpList, RestoreNameCaseIsPreservedNotNormalized) {
  JumpList list;
  list.recordVisit("/home", {});
  list.recordVisit("/tmp", {});
  list.recordVisit("/Tmp", "Pictures");  // distinct path: equality is exact; name stored on /tmp
  EXPECT_EQ(list.size(), 3);
  EXPECT_EQ(list.traverse(-1, 1, {}, missing({})).restore_name, "Pictures");
}

TEST(JumpList, RenamedDirectoryTreatedAsMissingByPredicate) {
  auto list = visited({"/home/old-name", "/home"});
  // After a rename only the new path exists; the stored old path is never aliased to it.
  const auto result = list.traverse(-1, 1, {}, [](const QString& path) { return path != "/home/old-name"; });
  EXPECT_FALSE(result.moved);
  EXPECT_EQ(result.skipped_paths, (QStringList{"/home/old-name"}));
  EXPECT_EQ(paths(list), (QStringList{"/home"}));
}
