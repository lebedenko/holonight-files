#include "vim_mode_controller.h"

#include "directory_fixtures.h"

#include <QTemporaryDir>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeFile;
using Mode = VimModeController::Mode;
using InsertKind = VimModeController::InsertKind;
using Action = VimModeController::InsertCommitAction;

TEST(VimModeController, StartsInNormalMode) {
  VimModeController vim;
  EXPECT_EQ(vim.currentMode(), Mode::Normal);
}

TEST(VimModeController, VisualEnterExtendAndExit) {
  VimModeController vim;
  vim.enterVisual(5);
  EXPECT_EQ(vim.currentMode(), Mode::Visual);
  EXPECT_EQ(vim.visualAnchorRow(), 5);
  EXPECT_EQ(vim.selectedCount(), 1);
  EXPECT_TRUE(vim.isRowSelected(5));
  EXPECT_FALSE(vim.isRowSelected(6));

  vim.extendVisual(8);
  EXPECT_EQ(vim.selectedCount(), 4);
  for (int row = 5; row <= 8; ++row) {
    EXPECT_TRUE(vim.isRowSelected(row));
  }
  EXPECT_FALSE(vim.isRowSelected(4));
  EXPECT_FALSE(vim.isRowSelected(9));

  vim.extendVisual(3);  // shrinking/reversing past the anchor still selects the full range
  EXPECT_EQ(vim.selectedCount(), 3);
  EXPECT_TRUE(vim.isRowSelected(3));
  EXPECT_TRUE(vim.isRowSelected(5));
  EXPECT_FALSE(vim.isRowSelected(6));

  vim.exitVisual();
  EXPECT_EQ(vim.currentMode(), Mode::Normal);
  EXPECT_EQ(vim.selectedCount(), 0);
  EXPECT_FALSE(vim.isRowSelected(3));
}

TEST(VimModeController, InsertPrependPositionsCursorAtStart) {
  QTemporaryDir dir(fixturePattern("vim-insert-i"));
  ASSERT_TRUE(dir.isValid());
  VimModeController vim;
  vim.enterInsert(InsertKind::Prepend, 2, dir.path(), "example.txt");
  EXPECT_EQ(vim.currentMode(), Mode::Insert);
  EXPECT_EQ(vim.editingRow(), 2);
  EXPECT_FALSE(vim.editingIsCreate());
  EXPECT_EQ(vim.insertText(), "example.txt");
  EXPECT_EQ(vim.insertCursorPosition(), 0);
}

TEST(VimModeController, InsertAppendPositionsCursorAtEnd) {
  QTemporaryDir dir(fixturePattern("vim-insert-a"));
  ASSERT_TRUE(dir.isValid());
  VimModeController vim;
  vim.enterInsert(InsertKind::Append, 0, dir.path(), "example.txt");
  EXPECT_EQ(vim.insertCursorPosition(), QStringLiteral("example.txt").size());
}

TEST(VimModeController, InsertCreateStartsWithEmptyText) {
  QTemporaryDir dir(fixturePattern("vim-insert-o"));
  ASSERT_TRUE(dir.isValid());
  VimModeController vim;
  vim.enterInsert(InsertKind::CreateBelow, 0, dir.path(), {});
  EXPECT_TRUE(vim.editingIsCreate());
  EXPECT_TRUE(vim.insertText().isEmpty());
}

