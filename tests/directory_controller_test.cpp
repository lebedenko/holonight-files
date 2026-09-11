#include "directory_controller.h"

#include "directory_fixtures.h"
#include "directory_model_test_access.h"

#include <QDir>
#include <QScopeGuard>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <fcntl.h>
#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeFile;

namespace {
bool settled(const DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.scanning(); });
}
}  // namespace

TEST(DirectoryController, JAndKMoveTheCursorByOneAndClampAtBoundaries) {
  QTemporaryDir dir(fixturePattern("ctrl-jk"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  writeFile(dir, "b.txt");
  writeFile(dir, "c.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.handleKey("k"));  // already at top; stays clamped
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 1);
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 2);
  EXPECT_TRUE(controller.handleKey("j"));  // already at bottom; stays clamped
  EXPECT_EQ(controller.cursorRow(), 2);
  EXPECT_TRUE(controller.handleKey("k"));
  EXPECT_EQ(controller.cursorRow(), 1);
}

TEST(DirectoryController, NumericPrefixAppliesAsMotionCount) {
  QTemporaryDir dir(fixturePattern("ctrl-count"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 10; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("5"));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 5);
  EXPECT_TRUE(controller.handleKey("2"));
  EXPECT_TRUE(controller.handleKey("k"));
  EXPECT_EQ(controller.cursorRow(), 3);
  // Multi-digit counts accumulate: "1" then "0" then "j" means count 10, not two separate motions.
  EXPECT_TRUE(controller.handleKey("1"));
  EXPECT_TRUE(controller.handleKey("0"));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 9);  // clamped to the last row (10 entries)
}

TEST(DirectoryController, CountBufferResetsAfterAnyKeyMotionOrNot) {
  QTemporaryDir dir(fixturePattern("ctrl-count-reset"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 10; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("5"));
  EXPECT_FALSE(controller.handleKey("x"));  // unrecognized key: still consumes the pending count
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 1);  // count was discarded, not carried over to this j
}

TEST(DirectoryController, GgJumpsToFirstRowAndGJumpsToLastRow) {
  QTemporaryDir dir(fixturePattern("ctrl-gg"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 5; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("G"));
  EXPECT_EQ(controller.cursorRow(), 4);
  EXPECT_TRUE(controller.handleKey("g"));
  EXPECT_TRUE(controller.handleKey("g"));
  EXPECT_EQ(controller.cursorRow(), 0);
}

TEST(DirectoryController, PendingGExpiresAfterTimeoutInsteadOfJumping) {
  QTemporaryDir dir(fixturePattern("ctrl-gg-timeout"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 5; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("j");
  controller.handleKey("j");
  ASSERT_EQ(controller.cursorRow(), 2);
  EXPECT_TRUE(controller.handleKey("g"));
  QTest::qWait(700);  // exceeds the 600ms pending-g timeout
  EXPECT_TRUE(controller.handleKey("g"));
  // The first "g" expired, so this second "g" starts a fresh pending sequence rather than
  // completing "gg" — the cursor must not have jumped to row 0.
  EXPECT_EQ(controller.cursorRow(), 2);
  EXPECT_TRUE(controller.handleKey("g"));
  EXPECT_EQ(controller.cursorRow(), 0);
}

TEST(DirectoryController, HNavigatesToParentDirectory) {
  QTemporaryDir parent(fixturePattern("ctrl-parent"));
  ASSERT_TRUE(parent.isValid());
  ASSERT_TRUE(QDir(parent.path()).mkdir("child"));
  const auto childPath = QDir(parent.path()).filePath("child");
  DirectoryController controller;
  controller.open(childPath);
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), childPath);
  EXPECT_TRUE(controller.handleKey("h"));
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(QDir(controller.currentPath()).canonicalPath(), QDir(parent.path()).canonicalPath());
}

TEST(DirectoryController, CursorRowStaysClampedToZeroOnAnEmptyDirectory) {
  QTemporaryDir dir(fixturePattern("ctrl-empty"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.handleKey("G"));
  EXPECT_EQ(controller.cursorRow(), 0);
}

TEST(DirectoryController, WatcherObservesCreateRenameDeleteWithoutExplicitRefresh) {
  QTemporaryDir dir(fixturePattern("watcher"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  auto names = [&] {
    QStringList result;
    for (int row = 0; row < controller.listing()->rowCount(); ++row) {
      result.append(
          controller.listing()->data(controller.listing()->index(row, 0), DirectoryModel::NameRole).toString());
    }
    return result;
  };
  ASSERT_FALSE(writeFile(dir, "created").isEmpty());
  ASSERT_TRUE(QTest::qWaitFor([&] { return names() == QStringList{"created"}; }));
  ASSERT_TRUE(QFile::rename(dir.filePath("created"), dir.filePath("renamed")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return names() == QStringList{"renamed"}; }));
  ASSERT_TRUE(QFile::remove(dir.filePath("renamed")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return names().isEmpty(); }));
}

TEST(DirectoryController, SpaceTogglesQuickLookWhenACursorIsOnAValidRow) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_FALSE(controller.quickLookOpen());
  EXPECT_TRUE(controller.handleKey(" "));
  EXPECT_TRUE(controller.quickLookOpen());
  EXPECT_TRUE(controller.handleKey(" "));
  EXPECT_FALSE(controller.quickLookOpen());
}

TEST(DirectoryController, SpaceOnAnEmptyDirectoryIsConsumedAsANoOp) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-empty"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey(" "));  // REQ-F-013: consumed, but explicitly a no-op
  EXPECT_FALSE(controller.quickLookOpen());
}

