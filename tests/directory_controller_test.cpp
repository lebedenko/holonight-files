#include "directory_controller.h"

#include "directory_controller_test_access.h"
#include "directory_fixtures.h"
#include "directory_model_test_access.h"
#include "initial_directory.h"
#include "preview_fixtures.h"
#include "settings/xdg_paths.h"
#include "settings_fixtures.h"
#include "state/state_store.h"

#include <QDir>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTest>

#include <array>
#include <fcntl.h>
#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::runningAsRoot;
using files_test::writeFile;
using files_test::writeNumberedLines;

using files_test::findPlaceRow;

namespace {
bool settled(const DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.scanning(); });
}
// The preview worker has answered for the entry under the cursor (Quick Look gating needs its MIME).
bool previewSettled(DirectoryController& controller) {
  return QTest::qWaitFor([&] { return controller.preview()->hasEntry() && !controller.preview()->busy(); }, 5000);
}
bool quickLookReady(DirectoryController& controller) {
  return QTest::qWaitFor([&] { return controller.preview()->quickLookEligible(); }, 5000);
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
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 3);
  EXPECT_TRUE(controller.handleKey("j"));  // already at bottom; stays clamped
  EXPECT_EQ(controller.cursorRow(), 3);
  EXPECT_TRUE(controller.handleKey("k"));
  EXPECT_EQ(controller.cursorRow(), 2);
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
  EXPECT_EQ(controller.cursorRow(), 10);  // clamped to the last row (11 entries)
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
  EXPECT_EQ(controller.cursorRow(), 5);
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
  ASSERT_TRUE(QTest::qWaitFor([&] { return names() == QStringList{"..", "created"}; }));
  ASSERT_TRUE(QFile::rename(dir.filePath("created"), dir.filePath("renamed")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return names() == QStringList{"..", "renamed"}; }));
  ASSERT_TRUE(QFile::remove(dir.filePath("renamed")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return names() == QStringList{".."}; }));
}

TEST(DirectoryController, SpaceTogglesQuickLookWhenACursorIsOnAValidRow) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_FALSE(controller.quickLookOpen());
  ASSERT_TRUE(quickLookReady(controller));
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

// T-021 (docs/sdd/vim-modal-editing/TASKS.md): the VimModeController dispatcher rewrite must not
// regress the Stage 1-2 keybindings it now routes past. Exercises j/k/gg/G, "." hidden toggle,
// "s" sort toggle and Space Quick Look entirely through handleKey(), as Stage 1-2 callers do.
TEST(DirectoryController, Stage1And2KeybindingsStillDispatchThroughHandleKeyUnchanged) {
  QTemporaryDir dir(fixturePattern("ctrl-stage12-smoke"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "b.txt");
  writeFile(dir, "a.txt");
  writeFile(dir, "c.txt");
  writeFile(dir, ".hidden");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  auto nameAt = [&](int row) {
    return controller.listing()->data(controller.listing()->index(row, 0), DirectoryModel::NameRole).toString();
  };

  // j/k motion, unchanged since Stage 1.
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), 1);
  EXPECT_TRUE(controller.handleKey("k"));
  EXPECT_EQ(controller.cursorRow(), 0);

  // gg/G jump, unchanged since Stage 1.
  EXPECT_TRUE(controller.handleKey("G"));
  EXPECT_EQ(controller.cursorRow(), controller.listing()->rowCount() - 1);
  EXPECT_TRUE(controller.handleKey("g"));
  EXPECT_TRUE(controller.handleKey("g"));
  EXPECT_EQ(controller.cursorRow(), 0);

  // "." hidden-files toggle, unchanged since Stage 1: dotfile joins the listing.
  EXPECT_EQ(controller.listing()->rowCount(), 4);
  EXPECT_TRUE(controller.handleKey("."));
  EXPECT_EQ(controller.listing()->rowCount(), 5);
  EXPECT_TRUE(controller.handleKey("."));
  EXPECT_EQ(controller.listing()->rowCount(), 4);

  // "s" sort-direction toggle, unchanged since Stage 1: row 1 identity reverses while row 0 stays "..".
  EXPECT_EQ(nameAt(0), "..");
  EXPECT_EQ(nameAt(1), "a.txt");
  EXPECT_TRUE(controller.handleKey("s"));
  EXPECT_EQ(nameAt(0), "..");
  EXPECT_EQ(nameAt(1), "c.txt");
  EXPECT_TRUE(controller.handleKey("s"));
  EXPECT_EQ(nameAt(0), "..");
  EXPECT_EQ(nameAt(1), "a.txt");

  // Space Quick Look, unchanged since Stage 2 (now gated on the previewed file's MIME).
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_FALSE(controller.quickLookOpen());
  ASSERT_TRUE(quickLookReady(controller));
  EXPECT_TRUE(controller.handleKey(" "));
  EXPECT_TRUE(controller.quickLookOpen());
  EXPECT_TRUE(controller.handleKey(" "));
  EXPECT_FALSE(controller.quickLookOpen());
}

TEST(DirectoryController, EscapeClosesQuickLookAndReturnsFalseWhenAlreadyClosed) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-escape"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("j"));
  // Closed already: falls through so the window-level fullscreen Shortcut can handle Escape.
  EXPECT_FALSE(controller.handleKey("Escape"));
  ASSERT_TRUE(quickLookReady(controller));
  ASSERT_TRUE(controller.handleKey(" "));
  ASSERT_TRUE(controller.quickLookOpen());
  EXPECT_TRUE(controller.handleKey("Escape"));
  EXPECT_FALSE(controller.quickLookOpen());
}

TEST(DirectoryController, QuickLookStaysPinnedToItsFileWhileJKAndArrowsMoveTheCurrentLine) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-pinned"));
  ASSERT_TRUE(dir.isValid());
  writeNumberedLines(dir, "a.txt", 30);
  writeNumberedLines(dir, "b.txt", 30);
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("j"));
  ASSERT_TRUE(quickLookReady(controller));
  ASSERT_TRUE(controller.handleKey(" "));
  ASSERT_TRUE(controller.quickLookOpen());
  const auto pinnedName = controller.preview()->name();
  const auto pinnedRow = controller.cursorRow();
  EXPECT_EQ(controller.preview()->currentLineIndex(), 0);

  for (int i = 0; i < 10; ++i) {
    EXPECT_TRUE(controller.handleKey(i % 2 == 0 ? "j" : "ArrowDown"));
    EXPECT_TRUE(controller.quickLookOpen());
    EXPECT_EQ(controller.cursorRow(), pinnedRow);
    EXPECT_EQ(controller.preview()->name(), pinnedName);
  }
  EXPECT_EQ(controller.preview()->currentLineIndex(), 10);
  for (int i = 0; i < 4; ++i) {
    EXPECT_TRUE(controller.handleKey(i % 2 == 0 ? "k" : "ArrowUp"));
  }
  EXPECT_EQ(controller.preview()->currentLineIndex(), 6);
  EXPECT_EQ(controller.cursorRow(), pinnedRow);
  EXPECT_EQ(controller.preview()->name(), pinnedName);

  EXPECT_TRUE(controller.handleKey("Escape"));
  EXPECT_FALSE(controller.quickLookOpen());
  EXPECT_EQ(controller.cursorRow(), pinnedRow);
}

TEST(DirectoryController, QuickLookLineMovementClampsAndDoesNotEmitListingChanges) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-clamp"));
  ASSERT_TRUE(dir.isValid());
  writeNumberedLines(dir, "a.txt", 3);
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("j"));
  ASSERT_TRUE(quickLookReady(controller));
  ASSERT_TRUE(controller.handleKey(" "));
  QSignalSpy changed(&controller, &DirectoryController::changed);
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(controller.handleKey("k"));
  }
  EXPECT_EQ(controller.preview()->currentLineIndex(), 0);
  for (int i = 0; i < 5; ++i) {
    EXPECT_TRUE(controller.handleKey("j"));
  }
  EXPECT_EQ(controller.preview()->currentLineIndex(), 2);
  EXPECT_EQ(changed.count(), 0);
}