TEST(VimModeController, RenameUnchangedNameCommitsATouch) {
  QTemporaryDir dir(fixturePattern("vim-touch"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "readme.md");
  VimModeController vim;
  vim.enterInsert(InsertKind::Prepend, 0, dir.path(), "readme.md");
  const auto commit = vim.commitInsert();
  EXPECT_EQ(commit.action, Action::Touch);
  EXPECT_EQ(commit.oldPath, dir.filePath("readme.md"));
}

TEST(VimModeController, RenameChangedNameCommitsARename) {
  QTemporaryDir dir(fixturePattern("vim-rename"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "old.txt");
  VimModeController vim;
  vim.enterInsert(InsertKind::Append, 0, dir.path(), "old.txt");
  vim.setInsertText("new.txt");
  const auto commit = vim.commitInsert();
  EXPECT_EQ(commit.action, Action::Rename);
  EXPECT_EQ(commit.oldPath, dir.filePath("old.txt"));
  EXPECT_EQ(commit.newPath, dir.filePath("new.txt"));
}

TEST(VimModeController, RenameToAnInvalidNameStaysInInsertWithNoCommitAction) {
  QTemporaryDir dir(fixturePattern("vim-rename-invalid"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "old.txt");
  VimModeController vim;
  vim.enterInsert(InsertKind::Append, 0, dir.path(), "old.txt");
  vim.setInsertText("../escape");
  EXPECT_FALSE(vim.insertValid());
  const auto commit = vim.commitInsert();
  EXPECT_EQ(commit.action, Action::None);
  EXPECT_EQ(vim.currentMode(), Mode::Insert);
}

TEST(VimModeController, CancelInsertDiscardsEditsWithNoSideEffects) {
  QTemporaryDir dir(fixturePattern("vim-cancel"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "original.txt");
  VimModeController vim;
  vim.enterInsert(InsertKind::Prepend, 0, dir.path(), "original.txt");
  vim.setInsertText("modified.txt");
  vim.cancelInsert();
  EXPECT_EQ(vim.currentMode(), Mode::Normal);
  EXPECT_EQ(vim.editingRow(), -1);
}

TEST(VimModeController, CreateDirectoryCommitStripsTrailingSlash) {
  QTemporaryDir dir(fixturePattern("vim-mkdir"));
  ASSERT_TRUE(dir.isValid());
  VimModeController vim;
  vim.enterInsert(InsertKind::CreateBelow, 0, dir.path(), {});
  vim.setInsertText("newfolder/");
  const auto commit = vim.commitInsert();
  EXPECT_EQ(commit.action, Action::CreateDirectory);
  EXPECT_EQ(commit.newPath, dir.filePath("newfolder"));
}

TEST(VimModeController, CreateFileCommitWithoutTrailingSlash) {
  QTemporaryDir dir(fixturePattern("vim-touch-create"));
  ASSERT_TRUE(dir.isValid());
  VimModeController vim;
  vim.enterInsert(InsertKind::CreateAbove, 0, dir.path(), {});
  vim.setInsertText("newfile.txt");
  const auto commit = vim.commitInsert();
  EXPECT_EQ(commit.action, Action::CreateFile);
  EXPECT_EQ(commit.newPath, dir.filePath("newfile.txt"));
}

TEST(VimModeController, ReportCommitFailedKeepsInsertModeWithAMessage) {
  QTemporaryDir dir(fixturePattern("vim-fail"));
  ASSERT_TRUE(dir.isValid());
  VimModeController vim;
  vim.enterInsert(InsertKind::CreateBelow, 0, dir.path(), {});
  vim.setInsertText("blocked.txt");
  vim.commitInsert();
  vim.reportCommitFailed("Permission denied");
  EXPECT_EQ(vim.currentMode(), Mode::Insert);
  EXPECT_FALSE(vim.insertValid());
  EXPECT_EQ(vim.insertErrorMessage(), "Permission denied");
}

TEST(VimModeController, ReportCommitSucceededReturnsToNormal) {
  QTemporaryDir dir(fixturePattern("vim-success"));
  ASSERT_TRUE(dir.isValid());
  VimModeController vim;
  vim.enterInsert(InsertKind::CreateBelow, 0, dir.path(), {});
  vim.setInsertText("ok.txt");
  vim.commitInsert();
  vim.reportCommitSucceeded();
  EXPECT_EQ(vim.currentMode(), Mode::Normal);
}

TEST(VimModeController, SearchJumpsToTheBestMatchAndTracksAllMatches) {
  VimModeController vim;
  vim.enterSearch(0);
  EXPECT_EQ(vim.currentMode(), Mode::Search);
  vim.setSearchQuery("ex", {"example.txt", "zzz.txt", "extra.txt"});
  EXPECT_GE(vim.searchBestRow(), 0);
  EXPECT_EQ(vim.searchMatchRows(), (QList<int>{0, 2}));
}

TEST(VimModeController, SearchNCyclesForwardWithWraparound) {
  VimModeController vim;
  vim.enterSearch(0);
  vim.setSearchQuery("e", {"e1.txt", "x.txt", "e2.txt", "e3.txt"});
  ASSERT_EQ(vim.searchMatchRows(), (QList<int>{0, 2, 3}));
  EXPECT_EQ(vim.advanceSearchMatch(0, /*forward=*/true), 2);
  EXPECT_EQ(vim.advanceSearchMatch(2, /*forward=*/true), 3);
  EXPECT_EQ(vim.advanceSearchMatch(3, /*forward=*/true), 0);  // wraps to the first match
}

TEST(VimModeController, SearchNCyclesBackwardWithWraparound) {
  VimModeController vim;
  vim.enterSearch(0);
  vim.setSearchQuery("e", {"e1.txt", "x.txt", "e2.txt", "e3.txt"});
  ASSERT_EQ(vim.searchMatchRows(), (QList<int>{0, 2, 3}));
  EXPECT_EQ(vim.advanceSearchMatch(3, /*forward=*/false), 2);
  EXPECT_EQ(vim.advanceSearchMatch(2, /*forward=*/false), 0);
  EXPECT_EQ(vim.advanceSearchMatch(0, /*forward=*/false), 3);  // wraps to the last match
}

TEST(VimModeController, CommitSearchReturnsToNormalWithoutClearingMatchesForNAndNRepeat) {
  VimModeController vim;
  vim.enterSearch(0);
  vim.setSearchQuery("e", {"e1.txt", "x.txt", "e2.txt"});
  vim.commitSearch();
  EXPECT_EQ(vim.currentMode(), Mode::Normal);
  EXPECT_TRUE(vim.searchQuery().isEmpty());
  // REQ-F-026/027: n/N repeat the last search even after Enter returns to NORMAL.
  EXPECT_EQ(vim.advanceSearchMatch(0, /*forward=*/true), 2);
}

TEST(VimModeController, CancelSearchRestoresThePreSearchCursorAndClearsMatches) {
  VimModeController vim;
  vim.enterSearch(7);
  vim.setSearchQuery("e", {"e1.txt", "x.txt", "e2.txt"});
  const int restoreRow = vim.cancelSearch();
  EXPECT_EQ(restoreRow, 7);
  EXPECT_EQ(vim.currentMode(), Mode::Normal);
  EXPECT_EQ(vim.advanceSearchMatch(0, /*forward=*/true), -1);  // no matches remembered after Escape
}