TEST(DirectoryController, EscapeClosesQuickLookAndReturnsFalseWhenAlreadyClosed) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-escape"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  // Closed already: falls through so the window-level fullscreen Shortcut can handle Escape.
  EXPECT_FALSE(controller.handleKey("Escape"));
  ASSERT_TRUE(controller.handleKey(" "));
  ASSERT_TRUE(controller.quickLookOpen());
  EXPECT_TRUE(controller.handleKey("Escape"));
  EXPECT_FALSE(controller.quickLookOpen());
}

TEST(DirectoryController, JAndKKeepUpdatingThePreviewWhileQuickLookStaysOpen) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-live"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  writeFile(dir, "b.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey(" "));
  ASSERT_TRUE(controller.quickLookOpen());
  const auto firstName = controller.preview()->name();
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_TRUE(controller.quickLookOpen());  // still open — j/k never close it
  EXPECT_NE(controller.preview()->name(), firstName);
}

TEST(DirectoryController, VPressEntersVisualModeAndEscapeExitsIt) {
  QTemporaryDir dir(fixturePattern("visual-basic"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 5; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_TRUE(controller.handleKey("v"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Visual);
  EXPECT_EQ(controller.vim()->selectedCount(), 1);
  EXPECT_TRUE(controller.handleKey("Escape"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_EQ(controller.vim()->selectedCount(), 0);
}

TEST(DirectoryController, VisualModeMotionsExtendSelectionAndCountedMotionsWork) {
  QTemporaryDir dir(fixturePattern("visual-extend"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 10; ++i) {
    writeFile(dir, QStringLiteral("f%1.txt").arg(i, 2, 10, QLatin1Char('0')));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("V"));
  EXPECT_TRUE(controller.handleKey("3"));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 3);
  EXPECT_EQ(controller.vim()->selectedCount(), 4);
  for (int row = 0; row <= 3; ++row) {
    EXPECT_TRUE(controller.vim()->isRowSelected(row));
  }
}

TEST(DirectoryController, VisualModeSwallowsUnrecognizedKeysAsNoOps) {
  // Stage 4 (file-operations) gave VISUAL real y/d/D consumers — docs/sdd/file-operations/
  // tests/directory_controller_file_ops_test.cpp covers those. This test now only asserts that a
  // key with no consumer at all (neither a motion nor a file-operation key) stays an inert no-op.
  QTemporaryDir dir(fixturePattern("visual-noop"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  writeFile(dir, "b.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey("v"));
  EXPECT_TRUE(controller.handleKey("x"));  // no consumer at all: consumed, no filesystem effect
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Visual);
  EXPECT_TRUE(QDir(dir.path()).exists("a.txt"));
  EXPECT_TRUE(QDir(dir.path()).exists("b.txt"));
}

TEST(DirectoryController, IEntersInsertWithCursorAtStartAndUnchangedEnterTouches) {
  QTemporaryDir dir(fixturePattern("insert-i-touch"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(dir, "readme.md");
  ASSERT_FALSE(path.isEmpty());
  QFile::setPermissions(path, QFile::permissions(path) | QFile::WriteOwner);
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey("i"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
  EXPECT_EQ(controller.vim()->insertText(), "readme.md");
  EXPECT_EQ(controller.vim()->insertCursorPosition(), 0);
  controller.commitInsertEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_TRUE(QFile::exists(path));  // still there, unrenamed
}

TEST(DirectoryController, AEntersInsertAtEndAndChangedNameRenamesTheFile) {
  QTemporaryDir dir(fixturePattern("insert-a-rename"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(writeFile(dir, "old.txt").isEmpty());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey("a"));
  EXPECT_EQ(controller.vim()->insertCursorPosition(), QStringLiteral("old.txt").size());
  controller.updateInsertText("renamed.txt");
  controller.commitInsertEditing();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_FALSE(QFile::exists(dir.filePath("old.txt")));
  EXPECT_TRUE(QFile::exists(dir.filePath("renamed.txt")));
}

TEST(DirectoryController, EscapeDuringInsertCancelsWithoutAnyRename) {
  QTemporaryDir dir(fixturePattern("insert-escape"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(writeFile(dir, "original.txt").isEmpty());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey("i"));
  controller.updateInsertText("modified.txt");
  controller.cancelInsertEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_TRUE(QFile::exists(dir.filePath("original.txt")));
  EXPECT_FALSE(QFile::exists(dir.filePath("modified.txt")));
}

TEST(DirectoryController, OCreatesADirectoryWhenTheCommittedNameEndsWithASlash) {
  QTemporaryDir dir(fixturePattern("insert-o-mkdir"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(writeFile(dir, "existing.txt").isEmpty());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey("o"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
  EXPECT_TRUE(controller.vim()->editingIsCreate());
  controller.updateInsertText("newdir/");
  controller.commitInsertEditing();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_TRUE(QDir(dir.filePath("newdir")).exists());
}

TEST(DirectoryController, OCreatesAFileWhenTheCommittedNameHasNoTrailingSlash) {
  QTemporaryDir dir(fixturePattern("insert-o-touch"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey("O"));
  controller.updateInsertText("newfile.txt");
  controller.commitInsertEditing();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  EXPECT_TRUE(QFile::exists(dir.filePath("newfile.txt")));
}

TEST(DirectoryController, EscapeDuringCreateDiscardsThePlaceholderWithoutTouchingTheFilesystem) {
  QTemporaryDir dir(fixturePattern("insert-o-escape"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(writeFile(dir, "a.txt").isEmpty());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  const int countBefore = controller.listing()->rowCount();
  ASSERT_TRUE(controller.handleKey("o"));
  EXPECT_EQ(controller.listing()->rowCount(), countBefore + 1);
  controller.updateInsertText("temp.txt");
  controller.cancelInsertEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_FALSE(QFile::exists(dir.filePath("temp.txt")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.listing()->rowCount() == countBefore; }));
}

TEST(DirectoryController, InvalidNameDuringInsertBlocksCommitAndStaysInInsert) {
  QTemporaryDir dir(fixturePattern("insert-invalid"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey("o"));
  controller.updateInsertText("../escape");
  EXPECT_FALSE(controller.vim()->insertValid());
  controller.commitInsertEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);  // still open for correction
  controller.updateInsertText("fixed.txt");
  EXPECT_TRUE(controller.vim()->insertValid());
  controller.commitInsertEditing();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_TRUE(QFile::exists(dir.filePath("fixed.txt")));
}

TEST(DirectoryController, SlashEntersSearchAndLiveJumpsToTheBestMatch) {
  QTemporaryDir dir(fixturePattern("search-basic"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "alpha.txt");
  writeFile(dir, "beta.txt");
  writeFile(dir, "gamma.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey("/"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Search);
  controller.updateSearchQuery("gam");
  EXPECT_EQ(controller.listing()
                ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
                .toString(),
            "gamma.txt");
  controller.commitSearchEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  // REQ-F-028: committing lands on the match without opening it.
  EXPECT_EQ(controller.currentPath(), dir.path());
}

TEST(DirectoryController, EscapeDuringSearchRestoresTheOriginalCursorPosition) {
  QTemporaryDir dir(fixturePattern("search-escape"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "alpha.txt");
  writeFile(dir, "beta.txt");
  writeFile(dir, "gamma.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("j");
  ASSERT_EQ(controller.cursorRow(), 1);
  ASSERT_TRUE(controller.handleKey("/"));
  controller.updateSearchQuery("gam");
  ASSERT_NE(controller.cursorRow(), 1);
  controller.cancelSearchEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_EQ(controller.cursorRow(), 1);
}

TEST(DirectoryController, NAndShiftNRepeatTheLastSearchInNormalModeWithWraparound) {
  QTemporaryDir dir(fixturePattern("search-n-repeat"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "e1.txt");
  writeFile(dir, "other.txt");
  writeFile(dir, "e2.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(controller.handleKey("/"));
  controller.updateSearchQuery("e");
  controller.commitSearchEditing();
  ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  const int first = controller.cursorRow();
  EXPECT_TRUE(controller.handleKey("n"));
  EXPECT_NE(controller.cursorRow(), first);
  EXPECT_TRUE(controller.handleKey("N"));
  EXPECT_EQ(controller.cursorRow(), first);
}

TEST(DirectoryController, OPlacesThePlaceholderImmediatelyBelowTheCursorRegardlessOfAlphabeticalOrder) {
  QTemporaryDir dir(fixturePattern("insert-o-position"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  writeFile(dir, "c.txt");
  writeFile(dir, "e.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  auto nameAt = [&](int row) {
    return controller.listing()->data(controller.listing()->index(row, 0), DirectoryModel::NameRole).toString();
  };
  ASSERT_TRUE(controller.handleKey("j"));  // cursor to "c.txt" (row 1)
  ASSERT_EQ(nameAt(controller.cursorRow()), "c.txt");
  ASSERT_TRUE(controller.handleKey("o"));
  ASSERT_EQ(controller.listing()->rowCount(), 4);
  // The placeholder (its committed name would alphabetically sort as "b*", between a and c) must
  // stay pinned right after "c.txt", not jump to where an empty name would naturally sort.
  EXPECT_EQ(nameAt(0), "a.txt");
  EXPECT_EQ(nameAt(1), "c.txt");
  EXPECT_EQ(nameAt(2), "");  // the placeholder itself
  EXPECT_EQ(nameAt(3), "e.txt");
  EXPECT_EQ(controller.cursorRow(), 2);
  controller.cancelInsertEditing();
}

TEST(DirectoryController, OAboveThePlaceholderPlacesItImmediatelyBeforeTheCursor) {
  QTemporaryDir dir(fixturePattern("insert-shift-o-position"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  writeFile(dir, "c.txt");
  writeFile(dir, "e.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  auto nameAt = [&](int row) {
    return controller.listing()->data(controller.listing()->index(row, 0), DirectoryModel::NameRole).toString();
  };
  ASSERT_TRUE(controller.handleKey("j"));  // cursor to "c.txt" (row 1)
  ASSERT_TRUE(controller.handleKey("O"));
  ASSERT_EQ(controller.listing()->rowCount(), 4);
  EXPECT_EQ(nameAt(0), "a.txt");
  EXPECT_EQ(nameAt(1), "");  // the placeholder, immediately above "c.txt"
  EXPECT_EQ(nameAt(2), "c.txt");
  EXPECT_EQ(nameAt(3), "e.txt");
  EXPECT_EQ(controller.cursorRow(), 1);
  controller.cancelInsertEditing();
}

TEST(DirectoryController, ColonIsANoOpInNormalMode) {
  QTemporaryDir dir(fixturePattern("no-command-mode"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey(":"));  // REQ-C-006: consumed, no mode change
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
}

TEST(PlacesModel, FixedStandardLocations) {
  const PlacesModel places;
  ASSERT_EQ(places.rowCount(), 4);
  const QList<QStandardPaths::StandardLocation> locations{
      QStandardPaths::HomeLocation, QStandardPaths::DocumentsLocation, QStandardPaths::DownloadLocation,
      QStandardPaths::PicturesLocation};
  const QStringList names{"Home", "Documents", "Downloads", "Pictures"};
  for (int row = 0; row < places.rowCount(); ++row) {
    EXPECT_EQ(places.data(places.index(row), PlacesModel::NameRole).toString(), names[row]);
    EXPECT_EQ(places.data(places.index(row), PlacesModel::PathRole).toString(),
              QDir::cleanPath(QStandardPaths::writableLocation(locations[row])));
  }
}

struct DirectoryControllerTestAccess {
  static DirectoryModel& model(DirectoryController& controller) { return controller.model_; }
  static void beforeCommit(DirectoryController& controller,
                           std::function<void(const VimModeController::InsertCommitResult&)> callback) {
    controller.before_commit_for_test_ = std::move(callback);
  }
};

TEST(DirectoryController, ExclusiveCreationPreservesRacingCollisionAndRetainsEditor) {
  QTemporaryDir dir(fixturePattern("exclusive-create"));
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("o");
  controller.updateInsertText("race");
  DirectoryControllerTestAccess::beforeCommit(controller, [&](const auto& commit) {
    EXPECT_EQ(commit.newPath, dir.filePath("race"));
    writeFile(dir, "race", "keep contents");
  });
  controller.commitInsertEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
  EXPECT_EQ(controller.vim()->insertText(), "race");
  EXPECT_EQ(controller.listing()->rowCount(), 1);
  QFile file(dir.filePath("race"));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), "keep contents");
  EXPECT_TRUE(controller.vim()->insertErrorMessage().contains("exists"));
  controller.cancelInsertEditing();
}

TEST(DirectoryController, CreationRejectsFilesDirectoriesAndDanglingLinks) {
  QTemporaryDir dir(fixturePattern("create-collisions"));
  writeFile(dir, "file", "preserved");
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  ASSERT_TRUE(QFile::link(dir.filePath("absent"), dir.filePath("link")));
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  for (const auto& name : {"file", "folder/", "link"}) {
    controller.handleKey("o");
    controller.updateInsertText(name);
    EXPECT_FALSE(controller.vim()->insertValid());
    controller.commitInsertEditing();
    EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
    controller.cancelInsertEditing();
    ASSERT_TRUE(settled(controller));
  }
  EXPECT_FALSE(QFile::exists(dir.filePath("absent")));
}

TEST(DirectoryController, UnchangedTouchPreservesAtimeForFilesDirectoriesAndLinks) {
  QTemporaryDir dir(fixturePattern("touch-types"));
  QTemporaryDir target(fixturePattern("touch-target"));
  writeFile(target, "target", "preserved");
  writeFile(dir, "file");
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  ASSERT_TRUE(QFile::link(target.filePath("target"), dir.filePath("link")));
  ASSERT_TRUE(QFile::link(target.filePath("missing"), dir.filePath("dangling")));
  // Future atime prevents unrelated preview reads from triggering Linux relatime updates.
  const std::array<timespec, 2> oldTimes{
      {{.tv_sec = QDateTime::currentSecsSinceEpoch() + 86400, .tv_nsec = 123}, {.tv_sec = 1000000, .tv_nsec = 456}}};
  ASSERT_EQ(::utimensat(AT_FDCWD, QFile::encodeName(target.filePath("target")).constData(), oldTimes.data(), 0), 0);
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  for (const auto& name : {"file", "folder", "link", "dangling"}) {
    const auto path = QFile::encodeName(dir.filePath(name));
    ASSERT_EQ(::utimensat(AT_FDCWD, path.constData(), oldTimes.data(), AT_SYMLINK_NOFOLLOW), 0);
    controller.handleKey("g");
    controller.handleKey("g");
    for (int step = 0; step < controller.listing()->rowCount() &&
                       controller.listing()
                               ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
                               .toString() != name;
         ++step) {
      controller.handleKey("j");
    }
    controller.handleKey("i");
    controller.commitInsertEditing();
    EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
    struct stat info{};
    ASSERT_EQ(::lstat(path.constData(), &info), 0);
    EXPECT_EQ(info.st_atim.tv_sec, oldTimes[0].tv_sec);
    EXPECT_EQ(info.st_atim.tv_nsec, oldTimes[0].tv_nsec);
    EXPECT_GT(info.st_mtim.tv_sec, oldTimes[1].tv_sec);
    ASSERT_TRUE(settled(controller));
  }
  struct stat info{};
  ASSERT_EQ(::stat(QFile::encodeName(target.filePath("target")).constData(), &info), 0);
  EXPECT_EQ(info.st_mtim.tv_sec, oldTimes[1].tv_sec);
  EXPECT_FALSE(QFile::exists(target.filePath("missing")));
}

TEST(DirectoryController, MissingTouchFailsWithoutRecreatingAndPermissionsRetainCreate) {
  QTemporaryDir dir(fixturePattern("commit-failures"));
  writeFile(dir, "file");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("i");
  ASSERT_TRUE(QFile::remove(dir.filePath("file")));
  controller.commitInsertEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
  EXPECT_EQ(controller.vim()->insertText(), "file");
  EXPECT_TRUE(controller.vim()->insertErrorMessage().contains("no longer exists"));
  EXPECT_FALSE(QFile::exists(dir.filePath("file")));
  controller.cancelInsertEditing();
  ASSERT_TRUE(settled(controller));
  if (files_test::runningAsRoot()) {
    GTEST_SKIP() << "Root bypasses permissions";
  }
  controller.handleKey("o");
  controller.updateInsertText("blocked");
  ASSERT_EQ(::chmod(QFile::encodeName(dir.path()).constData(), 0500), 0);
  controller.commitInsertEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
  EXPECT_EQ(controller.vim()->insertText(), "blocked");
  EXPECT_EQ(controller.vim()->insertErrorMessage(), "Permission denied");
  EXPECT_EQ(::chmod(QFile::encodeName(dir.path()).constData(), 0700), 0);
  controller.cancelInsertEditing();
}

TEST(DirectoryController, NavigationCancelsAllModesAndClearsChordsAndSearch) {
  QTemporaryDir dir(fixturePattern("modal-navigation"));
  QTemporaryDir destination(fixturePattern("modal-destination"));
  writeFile(dir, "a");
  writeFile(dir, "b");
  writeFile(destination, "a");
  writeFile(destination, "b");
  DirectoryController controller;
  for (const auto& key : {"i", "a", "o", "O", "v", "/"}) {
    controller.open(dir.path());
    ASSERT_TRUE(settled(controller));
    controller.handleKey(key);
    if (controller.vim()->currentMode() == VimModeController::Mode::Insert) {
      controller.updateInsertText("unfinished");
    }
    if (controller.vim()->currentMode() == VimModeController::Mode::Search) {
      controller.updateSearchQuery("b");
    }
    controller.open(destination.path());
    ASSERT_TRUE(settled(controller));
    EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
    EXPECT_EQ(controller.vim()->selectedCount(), 0);
    EXPECT_TRUE(controller.vim()->insertText().isEmpty());
    EXPECT_FALSE(QFile::exists(dir.filePath("unfinished")));
    controller.handleKey("n");
    EXPECT_EQ(controller.cursorRow(), 0);
  }
  controller.handleKey("9");
  controller.handleKey("g");
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("j");
  EXPECT_EQ(controller.cursorRow(), 1);
}

TEST(DirectoryController, SearchRepeatInvalidatesAndEscapeRestoresFilenameIdentity) {
  QTemporaryDir dir(fixturePattern("search-revision"));
  writeFile(dir, "a");
  writeFile(dir, "b");
  writeFile(dir, "c");
  writeFile(dir, "d");
  writeFile(dir, ".b");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("/");
  controller.updateSearchQuery("b");
  controller.commitSearchEditing();
  controller.toggleSortDirection();
  controller.handleKey("n");
  EXPECT_EQ(controller.listing()
                ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
                .toString(),
            "b");
  controller.toggleHidden();
  controller.handleKey("n");
  EXPECT_EQ(controller.listing()
                ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
                .toString(),
            ".b");
  controller.handleKey("/");
  controller.updateSearchQuery("c");
  controller.toggleSortDirection();
  controller.cancelSearchEditing();
  EXPECT_EQ(controller.listing()
                ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
                .toString(),
            ".b");
  controller.handleKey("/");
  controller.updateSearchQuery("a");
  controller.cancelSearchEditing();
  controller.handleKey("n");
  EXPECT_EQ(controller.listing()
                ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
                .toString(),
            "b");
  ASSERT_TRUE(QFile::remove(dir.filePath("b")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.listing()->rowCount() == 4; }));
  controller.handleKey("n");
  EXPECT_EQ(controller.listing()
                ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
                .toString(),
            ".b");
  controller.handleKey("/");
  controller.updateSearchQuery("c");
  ASSERT_TRUE(QFile::remove(dir.filePath(".b")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.listing()->rowCount() == 3; }));
  controller.cancelSearchEditing();
  EXPECT_EQ(controller.cursorRow(), 0);  // Removed pre-search identity falls back to original row.
}

TEST(DirectoryController, PendingScansCannotMoveEditorAndCancelReconciles) {
  QTemporaryDir dir(fixturePattern("controller-pending-edit"));
  files_test::populateEntries(dir, 600);
  DirectoryController controller;
  auto& model = DirectoryControllerTestAccess::model(controller);
  for (bool refresh : {false, true}) {
    std::atomic_bool entered = false;
    std::atomic_bool release = false;
    DirectoryModelTestAccess::beforeOpen(model, [&] {
      entered = true;
      while (!release.load()) {
        QThread::msleep(1);
      }
    });
    if (refresh) {
      model.refresh();
    } else {
      controller.open(dir.path());
    }
    const auto unblock = qScopeGuard([&] { release = true; });
    ASSERT_TRUE(QTest::qWaitFor([&] { return entered.load(); }));
    controller.handleKey(refresh ? "i" : "o");
    controller.updateInsertText("unfinished");
    const int row = controller.vim()->editingRow();
    const int count = controller.listing()->rowCount();
    writeFile(dir, "external");
    release = true;
    QTest::qWait(60);
    controller.toggleHidden();
    controller.toggleSortDirection();
    EXPECT_EQ(controller.vim()->editingRow(), row);
    EXPECT_EQ(controller.listing()->rowCount(), count);
    EXPECT_EQ(controller.vim()->insertText(), "unfinished");
    EXPECT_FALSE(controller.listing()->hiddenVisible());
    EXPECT_FALSE(controller.listing()->sortDescending());
    DirectoryModelTestAccess::beforeOpen(model, {});
    controller.cancelInsertEditing();
    ASSERT_TRUE(settled(controller));
    EXPECT_EQ(controller.listing()->rowCount(), 601);
  }
}

TEST(DirectoryController, RenameDirectoriesAndLinksUsesCapturedPathsAndNeverOverwrites) {
  QTemporaryDir dir(fixturePattern("rename-types"));
  writeFile(dir, "target", "keep");
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  ASSERT_TRUE(QFile::link(dir.filePath("target"), dir.filePath("link")));
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  auto select = [&](const QString& name) {
    controller.handleKey("g");
    controller.handleKey("g");
    for (int row = 0; row < controller.listing()->rowCount(); ++row) {
      if (controller.listing()
              ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
              .toString() == name) {
        return;
      }
      controller.handleKey("j");
    }
  };
  for (const auto& name : {QString("folder"), QString("link")}) {
    select(name);
    controller.handleKey("i");
    controller.updateInsertText(name + "-new");
    controller.commitInsertEditing();
    EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
    EXPECT_TRUE(QFileInfo::exists(dir.filePath(name + "-new")));
    ASSERT_TRUE(settled(controller));
  }
  EXPECT_TRUE(QFileInfo(dir.filePath("link-new")).isSymLink());
  select("link-new");
  controller.handleKey("i");
  controller.updateInsertText("collision");
  DirectoryControllerTestAccess::beforeCommit(controller,
                                              [&](const auto&) { writeFile(dir, "collision", "preserve"); });
  controller.commitInsertEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
  EXPECT_TRUE(controller.vim()->insertErrorMessage().contains("exists"));
  EXPECT_TRUE(QFileInfo(dir.filePath("link-new")).isSymLink());
  QFile target(dir.filePath("target"));
  ASSERT_TRUE(target.open(QIODevice::ReadOnly));
  EXPECT_EQ(target.readAll(), "keep");
  controller.cancelInsertEditing();
}