TEST(DirectoryController, QuickLookSwallowsEveryOtherKeyWithoutMovingAnything) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-swallow"));
  ASSERT_TRUE(dir.isValid());
  writeNumberedLines(dir, "a.txt", 5);
  writeNumberedLines(dir, "b.txt", 5);
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("j"));
  ASSERT_TRUE(quickLookReady(controller));
  ASSERT_TRUE(controller.handleKey("3"));  // a pending count typed before opening is discarded
  ASSERT_TRUE(controller.handleKey(" "));
  const auto pinnedName = controller.preview()->name();
  for (const auto* key : {"G", "g", "h", "l", "Return", "v", "/", ".", "s", "y", "d", "5"}) {
    SCOPED_TRACE(key);
    EXPECT_TRUE(controller.handleKey(key));
    EXPECT_TRUE(controller.quickLookOpen());
    EXPECT_EQ(controller.cursorRow(), 1);
    EXPECT_EQ(controller.preview()->name(), pinnedName);
    EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  }
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.preview()->currentLineIndex(), 1);  // the discarded/ignored counts did not multiply j
}

TEST(DirectoryController, ArrowKeysAreIgnoredOutsideQuickLook) {
  QTemporaryDir dir(fixturePattern("ctrl-arrows-closed"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "a.txt");
  writeFile(dir, "b.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_FALSE(controller.handleKey("ArrowDown"));
  EXPECT_EQ(controller.cursorRow(), 0);
}

TEST(DirectoryController, SpaceOpensQuickLookOnlyForImagesAndPlainText) {
  QTemporaryDir dir(fixturePattern("ctrl-quicklook-gate"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("adir"));
  writeNumberedLines(dir, "note.txt", 3);
  writeFile(dir, "photo.jpg", files_test::renderJpegBytes());
  writeFile(dir, "data.json", "{\"a\": 1}\n");
  writeFile(dir, "readme.md", "# hi\n");
  writeFile(dir, "bundle.tar.gz", QByteArray("\x1f\x8b\x08\x00", 4) + QByteArray(64, '\0'));
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));

  const auto rowNamed = [&](const QString& name) {
    for (int row = 0; row < controller.listing()->rowCount(); ++row) {
      if (controller.listing()->data(controller.listing()->index(row, 0), DirectoryModel::NameRole).toString() ==
          name) {
        return row;
      }
    }
    return -1;
  };
  const auto pressSpaceOn = [&](const QString& name) {
    const int row = rowNamed(name);
    EXPECT_GE(row, 0) << qPrintable(name);
    controller.handleKey("g");
    controller.handleKey("g");
    for (int i = 0; i < row; ++i) {
      controller.handleKey("j");
    }
    EXPECT_TRUE(previewSettled(controller)) << qPrintable(name);
    return controller.handleKey(" ");
  };

  for (const auto* name : {"adir", "data.json", "readme.md", "bundle.tar.gz"}) {
    SCOPED_TRACE(name);
    EXPECT_TRUE(pressSpaceOn(QString::fromLatin1(name)));  // consumed...
    EXPECT_FALSE(controller.quickLookOpen());              // ...but a no-op
    EXPECT_EQ(controller.cursorRow(), rowNamed(QString::fromLatin1(name)));
  }
  for (const auto* name : {"note.txt", "photo.jpg"}) {
    SCOPED_TRACE(name);
    ASSERT_TRUE(pressSpaceOn(QString::fromLatin1(name)));
    ASSERT_TRUE(QTest::qWaitFor([&] { return controller.quickLookOpen(); }, 1000));
    EXPECT_TRUE(controller.handleKey(" "));
    EXPECT_FALSE(controller.quickLookOpen());
  }
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
  ASSERT_TRUE(controller.handleKey("j"));
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
  ASSERT_TRUE(controller.handleKey("j"));
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
  ASSERT_TRUE(controller.handleKey("j"));
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
  ASSERT_TRUE(controller.handleKey("j"));
  ASSERT_TRUE(controller.handleKey("j"));  // cursor to "c.txt" (row 2)
  ASSERT_EQ(nameAt(controller.cursorRow()), "c.txt");
  ASSERT_TRUE(controller.handleKey("o"));
  ASSERT_EQ(controller.listing()->rowCount(), 5);
  // The placeholder (its committed name would alphabetically sort as "b*", between a and c) must
  // stay pinned right after "c.txt", not jump to where an empty name would naturally sort.
  EXPECT_EQ(nameAt(0), "..");
  EXPECT_EQ(nameAt(1), "a.txt");
  EXPECT_EQ(nameAt(2), "c.txt");
  EXPECT_EQ(nameAt(3), "");  // the placeholder itself
  EXPECT_EQ(nameAt(4), "e.txt");
  EXPECT_EQ(controller.cursorRow(), 3);
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
  ASSERT_TRUE(controller.handleKey("j"));
  ASSERT_TRUE(controller.handleKey("j"));  // cursor to "c.txt" (row 2)
  ASSERT_TRUE(controller.handleKey("O"));
  ASSERT_EQ(controller.listing()->rowCount(), 5);
  EXPECT_EQ(nameAt(0), "..");
  EXPECT_EQ(nameAt(1), "a.txt");
  EXPECT_EQ(nameAt(2), "");  // the placeholder, immediately above "c.txt"
  EXPECT_EQ(nameAt(3), "c.txt");
  EXPECT_EQ(nameAt(4), "e.txt");
  EXPECT_EQ(controller.cursorRow(), 2);
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
  EXPECT_EQ(controller.listing()->rowCount(), 2);
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
    SCOPED_TRACE(name);
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
    // Navigation and validation may dereference a symlink, updating its atime even with
    // a future timestamp. Establish the timestamp at the commit boundary so this test
    // measures touch's UTIME_OMIT behavior rather than those unrelated reads.
    DirectoryControllerTestAccess::beforeCommit(controller, [&](const auto&) {
      ASSERT_EQ(::utimensat(AT_FDCWD, path.constData(), oldTimes.data(), AT_SYMLINK_NOFOLLOW), 0);
    });
    controller.commitInsertEditing();
    DirectoryControllerTestAccess::beforeCommit(controller, {});
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
  ASSERT_TRUE(controller.handleKey("j"));
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
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.listing()->rowCount() == 5; }));
  controller.handleKey("n");
  EXPECT_EQ(controller.listing()
                ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
                .toString(),
            ".b");
  controller.handleKey("/");
  controller.updateSearchQuery("c");
  ASSERT_TRUE(QFile::remove(dir.filePath(".b")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.listing()->rowCount() == 4; }));
  controller.cancelSearchEditing();
  EXPECT_EQ(controller.cursorRow(), 1);  // Removed pre-search identity falls back to original row.
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
    if (refresh) {
      controller.handleKey("j");
    }
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
    EXPECT_EQ(controller.listing()->rowCount(), 602);
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

namespace {
QString cursorName(DirectoryController& controller) {
  return controller.listing()
      ->data(controller.listing()->index(controller.cursorRow(), 0), DirectoryModel::NameRole)
      .toString();
}

// Holds every walk the controller's model starts inside beforeOpen until released, so a test can
// act while a navigation is still loading.
class LoadGate {
 public:
  explicit LoadGate(DirectoryController& controller) : model_(DirectoryControllerTestAccess::model(controller)) {
    DirectoryModelTestAccess::beforeOpen(model_, [this] {
      entered_ = true;
      while (!released_.load()) {
        QThread::msleep(1);
      }
    });
  }
  ~LoadGate() {
    released_ = true;
    DirectoryModelTestAccess::beforeOpen(model_, {});
  }
  LoadGate(const LoadGate&) = delete;
  LoadGate& operator=(const LoadGate&) = delete;
  LoadGate(LoadGate&&) = delete;
  LoadGate& operator=(LoadGate&&) = delete;
  bool waitEntered() {
    return QTest::qWaitFor([this] { return entered_.load(); });
  }
  void release() { released_ = true; }

 private:
  DirectoryModel& model_;
  std::atomic_bool entered_ = false;
  std::atomic_bool released_ = false;
};

// root/{aaa, mid/{aaa, zed}}: every restore target sorts at a non-zero row.
struct NestedFixture {
  QString root;
  QString mid;
  QString zed;
};
NestedFixture buildNestedFixture(const QTemporaryDir& dir) {
  const NestedFixture fixture{
      .root = dir.path(), .mid = dir.filePath("mid"), .zed = QDir(dir.filePath("mid")).filePath("zed")};
  QDir().mkpath(dir.filePath("aaa"));
  QDir().mkpath(QDir(fixture.mid).filePath("aaa"));
  QDir().mkpath(fixture.zed);
  return fixture;
}
}  // namespace

TEST(DirectoryController, NavigateParentPositionsCursorOnChildBasename) {
  QTemporaryDir dir(fixturePattern("restore-parent"));
  const auto fixture = buildNestedFixture(dir);
  writeFile(dir, "file.txt");
  DirectoryController controller;
  controller.open(fixture.mid);
  ASSERT_TRUE(settled(controller));
  controller.handleKey("h");
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.root);
  EXPECT_EQ(controller.cursorRow(), 2);
  EXPECT_EQ(cursorName(controller), "mid");
}

