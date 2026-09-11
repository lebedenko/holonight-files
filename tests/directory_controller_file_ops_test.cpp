#include "directory_controller.h"
#include "directory_fixtures.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::ScopedXdgDataHome;
using files_test::writeFile;

namespace {
bool settled(const DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.scanning(); });
}
bool taskIdle(DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.tasks()->busy(); });
}
}  // namespace

TEST(DirectoryControllerFileOps, YyYanksCursorItemAndPPastesCopyIntoDestination) {
  QTemporaryDir src(fixturePattern("dc-yy-src"));
  QTemporaryDir dst(fixturePattern("dc-yy-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  writeFile(src, "a.txt", "payload");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_TRUE(controller.handleKey("y"));
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_TRUE(QFile::exists(src.filePath("a.txt")));  // copy: original remains
}

TEST(DirectoryControllerFileOps, DdCutsCursorItemAndPPastesMoveRemovingOriginal) {
  QTemporaryDir src(fixturePattern("dc-dd-src"));
  QTemporaryDir dst(fixturePattern("dc-dd-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  writeFile(src, "a.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("d"));
  EXPECT_TRUE(controller.handleKey("d"));
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_FALSE(QFile::exists(src.filePath("a.txt")));
  // REQ-F-008: register is cleared synchronously with the paste — a second p is a no-op.
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
}

TEST(DirectoryControllerFileOps, PastePlacesIntoCurrentDirectoryNotCursorSubdirectory) {
  QTemporaryDir src(fixturePattern("dc-paste-dest-src"));
  QTemporaryDir dst(fixturePattern("dc-paste-dest-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  writeFile(src, "a.txt");
  QDir(dst.path()).mkdir("subdir");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_TRUE(controller.handleKey("y"));
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  // Position the cursor on "subdir" (first entry, since it's the only one and dirs sort first).
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));          // pasted into dst itself
  EXPECT_FALSE(QFile::exists(dst.filePath("subdir/a.txt")));  // never into the subdirectory
}

TEST(DirectoryControllerFileOps, VisualYCopiesSelectionAndExitsToNormal) {
  QTemporaryDir src(fixturePattern("dc-visual-y-src"));
  QTemporaryDir dst(fixturePattern("dc-visual-y-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  writeFile(src, "a.txt");
  writeFile(src, "b.txt");
  writeFile(src, "c.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("v"));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.vim()->selectedCount(), 2);
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_TRUE(QFile::exists(dst.filePath("b.txt")));
  EXPECT_TRUE(QFile::exists(src.filePath("a.txt")));
  EXPECT_TRUE(QFile::exists(src.filePath("b.txt")));
}

TEST(DirectoryControllerFileOps, VisualDCutsSelectionAndExitsToNormal) {
  QTemporaryDir src(fixturePattern("dc-visual-d-src"));
  QTemporaryDir dst(fixturePattern("dc-visual-d-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  writeFile(src, "a.txt");
  writeFile(src, "b.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("v"));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_TRUE(controller.handleKey("d"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_TRUE(QFile::exists(dst.filePath("b.txt")));
  EXPECT_FALSE(QFile::exists(src.filePath("a.txt")));
  EXPECT_FALSE(QFile::exists(src.filePath("b.txt")));
}

TEST(DirectoryControllerFileOps, RegisterOverwriteReplacesPreviousContents) {
  QTemporaryDir src(fixturePattern("dc-register-overwrite"));
  ASSERT_TRUE(src.isValid());
  writeFile(src, "a.txt");
  writeFile(src, "b.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_TRUE(controller.handleKey("y"));  // yy on "a.txt" (first entry)
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_TRUE(controller.handleKey("y"));  // yy on "b.txt", no paste in between
  QTemporaryDir dst(fixturePattern("dc-register-overwrite-dst"));
  ASSERT_TRUE(dst.isValid());
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_FALSE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_TRUE(QFile::exists(dst.filePath("b.txt")));
}

TEST(DirectoryControllerFileOps, RegisterPersistsAcrossNavigation) {
  QTemporaryDir src(fixturePattern("dc-register-persist-src"));
  QTemporaryDir other(fixturePattern("dc-register-persist-other"));
  QTemporaryDir dst(fixturePattern("dc-register-persist-dst"));
  ASSERT_TRUE(src.isValid() && other.isValid() && dst.isValid());
  writeFile(src, "a.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_TRUE(controller.handleKey("y"));
  controller.open(other.path());
  ASSERT_TRUE(settled(controller));
  controller.toggleHidden();
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));
}

TEST(DirectoryControllerFileOps, StrayKeyBetweenYPressesPreventsFalseChordMatch) {
  QTemporaryDir src(fixturePattern("dc-chord-hygiene-src"));
  QTemporaryDir dst(fixturePattern("dc-chord-hygiene-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  writeFile(src, "a.txt");
  writeFile(src, "b.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("y"));  // arms pending_y_ on "a.txt" (row 0)
  EXPECT_TRUE(controller.handleKey("j"));  // unrelated key: must clear pending_y_, moves to row 1
  EXPECT_TRUE(controller.handleKey("y"));  // a fresh chord start, not a false match with the first "y"
  EXPECT_TRUE(controller.handleKey("y"));  // completes on the *current* cursor row: "b.txt"
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_FALSE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_TRUE(QFile::exists(dst.filePath("b.txt")));
}

TEST(DirectoryControllerFileOps, DInNormalRequestsTrashConfirmationForCursorItem) {
  QTemporaryDir home(fixturePattern("dc-trash-normal-home"));
  QTemporaryDir src(fixturePattern("dc-trash-normal-src"));
  ASSERT_TRUE(home.isValid() && src.isValid());
  const ScopedXdgDataHome guard(home.path());
  writeFile(src, "a.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("D"));
  ASSERT_TRUE(controller.tasks()->hasPrompt());
  EXPECT_EQ(controller.tasks()->promptKind(), TaskManager::PromptKind::TrashConfirm);
  EXPECT_TRUE(controller.handleKey("y"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_FALSE(QFile::exists(src.filePath("a.txt")));
}

TEST(DirectoryControllerFileOps, DInNormalDeclineLeavesFileInPlace) {
  QTemporaryDir home(fixturePattern("dc-trash-decline-home"));
  QTemporaryDir src(fixturePattern("dc-trash-decline-src"));
  ASSERT_TRUE(home.isValid() && src.isValid());
  const ScopedXdgDataHome guard(home.path());
  writeFile(src, "a.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("D"));
  EXPECT_TRUE(controller.handleKey("n"));
  EXPECT_FALSE(controller.tasks()->hasPrompt());
  EXPECT_TRUE(QFile::exists(src.filePath("a.txt")));
}

TEST(DirectoryControllerFileOps, VisualDTrashesEntireSelectionAfterOneConfirmation) {
  QTemporaryDir home(fixturePattern("dc-trash-visual-home"));
  QTemporaryDir src(fixturePattern("dc-trash-visual-src"));
  ASSERT_TRUE(home.isValid() && src.isValid());
  const ScopedXdgDataHome guard(home.path());
  writeFile(src, "a.txt");
  writeFile(src, "b.txt");
  writeFile(src, "c.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("v"));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_TRUE(controller.handleKey("D"));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  ASSERT_TRUE(controller.tasks()->hasPrompt());
  EXPECT_EQ(controller.tasks()->trashConfirmCount(), 3);
  EXPECT_TRUE(controller.handleKey("y"));
  ASSERT_TRUE(taskIdle(controller));
  for (const auto* name : {"a.txt", "b.txt", "c.txt"}) {
    EXPECT_FALSE(QFile::exists(src.filePath(name)));
  }
}

TEST(DirectoryControllerFileOps, PromptCapturesKeysExclusivelyUntilResolved) {
  QTemporaryDir src(fixturePattern("dc-prompt-conflict-src"));
  QTemporaryDir dst(fixturePattern("dc-prompt-conflict-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  writeFile(src, "file.txt", "NEW");
  writeFile(dst, "file.txt", "OLD");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_TRUE(controller.handleKey("y"));
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.tasks()->hasPrompt(); }));
  // Ordinary navigation keys are captured (swallowed), not routed to the listing, while the
  // conflict prompt is open.
  const int cursorBefore = controller.cursorRow();
  EXPECT_TRUE(controller.handleKey("j"));
  EXPECT_EQ(controller.cursorRow(), cursorBefore);
  EXPECT_TRUE(controller.handleKey("x"));  // not one of s/o/r/c: swallowed, no effect on the task
  EXPECT_TRUE(controller.tasks()->hasPrompt());
  EXPECT_TRUE(controller.handleKey("o"));  // now resolve as overwrite
  ASSERT_TRUE(taskIdle(controller));
  QFile destFile(dst.filePath("file.txt"));
  ASSERT_TRUE(destFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(destFile.readAll(), QByteArray("NEW"));
}

TEST(DirectoryControllerFileOps, EscapeDuringPromptDeclinesLikeAnyOtherKey) {
  QTemporaryDir home(fixturePattern("dc-prompt-escape-home"));
  QTemporaryDir src(fixturePattern("dc-prompt-escape-src"));
  ASSERT_TRUE(home.isValid() && src.isValid());
  const ScopedXdgDataHome guard(home.path());
  writeFile(src, "a.txt");
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("D"));
  ASSERT_TRUE(controller.tasks()->hasPrompt());
  EXPECT_TRUE(controller.handleKey("Escape"));  // REQ-C-008: not special-cased, declines like "n"
  EXPECT_FALSE(controller.tasks()->hasPrompt());
  EXPECT_TRUE(QFile::exists(src.filePath("a.txt")));
}

TEST(DirectoryControllerFileOps, YankInterruptsPendingCutChord) {
  QTemporaryDir src(fixturePattern("dc-interrupted-cut-src"));
  QTemporaryDir dst(fixturePattern("dc-interrupted-cut-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  ASSERT_FALSE(writeFile(src, "a.txt").isEmpty());
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("d"));
  EXPECT_TRUE(controller.handleKey("y"));
  EXPECT_TRUE(controller.handleKey("d"));
  controller.open(dst.path());
  ASSERT_TRUE(settled(controller));
  EXPECT_TRUE(controller.handleKey("p"));
  ASSERT_TRUE(taskIdle(controller));
  EXPECT_TRUE(QFile::exists(src.filePath("a.txt")));
  EXPECT_FALSE(QFile::exists(dst.filePath("a.txt")));
}