TEST(DirectoryController, NavigateParentFallsBackToRowZeroWhenBasenameMissing) {
  QTemporaryDir dir(fixturePattern("restore-parent-missing"));
  const auto fixture = buildNestedFixture(dir);
  DirectoryController controller;
  controller.open(fixture.zed);
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(QDir(fixture.zed).removeRecursively());
  controller.handleKey("h");
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.mid);
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_EQ(cursorName(controller), "..");
}

TEST(DirectoryController, RestoreAppliedOnLoadCompletionToNamedRow) {
  QTemporaryDir dir(fixturePattern("restore-large"));
  // Enough entries for several worker batches, so the target is only final at the last one.
  files_test::populateEntries(dir, 1200);
  for (const auto* name : {"dir-a", "dir-m", "dir-z"}) {
    ASSERT_TRUE(QDir(dir.path()).mkdir(name));
  }
  DirectoryController controller;
  controller.open(dir.filePath("dir-z"));
  ASSERT_TRUE(settled(controller));
  controller.handleKey("h");
  ASSERT_TRUE(settled(controller));
  ASSERT_EQ(controller.listing()->rowCount(), 1204);
  EXPECT_EQ(controller.cursorRow(), 3);
  EXPECT_EQ(cursorName(controller), "dir-z");
  // Case-sensitive: a differently cased sibling never matches (REQ-C-004).
  ASSERT_TRUE(QDir(dir.path()).mkdir("Dir-Q"));
  controller.open(dir.filePath("dir-m"));
  ASSERT_TRUE(settled(controller));
  ASSERT_TRUE(QDir(dir.path()).rmdir("dir-m"));
  ASSERT_TRUE(QDir(dir.path()).mkdir("DIR-M"));
  controller.handleKey("h");
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.cursorRow(), 0);
}

TEST(DirectoryController, RestoreFallsBackToRowZeroWhenTargetNotVisible) {
  QTemporaryDir dir(fixturePattern("restore-hidden"));
  ASSERT_TRUE(QDir(dir.path()).mkdir("aaa"));
  ASSERT_TRUE(QDir(dir.path()).mkdir(".secret"));
  writeFile(dir, "b.txt");
  DirectoryController controller;
  controller.open(dir.filePath(".secret"));
  ASSERT_TRUE(settled(controller));
  controller.handleKey("h");
  ASSERT_TRUE(settled(controller));
  EXPECT_FALSE(controller.listing()->hiddenVisible());
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_EQ(cursorName(controller), "..");
}

TEST(DirectoryController, RestoreNotAppliedAgainstEmptyListingDuringLoadReset) {
  QTemporaryDir dir(fixturePattern("restore-reset"));
  const auto fixture = buildNestedFixture(dir);
  DirectoryController controller;
  controller.open(fixture.mid);
  ASSERT_TRUE(settled(controller));
  {
    LoadGate gate(controller);
    controller.handleKey("h");
    ASSERT_TRUE(gate.waitEntered());
    EXPECT_TRUE(controller.scanning());
    EXPECT_EQ(controller.listing()->rowCount(), 1);
    EXPECT_EQ(controller.cursorRow(), 0);
    gate.release();
    ASSERT_TRUE(settled(controller));
  }
  EXPECT_EQ(cursorName(controller), "mid");
}

TEST(DirectoryController, ExplicitJCancelsPendingRestoreBeforeLoadCompletes) {
  QTemporaryDir dir(fixturePattern("restore-cancel"));
  const auto fixture = buildNestedFixture(dir);
  DirectoryController controller;
  controller.open(fixture.zed);
  ASSERT_TRUE(settled(controller));
  for (const auto* motion : {"j", "3", "G"}) {
    controller.open(fixture.zed);
    ASSERT_TRUE(settled(controller));
    LoadGate gate(controller);
    controller.handleKey("h");
    ASSERT_TRUE(gate.waitEntered());
    controller.handleKey(motion);
    if (QString(motion) == "3") {
      controller.handleKey("j");
    }
    gate.release();
    ASSERT_TRUE(settled(controller));
    EXPECT_EQ(controller.cursorRow(), 0) << motion;  // the motion ran on an empty listing
    EXPECT_EQ(cursorName(controller), "..") << motion;
  }
}

TEST(DirectoryController, SecondNavigationBeforeSettleReplacesPendingRestore) {
  QTemporaryDir dir(fixturePattern("restore-replace"));
  const auto fixture = buildNestedFixture(dir);
  QTemporaryDir other(fixturePattern("restore-replace-other"));
  ASSERT_TRUE(QDir(other.path()).mkdir("aaa"));
  ASSERT_TRUE(QDir(other.path()).mkdir("zed"));
  DirectoryController controller;
  controller.open(fixture.zed);
  ASSERT_TRUE(settled(controller));
  {
    LoadGate gate(controller);
    controller.handleKey("h");
    ASSERT_TRUE(gate.waitEntered());
    controller.handleKey("h");
    gate.release();
    ASSERT_TRUE(settled(controller));
  }
  EXPECT_EQ(controller.currentPath(), fixture.root);
  EXPECT_EQ(cursorName(controller), "mid");

  controller.open(fixture.zed);
  ASSERT_TRUE(settled(controller));
  {
    LoadGate gate(controller);
    controller.handleKey("h");  // pending "zed"
    ASSERT_TRUE(gate.waitEntered());
    controller.open(other.path());  // e.g. a Places click; "zed" also exists there
    gate.release();
    ASSERT_TRUE(settled(controller));
  }
  EXPECT_EQ(controller.currentPath(), other.path());
  EXPECT_EQ(controller.cursorRow(), 0);
}

TEST(DirectoryController, WatcherRefreshAfterRestoreDoesNotMoveCursor) {
  QTemporaryDir dir(fixturePattern("restore-watcher"));
  const auto fixture = buildNestedFixture(dir);
  writeFile(dir, "b.txt");
  DirectoryController controller;
  controller.open(fixture.mid);
  ASSERT_TRUE(settled(controller));
  controller.handleKey("h");
  ASSERT_TRUE(settled(controller));
  ASSERT_EQ(cursorName(controller), "mid");
  controller.handleKey("j");
  ASSERT_EQ(cursorName(controller), "b.txt");
  const int row = controller.cursorRow();
  writeFile(dir, "c.txt");
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.listing()->rowCount() == 5 && !controller.scanning(); }));
  EXPECT_EQ(controller.cursorRow(), row);
  // Without an explicit move in between, a refresh still leaves the restored row alone.
  controller.open(fixture.mid);
  ASSERT_TRUE(settled(controller));
  controller.handleKey("h");
  ASSERT_TRUE(settled(controller));
  ASSERT_EQ(controller.cursorRow(), 2);
  ASSERT_TRUE(QDir(dir.path()).mkdir("0-first"));  // sorts before "mid", shifting it down a row
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.listing()->rowCount() == 6 && !controller.scanning(); }));
  EXPECT_EQ(controller.cursorRow(), 2);
}

namespace {
QStringList historyPaths(const DirectoryController& controller) {
  QStringList result;
  for (const auto& entry : DirectoryControllerTestAccess::jumpList(controller).entries()) {
    result.append(entry.path);
  }
  return result;
}

// root/{a, b, c}, each holding files "1.txt", "2.txt", "3.txt".
struct HistoryFixture {
  QString a;
  QString b;
  QString c;
};
HistoryFixture buildHistoryFixture(const QTemporaryDir& dir) {
  const HistoryFixture fixture{.a = dir.filePath("a"), .b = dir.filePath("b"), .c = dir.filePath("c")};
  for (const auto& path : {fixture.a, fixture.b, fixture.c}) {
    QDir().mkpath(path);
    for (const auto* name : {"1.txt", "2.txt", "3.txt"}) {
      QFile file(QDir(path).filePath(name));
      [[maybe_unused]] const bool created = file.open(QIODevice::WriteOnly);
    }
  }
  return fixture;
}

bool openSettled(DirectoryController& controller, const QString& path) {
  controller.open(path);
  return settled(controller);
}
}  // namespace

TEST(DirectoryController, FreshInstanceHistoryIsEmpty) {
  const DirectoryController controller;
  EXPECT_EQ(DirectoryControllerTestAccess::jumpList(controller).size(), 0);
  EXPECT_FALSE(controller.canGoBack());
  EXPECT_FALSE(controller.canGoForward());
}

TEST(DirectoryController, InitialOpenAddsFirstJumpListEntryWithEmptyCursorName) {
  QTemporaryDir dir(fixturePattern("history-initial"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, fixture.a));
  const auto& list = DirectoryControllerTestAccess::jumpList(controller);
  ASSERT_EQ(list.size(), 1);
  EXPECT_EQ(list.currentIndex(), 0);
  EXPECT_EQ(list.entries()[0].path, fixture.a);
  EXPECT_TRUE(list.entries()[0].cursor_entry_name.isEmpty());
  EXPECT_FALSE(controller.canGoBack());
  EXPECT_FALSE(controller.canGoForward());
}

TEST(DirectoryController, TwoInstancesDoNotShareHistory) {
  QTemporaryDir dir(fixturePattern("history-instances"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController first;
  DirectoryController second;
  ASSERT_TRUE(openSettled(first, fixture.a));
  ASSERT_TRUE(openSettled(first, fixture.b));
  ASSERT_TRUE(openSettled(second, fixture.c));
  EXPECT_EQ(historyPaths(first), (QStringList{fixture.a, fixture.b}));
  EXPECT_EQ(historyPaths(second), (QStringList{fixture.c}));
}

TEST(DirectoryController, OpenNavigateIntoNavigateParentAllUpdateHistory) {
  QTemporaryDir dir(fixturePattern("history-methods"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, dir.path()));
  controller.handleKey("j");
  controller.handleKey("j");  // "b"
  controller.handleKey("l");
  ASSERT_TRUE(settled(controller));
  controller.handleKey("h");
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(historyPaths(controller), (QStringList{fixture.b, dir.path()}));
  ASSERT_TRUE(openSettled(controller, dir.path()));  // no-op for history
  EXPECT_EQ(historyPaths(controller), (QStringList{fixture.b, dir.path()}));
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.b);
  EXPECT_TRUE(controller.canGoForward());
  ASSERT_TRUE(openSettled(controller, fixture.b));  // reopening the current entry keeps forward
  EXPECT_TRUE(controller.canGoForward());
}

TEST(DirectoryController, CursorEntryNameCapturedOnEveryNavigationAwayMethod) {
  QTemporaryDir dir(fixturePattern("history-capture"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  const auto& list = DirectoryControllerTestAccess::jumpList(controller);
  const auto storedName = [&](const QString& path) {
    for (const auto& entry : list.entries()) {
      if (entry.path == path) {
        return entry.cursor_entry_name;
      }
    }
    return QStringLiteral("<absent>");
  };

  ASSERT_TRUE(openSettled(controller, fixture.a));
  controller.handleKey("j");
  controller.handleKey("j");                        // 2.txt
  ASSERT_TRUE(openSettled(controller, fixture.b));  // open()
  EXPECT_EQ(storedName(fixture.a), "2.txt");

  ASSERT_TRUE(openSettled(controller, dir.path()));
  controller.handleKey("G");  // "c"
  controller.handleKey("l");  // navigateInto()
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(storedName(dir.path()), "c");

  controller.handleKey("G");  // 3.txt
  controller.handleKey("h");  // navigateParent()
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(storedName(fixture.c), "3.txt");
  EXPECT_EQ(cursorName(controller), "c");  // the parent restore, independent of history

  controller.handleKey("k");         // "b"
  controller.navigateHistoryBack();  // back: to c, restoring 3.txt
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(storedName(dir.path()), "b");
  EXPECT_EQ(controller.currentPath(), fixture.c);
  EXPECT_EQ(cursorName(controller), "3.txt");

  controller.handleKey("k");            // 2.txt
  controller.navigateHistoryForward();  // forward: to root, restoring "b"
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(storedName(fixture.c), "2.txt");
  EXPECT_EQ(controller.currentPath(), dir.path());
  EXPECT_EQ(cursorName(controller), "b");
}

TEST(DirectoryController, EmptyOrUnloadedDirectoryCapturesEmptyCursorName) {
  QTemporaryDir dir(fixturePattern("history-empty-name"));
  const auto fixture = buildHistoryFixture(dir);
  ASSERT_TRUE(QDir(dir.path()).mkdir("empty"));
  const auto empty = dir.filePath("empty");
  DirectoryController controller;
  const auto& list = DirectoryControllerTestAccess::jumpList(controller);

  ASSERT_TRUE(openSettled(controller, empty));
  ASSERT_TRUE(openSettled(controller, fixture.a));
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), empty);
  EXPECT_TRUE(list.entries()[0].cursor_entry_name.isEmpty());
  EXPECT_EQ(controller.cursorRow(), 0);

  ASSERT_TRUE(openSettled(controller, fixture.b));
  controller.handleKey("G");  // 3.txt, so a stale name would be visible
  {
    LoadGate gate(controller);
    controller.open(fixture.c);
    ASSERT_TRUE(gate.waitEntered());
    controller.open(fixture.a);  // c never finished loading
    gate.release();
    ASSERT_TRUE(settled(controller));
  }
  EXPECT_EQ(historyPaths(controller), (QStringList{empty, fixture.b, fixture.c, fixture.a}));
  EXPECT_EQ(list.entries()[1].cursor_entry_name, "3.txt");
  EXPECT_TRUE(list.entries()[2].cursor_entry_name.isEmpty());
}

TEST(DirectoryController, BackSkipsMissingEntriesAndReportsSkippedStatusMessage) {
  QTemporaryDir dir(fixturePattern("history-skip"));
  const auto fixture = buildHistoryFixture(dir);
  ASSERT_TRUE(QDir(dir.path()).mkdir("d"));
  const auto dirD = dir.filePath("d");
  DirectoryController controller;
  for (const auto& path : {fixture.a, fixture.b, fixture.c, dirD}) {
    ASSERT_TRUE(openSettled(controller, path));
  }
  ASSERT_TRUE(QDir(fixture.b).removeRecursively());
  ASSERT_TRUE(QDir(fixture.c).removeRecursively());
  QSignalSpy navigated(&controller, &DirectoryController::navigated);
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.a);
  EXPECT_EQ(historyPaths(controller), (QStringList{fixture.a, dirD}));
  EXPECT_EQ(DirectoryControllerTestAccess::jumpList(controller).currentIndex(), 0);
  EXPECT_TRUE(controller.statusMessage().startsWith("Skipped missing:"));
  EXPECT_TRUE(controller.statusMessage().contains(fixture.b));
  EXPECT_TRUE(controller.statusMessage().contains(fixture.c));
  EXPECT_GE(navigated.count(), 1);

  controller.navigateHistoryForward();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), dirD);
  EXPECT_FALSE(controller.statusMessage().contains("Skipped missing"));
}

TEST(DirectoryController, AllEntriesMissingInDirectionShowsStatusAndDoesNotNavigate) {
  QTemporaryDir dir(fixturePattern("history-all-missing"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, fixture.a));
  ASSERT_TRUE(openSettled(controller, fixture.b));
  controller.handleKey("j");
  ASSERT_TRUE(QDir(fixture.a).removeRecursively());
  EXPECT_TRUE(controller.canGoBack());  // index-only until a traversal prunes it (REQ-F-028)
  QSignalSpy navigated(&controller, &DirectoryController::navigated);
  controller.navigateHistoryBack();
  EXPECT_EQ(navigated.count(), 0);
  EXPECT_EQ(controller.currentPath(), fixture.b);
  EXPECT_EQ(controller.cursorRow(), 1);
  EXPECT_EQ(historyPaths(controller), (QStringList{fixture.b}));
  EXPECT_FALSE(controller.canGoBack());
  EXPECT_EQ(controller.statusMessage(), "Skipped missing: " + fixture.a);

  // With nothing left to try, a further back leaves the message alone (REQ-F-009).
  controller.navigateHistoryBack();
  EXPECT_EQ(navigated.count(), 0);
  EXPECT_EQ(controller.statusMessage(), "Skipped missing: " + fixture.a);
}

TEST(DirectoryController, RenamedDirectoryDetectedAsMissingOnTraversal) {
  QTemporaryDir dir(fixturePattern("history-renamed"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, fixture.a));
  ASSERT_TRUE(openSettled(controller, fixture.b));
  ASSERT_TRUE(QDir(dir.path()).rename("a", "a-renamed"));
  controller.navigateHistoryBack();
  EXPECT_EQ(controller.currentPath(), fixture.b);
  EXPECT_EQ(historyPaths(controller), (QStringList{fixture.b}));
}

TEST(DirectoryController, VanishingDirectoryBetweenCheckAndLoadShowsDirectoryError) {
  QTemporaryDir dir(fixturePattern("history-vanish"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, fixture.a));
  ASSERT_TRUE(openSettled(controller, fixture.b));
  auto& model = DirectoryControllerTestAccess::model(controller);
  // Runs on the worker after the UI-thread existence check passed, before opendir().
  DirectoryModelTestAccess::beforeOpen(model, [&] { QDir(fixture.a).removeRecursively(); });
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  DirectoryModelTestAccess::beforeOpen(model, {});
  EXPECT_EQ(controller.currentPath(), fixture.a);
  EXPECT_FALSE(controller.directoryError().isEmpty());
  EXPECT_EQ(controller.listing()->rowCount(), 0);
}

TEST(DirectoryController, InaccessibleDirectoryIsTraversedNotSkippedAndKeptInHistory) {
  if (files_test::runningAsRoot()) {
    GTEST_SKIP() << "root bypasses permission bits";
  }
  QTemporaryDir dir(fixturePattern("history-inaccessible"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, fixture.a));
  ASSERT_TRUE(openSettled(controller, fixture.b));
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  ASSERT_EQ(::chmod(QFile::encodeName(fixture.b).constData(), 0), 0);
  const auto restore = qScopeGuard([&] { ::chmod(QFile::encodeName(fixture.b).constData(), 0700); });
  controller.navigateHistoryForward();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.b);
  EXPECT_TRUE(controller.directoryError().contains("Permission denied")) << controller.directoryError().toStdString();
  EXPECT_FALSE(controller.statusMessage().contains("Skipped missing"));
  EXPECT_EQ(historyPaths(controller), (QStringList{fixture.a, fixture.b}));
  EXPECT_EQ(DirectoryControllerTestAccess::jumpList(controller).currentIndex(), 1);
}

TEST(DirectoryController, HistoryNavigationInertInVisualSearchInsertModes) {
  QTemporaryDir dir(fixturePattern("history-modes"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, fixture.a));
  ASSERT_TRUE(openSettled(controller, fixture.b));
  ASSERT_TRUE(openSettled(controller, fixture.c));
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  ASSERT_EQ(controller.currentPath(), fixture.b);
  controller.handleKey("j");
  for (const auto* key : {"v", "/", "i", "o"}) {
    controller.handleKey(key);
    ASSERT_NE(controller.vim()->currentMode(), VimModeController::Mode::Normal) << key;
    controller.navigateHistoryBack();
    controller.navigateHistoryForward();
    controller.goBack();
    controller.goForward();
    EXPECT_EQ(controller.currentPath(), fixture.b) << key;
    EXPECT_EQ(DirectoryControllerTestAccess::jumpList(controller).currentIndex(), 1) << key;
    if (controller.vim()->currentMode() == VimModeController::Mode::Visual) {
      controller.handleKey("Escape");
    } else if (controller.vim()->currentMode() == VimModeController::Mode::Search) {
      controller.cancelSearchEditing();
    } else {
      controller.cancelInsertEditing();
    }
    ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal) << key;
  }
  controller.navigateHistoryForward();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.c);
}

TEST(DirectoryController, HistoryNavigationInertWhilePromptOpen) {
  QTemporaryDir home(fixturePattern("history-prompt-home"));
  QTemporaryDir dir(fixturePattern("history-prompt"));
  const files_test::ScopedXdgDataHome guard(home.path());
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, fixture.a));
  ASSERT_TRUE(openSettled(controller, fixture.b));
  controller.handleKey("j");
  controller.handleKey("D");
  ASSERT_TRUE(controller.tasks()->hasPrompt());
  controller.navigateHistoryBack();
  controller.goBack();
  EXPECT_EQ(controller.currentPath(), fixture.b);
  EXPECT_TRUE(controller.canGoBack());
  controller.handleKey("n");  // decline
  ASSERT_FALSE(controller.tasks()->hasPrompt());
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.a);
}

TEST(DirectoryController, PendingCountConsumedByHistoryNavigationRegardlessOfOutcome) {
  QTemporaryDir dir(fixturePattern("history-count"));
  const auto fixture = buildHistoryFixture(dir);
  ASSERT_TRUE(QDir(dir.path()).mkdir("d"));
  DirectoryController controller;
  for (const auto& path : {fixture.a, fixture.b, fixture.c, dir.filePath("d")}) {
    ASSERT_TRUE(openSettled(controller, path));
  }
  controller.handleKey("2");
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.b);
  controller.handleKey("9");
  controller.navigateHistoryForward();  // as far as possible
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), dir.filePath("d"));
  controller.handleKey("5");
  controller.navigateHistoryForward();  // impossible, still consumes the count
  EXPECT_EQ(controller.currentPath(), dir.filePath("d"));
  controller.navigateHistoryBack();  // count 1
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.c);
  controller.handleKey("v");
  controller.handleKey("3");
  controller.navigateHistoryBack();  // gated off in VISUAL, but still consumes the count
  EXPECT_EQ(controller.currentPath(), fixture.c);
  controller.handleKey("j");
  EXPECT_EQ(controller.cursorRow(), 1);
}

TEST(DirectoryController, GoBackAndGoForwardAlwaysUseCountOneIgnoringPendingCount) {
  QTemporaryDir dir(fixturePattern("history-buttons"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  for (const auto& path : {fixture.a, fixture.b, fixture.c}) {
    ASSERT_TRUE(openSettled(controller, path));
  }
  controller.handleKey("2");
  controller.goBack();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.b);
  controller.handleKey("j");  // the discarded count must not leak into this motion
  EXPECT_EQ(controller.cursorRow(), 1);
  controller.handleKey("2");
  controller.goBack();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.a);
  controller.handleKey("2");
  controller.goForward();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.b);
}

TEST(DirectoryController, HistoryNavigationStaysPinnedUntilQuickLookCloses) {
  QTemporaryDir dir(fixturePattern("history-quicklook"));
  const auto fixture = buildHistoryFixture(dir);
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, fixture.a));
  ASSERT_TRUE(openSettled(controller, fixture.b));
  ASSERT_TRUE(openSettled(controller, fixture.c));
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  ASSERT_EQ(controller.currentPath(), fixture.b);
  controller.handleKey("j");
  ASSERT_TRUE(quickLookReady(controller));
  controller.handleKey(" ");
  ASSERT_TRUE(controller.quickLookOpen());
  const auto row = controller.cursorRow();
  const auto name = controller.preview()->name();
  for (const auto navigate : {&DirectoryController::navigateHistoryBack, &DirectoryController::navigateHistoryForward,
                              &DirectoryController::goBack, &DirectoryController::goForward}) {
    (controller.*navigate)();
    EXPECT_TRUE(controller.quickLookOpen());
    EXPECT_EQ(controller.currentPath(), fixture.b);
    EXPECT_EQ(controller.cursorRow(), row);
    EXPECT_EQ(controller.preview()->name(), name);
  }
  controller.handleKey("Escape");
  EXPECT_FALSE(controller.quickLookOpen());
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.a);
}

TEST(DirectoryController, HistoryPathIsCleanedAbsoluteNotSymlinkResolved) {
  QTemporaryDir dir(fixturePattern("history-paths"));
  const auto fixture = buildHistoryFixture(dir);
  const auto link = dir.filePath("link-to-a");
  ASSERT_TRUE(QFile::link(fixture.a, link));
  DirectoryController controller;
  controller.open(dir.path() + "/c/../b//");
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), fixture.b);
  ASSERT_TRUE(openSettled(controller, fixture.b));
  EXPECT_EQ(historyPaths(controller), (QStringList{fixture.b}));

  ASSERT_TRUE(openSettled(controller, link));
  ASSERT_TRUE(openSettled(controller, fixture.c));
  controller.navigateHistoryBack();
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), link);
  EXPECT_EQ(historyPaths(controller), (QStringList{fixture.b, link, fixture.c}));
}

namespace {
using Classification = LocationClassifier::Classification;
using files_test::FakeLocationClassifier;
using files_test::RecordingWarningSink;
using files_test::ScopedXdgStateHome;

QString homePath() { return QStandardPaths::writableLocation(QStandardPaths::HomeLocation); }

QByteArray fileBytes(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}

// Opens path and waits until its load-time classification has reached the controller.
bool openAndTrack(DirectoryController& controller, const QString& path) {
  QSignalSpy succeeded(&DirectoryControllerTestAccess::model(controller), &DirectoryModel::loadSucceeded);
  controller.open(path);
  return succeeded.wait(5000) && settled(controller);
}

bool shutDown(DirectoryController& controller) {
  QSignalSpy finished(&controller, &DirectoryController::shutdownFinished);
  controller.shutdown();
  return finished.count() > 0 || finished.wait(5000);
}

// Waits for a restore candidate's worker-thread validation to be applied.
bool restore(DirectoryController& controller, const QString& path) {
  QSignalSpy navigated(&controller, &DirectoryController::navigated);
  controller.openRestoreCandidate(path);
  return navigated.wait(5000);
}
}  // namespace

// SPEC.md REQ-F-012
TEST(DirectoryController, RestoreCandidateThatIsLocalOpensWithoutReason) {
  QTemporaryDir dir(fixturePattern("restore-ok"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  DirectoryControllerTestAccess::setLocationClassifier(controller,
                                                       std::make_shared<FakeLocationClassifier>(Classification::Local));
  ASSERT_TRUE(restore(controller, dir.path()));
  EXPECT_EQ(controller.currentPath(), dir.path());
  EXPECT_TRUE(controller.statusMessage().isEmpty());
}

// SPEC.md REQ-F-013/014/027
TEST(DirectoryController, FailedRestoreOpensHomeWithMatchingReason) {
  QTemporaryDir dir(fixturePattern("restore-fallback"));
  ASSERT_TRUE(dir.isValid());
  const auto file = writeFile(dir, "file.txt");
  struct Case {
    QString path;
    Classification classification;
    const char* reason;
  };
  const std::vector<Case> cases = {
      {.path = dir.filePath("deleted"),
       .classification = Classification::Local,
       .reason = "last location does not exist"},
      {.path = file, .classification = Classification::Local, .reason = "last location is not a directory"},
      {.path = dir.path(), .classification = Classification::Network, .reason = "last location is not local"},
      {.path = dir.path(), .classification = Classification::Removable, .reason = "last location is not local"},
  };
  for (const auto& testCase : cases) {
    DirectoryController controller;
    DirectoryControllerTestAccess::setLocationClassifier(
        controller, std::make_shared<FakeLocationClassifier>(testCase.classification));
    ASSERT_TRUE(restore(controller, testCase.path));
    EXPECT_EQ(controller.currentPath(), QDir::cleanPath(homePath()));
    EXPECT_EQ(controller.statusMessage(), testCase.reason);
  }
  if (!runningAsRoot()) {
    ASSERT_TRUE(QDir(dir.path()).mkdir("blocked"));
    const auto blocked = dir.filePath("blocked");
    const auto restorePermissions = qScopeGuard([&] { ::chmod(QFile::encodeName(blocked).constData(), 0700); });
    ASSERT_EQ(::chmod(QFile::encodeName(blocked).constData(), 0), 0);
    DirectoryController controller;
    ASSERT_TRUE(restore(controller, blocked));
    EXPECT_EQ(controller.statusMessage(), "last location is not readable");
  }
}

TEST(DirectoryController, NavigationDuringRestoreValidationWins) {
  QTemporaryDir stored(fixturePattern("restore-stored"));
  QTemporaryDir chosen(fixturePattern("restore-chosen"));
  ASSERT_TRUE(stored.isValid() && chosen.isValid());
  DirectoryController controller;
  DirectoryControllerTestAccess::setLocationClassifier(
      controller, std::make_shared<FakeLocationClassifier>(Classification::Local, std::chrono::milliseconds(300)));
  QSignalSpy validated(&DirectoryControllerTestAccess::model(controller), &DirectoryModel::restoreValidated);
  controller.openRestoreCandidate(stored.path());
  controller.open(chosen.path());
  ASSERT_TRUE(validated.wait(5000));
  QTest::qWait(50);
  EXPECT_EQ(controller.currentPath(), chosen.path());
}

// SPEC.md REQ-F-017/019: Local A -> Network B -> close stores A.
TEST(DirectoryController, ShutdownSavesTheLastLocalFolder) {
  QTemporaryDir state(fixturePattern("save-state"));
  QTemporaryDir local(fixturePattern("save-local"));
  QTemporaryDir network(fixturePattern("save-network"));
  ASSERT_TRUE(state.isValid() && local.isValid() && network.isValid());
  const ScopedXdgStateHome stateHome(state.path());
  DirectoryController controller;
  controller.configureRestore(true);
  const auto classifier = std::make_shared<FakeLocationClassifier>(Classification::Local);
  classifier->overrides.insert(network.path(), Classification::Network);
  DirectoryControllerTestAccess::setLocationClassifier(controller, classifier);
  ASSERT_TRUE(openAndTrack(controller, local.path()));
  ASSERT_TRUE(openAndTrack(controller, network.path()));
  ASSERT_TRUE(shutDown(controller));
  RecordingWarningSink warnings;
  EXPECT_EQ(StateStore(XdgPaths::stateFilePath(), XdgPaths::stateDirPath()).load(warnings).last_location, local.path());
  EXPECT_TRUE(warnings.messages.isEmpty());
}

// SPEC.md REQ-F-020
TEST(DirectoryController, ShutdownWithoutAnyLocalLoadLeavesStateUntouched) {
  QTemporaryDir state(fixturePattern("save-none"));
  QTemporaryDir network(fixturePattern("save-none-network"));
  ASSERT_TRUE(state.isValid() && network.isValid());
  const ScopedXdgStateHome stateHome(state.path());
  RecordingWarningSink warnings;
  ASSERT_TRUE(StateStore(XdgPaths::stateFilePath(), XdgPaths::stateDirPath()).save("/previous", warnings));
  const auto before = fileBytes(XdgPaths::stateFilePath());
  DirectoryController controller;
  controller.configureRestore(true);
  DirectoryControllerTestAccess::setLocationClassifier(
      controller, std::make_shared<FakeLocationClassifier>(Classification::Network));
  ASSERT_TRUE(openAndTrack(controller, network.path()));
  ASSERT_TRUE(shutDown(controller));
  EXPECT_EQ(fileBytes(XdgPaths::stateFilePath()), before);
}

// SPEC.md REQ-F-018
TEST(DirectoryController, DisabledRestoreNeverWritesState) {
  QTemporaryDir state(fixturePattern("save-disabled"));
  QTemporaryDir local(fixturePattern("save-disabled-local"));
  ASSERT_TRUE(state.isValid() && local.isValid());
  const ScopedXdgStateHome stateHome(state.path());
  {
    DirectoryController controller;
    DirectoryControllerTestAccess::setLocationClassifier(
        controller, std::make_shared<FakeLocationClassifier>(Classification::Local));
    ASSERT_TRUE(openAndTrack(controller, local.path()));
    ASSERT_TRUE(shutDown(controller));
  }
  EXPECT_FALSE(QFileInfo::exists(XdgPaths::stateDirPath()));
  RecordingWarningSink warnings;
  ASSERT_TRUE(StateStore(XdgPaths::stateFilePath(), XdgPaths::stateDirPath()).save("/previous", warnings));
  const auto before = fileBytes(XdgPaths::stateFilePath());
  DirectoryController controller;
  controller.configureRestore(false);
  DirectoryControllerTestAccess::setLocationClassifier(controller,
                                                       std::make_shared<FakeLocationClassifier>(Classification::Local));
  ASSERT_TRUE(openAndTrack(controller, local.path()));
  ASSERT_TRUE(shutDown(controller));
  EXPECT_EQ(fileBytes(XdgPaths::stateFilePath()), before);
}

// SPEC.md REQ-F-010
TEST(DirectoryController, StateWriteFailureWarnsOnceAndStillFinishesShutdown) {
  if (runningAsRoot()) {
    GTEST_SKIP() << "root bypasses chmod 0555 permission checks";
  }
  QTemporaryDir state(fixturePattern("save-readonly"));
  QTemporaryDir local(fixturePattern("save-readonly-local"));
  ASSERT_TRUE(state.isValid() && local.isValid());
  const ScopedXdgStateHome stateHome(state.path());
  const auto restorePermissions = qScopeGuard([&] { ::chmod(QFile::encodeName(state.path()).constData(), 0700); });
  ASSERT_EQ(::chmod(QFile::encodeName(state.path()).constData(), 0555), 0);
  DirectoryController controller;
  controller.configureRestore(true);
  const auto warnings = std::make_shared<RecordingWarningSink>();
  DirectoryControllerTestAccess::setWarningSink(controller, warnings);
  DirectoryControllerTestAccess::setLocationClassifier(controller,
                                                       std::make_shared<FakeLocationClassifier>(Classification::Local));
  ASSERT_TRUE(openAndTrack(controller, local.path()));
  ASSERT_TRUE(shutDown(controller));
  EXPECT_EQ(warnings->messages.size(), 1);
}

// SPEC.md REQ-F-012 end to end: one session saves, the next plans startup and restores.
TEST(DirectoryController, SavedLocationIsRestoredByTheNextSession) {
  QTemporaryDir state(fixturePattern("roundtrip-state"));
  QTemporaryDir local(fixturePattern("roundtrip-local"));
  ASSERT_TRUE(state.isValid() && local.isValid());
  QDir(local.path()).mkdir("inner");
  const auto inner = local.filePath("inner");
  const ScopedXdgStateHome stateHome(state.path());
  {
    DirectoryController first;
    first.configureRestore(true);
    DirectoryControllerTestAccess::setLocationClassifier(
        first, std::make_shared<FakeLocationClassifier>(Classification::Local));
    ASSERT_TRUE(openAndTrack(first, local.path()));
    ASSERT_TRUE(openAndTrack(first, inner));
    ASSERT_TRUE(shutDown(first));
  }
  RecordingWarningSink warnings;
  const auto stored = StateStore(XdgPaths::stateFilePath(), XdgPaths::stateDirPath()).load(warnings);
  const auto plan = planStartup({}, true, stored.last_location);
  ASSERT_FALSE(plan.resolved.has_value());
  DirectoryController second;
  second.configureRestore(true);
  ASSERT_TRUE(restore(second, plan.pending_candidate_path));
  EXPECT_EQ(second.currentPath(), inner);
  EXPECT_TRUE(second.statusMessage().isEmpty());
  EXPECT_TRUE(warnings.messages.isEmpty());
}

// REQ-F-017/019: close must drain classification of a displayed load before saving, including
// when a watcher refresh was queued while classification was still running.
TEST(DirectoryController, ShutdownSavesAcceptedLoadWithPendingClassification) {
  for (bool refresh : {false, true}) {
    SCOPED_TRACE(refresh);
    QTemporaryDir state(fixturePattern("shutdown-classifying-state"));
    QTemporaryDir local(fixturePattern("shutdown-classifying-local"));
    ASSERT_TRUE(state.isValid() && local.isValid());
    const ScopedXdgStateHome stateHome(state.path());
    RecordingWarningSink warnings;
    const StateStore store(XdgPaths::stateFilePath(), XdgPaths::stateDirPath());
    ASSERT_TRUE(store.save("/previous", warnings));
    DirectoryController controller;
    controller.configureRestore(true);
    const auto classifier = std::make_shared<files_test::GatedLocationClassifier>();
    DirectoryControllerTestAccess::setLocationClassifier(controller, classifier);
    const auto release = qScopeGuard([&] { classifier->release.release(); });
    auto& model = DirectoryControllerTestAccess::model(controller);
    QSignalSpy succeeded(&model, &DirectoryModel::loadSucceeded);
    controller.open(local.path());
    ASSERT_TRUE(QTest::qWaitFor([&] { return classifier->entered.load() && !model.scanning(); }));
    ASSERT_TRUE(model.directoryError().isEmpty());
    ASSERT_EQ(succeeded.count(), 0);
    if (refresh) {
      model.refresh();
    }
    QSignalSpy finished(&controller, &DirectoryController::shutdownFinished);
    std::optional<QString> savedAtShutdown;
    QObject::connect(&controller, &DirectoryController::shutdownFinished,
                     [&] { savedAtShutdown = store.load(warnings).last_location; });
    controller.shutdown();
    EXPECT_EQ(finished.count(), 0);
    EXPECT_EQ(store.load(warnings).last_location, QStringLiteral("/previous"));
    classifier->release.release();
    ASSERT_TRUE(finished.wait(5000));
    EXPECT_EQ(succeeded.count(), 1);
    EXPECT_EQ(savedAtShutdown, local.path());
    EXPECT_TRUE(warnings.messages.isEmpty());
  }
}

TEST(DirectoryController, ActivateBookmarkNavigatesOnceTheDirectoryExists) {
  QTemporaryDir home(fixturePattern("bookmark-available"));
  ASSERT_TRUE(home.isValid());
  const files_test::ScopedXdgDataHome guard(home.path());
  QDir(home.path()).mkpath("holonight/holonight-files");
  writeFile(home, "holonight/holonight-files/places.toml",
            "version = 1\n[[bookmarks]]\npath = \"" + home.filePath("target").toUtf8() + "\"\n");
  DirectoryController controller;
  const auto row = findPlaceRow(*controller.places(), home.filePath("target"));
  ASSERT_GE(row, 0);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    return controller.places()
               ->data(controller.places()->index(row), PlacesModel::StatusRole)
               .value<PlacesModel::Status>() == PlacesModel::Status::Unavailable;
  }));
  ASSERT_TRUE(QDir(home.path()).mkdir("target"));
  controller.activateBookmark(row);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.currentPath() == home.filePath("target"); }));
  EXPECT_EQ(
      controller.places()->data(controller.places()->index(row), PlacesModel::StatusRole).value<PlacesModel::Status>(),
      PlacesModel::Status::Available);
  EXPECT_EQ(DirectoryControllerTestAccess::jumpList(controller).size(), 1);  // history gained an entry
}

TEST(DirectoryController, ActivateBookmarkShowsUnavailableMessageWithoutNavigatingWhenDirectoryMissing) {
  QTemporaryDir home(fixturePattern("bookmark-unavailable"));
  ASSERT_TRUE(home.isValid());
  const files_test::ScopedXdgDataHome guard(home.path());
  QDir(home.path()).mkpath("holonight/holonight-files");
  ASSERT_TRUE(QDir(home.path()).mkdir("target"));
  writeFile(home, "holonight/holonight-files/places.toml",
            "version = 1\n[[bookmarks]]\npath = \"" + home.filePath("target").toUtf8() + "\"\n");
  DirectoryController controller;
  const auto row = findPlaceRow(*controller.places(), home.filePath("target"));
  ASSERT_GE(row, 0);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    return controller.places()
               ->data(controller.places()->index(row), PlacesModel::StatusRole)
               .value<PlacesModel::Status>() == PlacesModel::Status::Available;
  }));
  const auto before = controller.currentPath();
  ASSERT_TRUE(QDir().rmdir(home.filePath("target")));
  controller.activateBookmark(row);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return controller.statusMessage() == QStringLiteral("Location is currently unavailable"); }));
  EXPECT_EQ(controller.currentPath(), before);
  EXPECT_EQ(
      controller.places()->data(controller.places()->index(row), PlacesModel::StatusRole).value<PlacesModel::Status>(),
      PlacesModel::Status::Unavailable);
}

TEST(DirectoryController, NavigationBetweenActivationAndResolutionSuppressesBookmarkSideEffect) {
  QTemporaryDir home(fixturePattern("bookmark-stale"));
  ASSERT_TRUE(home.isValid());
  const files_test::ScopedXdgDataHome guard(home.path());
  QDir(home.path()).mkpath("holonight/holonight-files");
  ASSERT_TRUE(QDir(home.path()).mkdir("target"));
  ASSERT_TRUE(QDir(home.path()).mkdir("elsewhere"));
  writeFile(home, "holonight/holonight-files/places.toml",
            "version = 1\n[[bookmarks]]\npath = \"" + home.filePath("target").toUtf8() + "\"\n");
  DirectoryController controller;
  ASSERT_TRUE(openSettled(controller, home.filePath("elsewhere")));
  const auto row = findPlaceRow(*controller.places(), home.filePath("target"));
  ASSERT_GE(row, 0);
  controller.activateBookmark(row);  // dispatches an async recheck, snapshotting navigation_serial_
  controller.open(home.path());      // a real navigation intervenes before that recheck can resolve
  ASSERT_TRUE(settled(controller));
  const auto pathAfterInterveningNavigation = controller.currentPath();
  QTest::qWait(200);  // give the async recheck time to actually resolve and (attempt to) deliver
  EXPECT_EQ(controller.currentPath(), pathAfterInterveningNavigation);
}

TEST(DirectoryController, TwoBookmarkActivationsAddNoWarnings) {
  QTemporaryDir home(fixturePattern("bookmark-warnings"));
  ASSERT_TRUE(home.isValid());
  const files_test::ScopedXdgDataHome guard(home.path());
  QDir(home.path()).mkpath("holonight/holonight-files");
  ASSERT_TRUE(QDir(home.path()).mkdir("target"));
  writeFile(home, "holonight/holonight-files/places.toml",
            "version = 1\n[[bookmarks]]\npath = \"" + home.filePath("target").toUtf8() + "\"\n");
  DirectoryController controller;
  files_test::RecordingWarningSink warnings;
  DirectoryControllerTestAccess::setWarningSink(controller,
                                                std::shared_ptr<WarningSink>(&warnings, [](WarningSink*) {}));
  const auto row = findPlaceRow(*controller.places(), home.filePath("target"));
  ASSERT_GE(row, 0);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    return controller.places()
               ->data(controller.places()->index(row), PlacesModel::StatusRole)
               .value<PlacesModel::Status>() != PlacesModel::Status::Checking;
  }));
  controller.activateBookmark(row);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.currentPath() == home.filePath("target"); }));
  controller.activateBookmark(row);
  QTest::qWait(50);
  EXPECT_TRUE(warnings.messages.isEmpty());
}

TEST(DirectoryController, BookmarkCompletionPreservesInterveningInteractionGuards) {
  for (const bool available : {false, true}) {
    for (const auto* key : {"v", "/", "i", " ", "D"}) {
      SCOPED_TRACE(key);
      SCOPED_TRACE(available);
      QTemporaryDir home(fixturePattern("bookmark-interaction"));
      ASSERT_TRUE(home.isValid());
      const files_test::ScopedXdgDataHome guard(home.path());
      ASSERT_TRUE(QDir(home.path()).mkpath("holonight/holonight-files"));
      ASSERT_TRUE(QDir(home.path()).mkdir("current"));
      ASSERT_FALSE(writeFile(home, "current/file.txt", "preview").isEmpty());
      if (available) {
        ASSERT_TRUE(QDir(home.path()).mkdir("target"));
      }
      writeFile(home, "holonight/holonight-files/places.toml",
                "version = 1\n[[bookmarks]]\npath = \"" + home.filePath("target").toUtf8() + "\"\n");
      DirectoryController controller;
      ASSERT_TRUE(openSettled(controller, home.filePath("current")));
      controller.handleKey("j");
      ASSERT_TRUE(quickLookReady(controller));
      const auto row = findPlaceRow(*controller.places(), home.filePath("target"));
      ASSERT_GE(row, 0);
      QSignalSpy resolved(controller.places(), &PlacesModel::bookmarkRecheckResolved);
      controller.activateBookmark(row);
      // Both operations precede queued result delivery, regardless of worker timing.
      ASSERT_TRUE(controller.handleKey(key));
      const auto mode = controller.vim()->currentMode();
      const auto quickLook = controller.quickLookOpen();
      const auto prompt = controller.tasks()->hasPrompt();
      ASSERT_TRUE(mode != VimModeController::Mode::Normal || quickLook || prompt);
      if (mode == VimModeController::Mode::Insert) {
        controller.vim()->setInsertText("unfinished-name.txt");
      }
      const auto status = controller.statusMessage();
      ASSERT_TRUE(QTest::qWaitFor([&] { return resolved.count() == 1; }));
      EXPECT_EQ(controller.currentPath(), home.filePath("current"));
      EXPECT_EQ(controller.statusMessage(), status);
      EXPECT_EQ(controller.vim()->currentMode(), mode);
      EXPECT_EQ(controller.quickLookOpen(), quickLook);
      EXPECT_EQ(controller.tasks()->hasPrompt(), prompt);
      if (mode == VimModeController::Mode::Insert) {
        EXPECT_EQ(controller.vim()->insertText(), "unfinished-name.txt");
      }
      EXPECT_EQ(controller.places()
                    ->data(controller.places()->index(row), PlacesModel::StatusRole)
                    .value<PlacesModel::Status>(),
                available ? PlacesModel::Status::Available : PlacesModel::Status::Unavailable);
    }
  }
}

TEST(DirectoryController, ShutdownIgnoresDuplicateWorkerCompletionsAndRepeatedRequests) {
  DirectoryController controller;
  QSignalSpy finished(&controller, &DirectoryController::shutdownFinished);
  controller.shutdown();
  auto& model = DirectoryControllerTestAccess::model(controller);
  emit model.shutdownFinished();
  emit model.shutdownFinished();
  emit model.shutdownFinished();
  EXPECT_EQ(finished.count(), 0);
  ASSERT_TRUE(QTest::qWaitFor([&] { return finished.count() == 1; }));
  controller.shutdown();
  emit model.shutdownFinished();
  QCoreApplication::processEvents();
  EXPECT_EQ(finished.count(), 1);
}

TEST(DirectoryController, ParentRowActivatesNavigateParent) {
  QTemporaryDir parent(fixturePattern("parent-nav"));
  ASSERT_TRUE(parent.isValid());
  ASSERT_TRUE(QDir(parent.path()).mkdir("child"));
  const auto childPath = QDir(parent.path()).filePath("child");
  writeFile(parent, "child/item.txt");
  DirectoryController controller;
  controller.open(childPath);
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_EQ(cursorName(controller), "..");
  EXPECT_TRUE(controller.listing()->data(controller.listing()->index(0, 0), DirectoryModel::IsParentRole).toBool());

  // 'l' key navigates to parent
  EXPECT_TRUE(controller.handleKey("l"));
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), parent.path());
  EXPECT_EQ(cursorName(controller), "child");

  // Re-enter child
  EXPECT_TRUE(controller.handleKey("l"));
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), childPath);
  EXPECT_EQ(controller.cursorRow(), 0);

  // 'Return' key navigates to parent
  EXPECT_TRUE(controller.handleKey("Return"));
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), parent.path());
  EXPECT_EQ(cursorName(controller), "child");

  // Re-enter child and click row 0 (openEntry(0))
  EXPECT_TRUE(controller.handleKey("l"));
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), childPath);
  controller.openEntry(0);
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), parent.path());
  EXPECT_EQ(cursorName(controller), "child");
}

TEST(DirectoryController, ParentRowProtectedFromOperations) {
  QTemporaryDir dir(fixturePattern("parent-ops"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "file.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_EQ(cursorName(controller), "..");

  // Rename keys (i / a) are no-ops
  EXPECT_TRUE(controller.handleKey("i"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_TRUE(controller.handleKey("a"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);

  // Trash key (D) is a no-op
  EXPECT_TRUE(controller.handleKey("D"));
  EXPECT_FALSE(controller.tasks()->hasPrompt());

  // Yank and cut chords (yy, dd) are no-ops
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_TRUE(controller.handleKey("d"));
  EXPECT_TRUE(controller.handleKey("d"));

  // Destination paste does nothing because clipboard is empty
  QTemporaryDir dst(fixturePattern("parent-ops-dst"));
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  EXPECT_FALSE(controller.tasks()->busy());
  EXPECT_EQ(controller.listing()->rowCount(), 1);  // only synthetic ".."
}

TEST(DirectoryController, ParentRowExcludedFromSearchAndQuickLook) {
  QTemporaryDir dir(fixturePattern("parent-search-ql"));
  ASSERT_TRUE(dir.isValid());
  writeFile(dir, "item.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_EQ(cursorName(controller), "..");

  // Space Quick Look on parent row is a no-op
  EXPECT_TRUE(controller.handleKey(" "));
  EXPECT_FALSE(controller.quickLookOpen());

  // Search query "." matches "item.txt" (row 1), never ".." (row 0)
  EXPECT_TRUE(controller.handleKey("/"));
  controller.updateSearchQuery(".");
  EXPECT_EQ(cursorName(controller), "item.txt");
  EXPECT_EQ(controller.cursorRow(), 1);
  controller.cancelSearchEditing();

  // Position cursor on row 1 ("item.txt")
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(cursorName(controller), "item.txt");

  // Search query ".." matches nothing (excludes parent row) so cursor does NOT jump to row 0
  EXPECT_TRUE(controller.handleKey("/"));
  controller.updateSearchQuery("..");
  EXPECT_EQ(cursorName(controller), "item.txt");
  EXPECT_EQ(controller.cursorRow(), 1);
  controller.cancelSearchEditing();
}

TEST(DirectoryController, RootDirectoryHasNoParentRow) {
  DirectoryController controller;
  controller.open(QStringLiteral("/"));
  ASSERT_TRUE(settled(controller));
  EXPECT_EQ(controller.currentPath(), QStringLiteral("/"));
  EXPECT_GT(controller.listing()->rowCount(), 0);
  for (int row = 0; row < controller.listing()->rowCount(); ++row) {
    const auto name =
        controller.listing()->data(controller.listing()->index(row, 0), DirectoryModel::NameRole).toString();
    EXPECT_NE(name, "..");
    EXPECT_FALSE(
        controller.listing()->data(controller.listing()->index(row, 0), DirectoryModel::IsParentRole).toBool());
  }
  // Navigating parent from root is a no-op
  EXPECT_TRUE(controller.handleKey("h"));
  EXPECT_EQ(controller.currentPath(), QStringLiteral("/"));
}
