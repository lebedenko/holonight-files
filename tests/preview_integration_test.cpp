#include "directory_controller.h"
#include "directory_fixtures.h"
#include "preview_fixtures.h"
#include "preview_service_test_access.h"

#include <QElapsedTimer>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSemaphore>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeJpegWithExif;
using files_test::writeLargeText;
using files_test::writeSmallText;
using files_test::writeUtf8Multilang;

namespace {
bool settled(const DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.scanning(); });
}
bool previewSettled(DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.preview()->quickLookBusy(); }, 5000);
}
}  // namespace

TEST(PreviewIntegration, CursorMovementThroughMixedFileTypesUpdatesThePreviewLive) {
  QTemporaryDir dir(fixturePattern("preview-mixed"));
  ASSERT_TRUE(dir.isValid());
  writeJpegWithExif(dir, "01-photo.jpg");
  writeSmallText(dir, "02-notes.txt");
  writeLargeText(dir, "03-log.txt");
  writeUtf8Multilang(dir, "04-multilang.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_EQ(controller.listing()->rowCount(), 5);

  // Row 0 is .. (parent). Move to row 1 (01-photo.jpg).
  controller.handleKey("j");
  ASSERT_TRUE(previewSettled(controller));
  EXPECT_TRUE(controller.preview()->hasImage());
  EXPECT_TRUE(controller.preview()->exifPresent());
  EXPECT_FALSE(controller.preview()->hasText());

  controller.handleKey("j");
  ASSERT_TRUE(previewSettled(controller));
  controller.handleKey(" ");
  ASSERT_TRUE(previewSettled(controller));
  EXPECT_TRUE(controller.preview()->hasText());
  EXPECT_FALSE(controller.preview()->textTruncated());
  EXPECT_FALSE(controller.preview()->hasImage());
  controller.handleKey("Escape");

  controller.handleKey("j");
  ASSERT_TRUE(previewSettled(controller));
  controller.handleKey(" ");
  ASSERT_TRUE(previewSettled(controller));
  EXPECT_TRUE(controller.preview()->hasText());
  EXPECT_TRUE(controller.preview()->textTruncated());  // 03-log.txt is the ~200KB fixture
  controller.handleKey("Escape");

  controller.handleKey("j");
  ASSERT_TRUE(previewSettled(controller));
  controller.handleKey(" ");
  ASSERT_TRUE(previewSettled(controller));
  EXPECT_TRUE(controller.preview()->hasText());
  bool foundJapanese = false;
  auto* lines = controller.preview()->textLines();
  for (int row = 0; row < lines->rowCount(); ++row) {
    foundJapanese =
        foundJapanese ||
        lines->data(lines->index(row, 0), TextLineModel::LineTextRole).toString().contains(QStringLiteral("日本語"));
  }
  EXPECT_TRUE(foundJapanese);
}

TEST(PreviewIntegration, RevisitingAnAlreadyCachedEntryUpdatesWithinTheLatencyBudget) {
  QTemporaryDir dir(fixturePattern("preview-cache-latency"));
  ASSERT_TRUE(dir.isValid());
  writeJpegWithExif(dir, "photo.jpg");
  writeSmallText(dir, "notes.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("j");                // move to photo.jpg
  ASSERT_TRUE(previewSettled(controller));  // warm the cache for row 1

  controller.handleKey("j");  // move to notes.txt
  ASSERT_TRUE(previewSettled(controller));

  QElapsedTimer elapsed;
  elapsed.start();
  controller.handleKey("k");  // back to the already-decoded JPEG
  ASSERT_TRUE(previewSettled(controller));
  EXPECT_LT(elapsed.elapsed(), 500);  // REQ-NF-002's cached-path budget, generously bounded here
}

TEST(PreviewIntegration, QuickLookOpensPinnedToItsFileAndJMovesTheCurrentLineNotTheSelection) {
  QTemporaryDir dir(fixturePattern("preview-quicklook-flow"));
  ASSERT_TRUE(dir.isValid());
  writeJpegWithExif(dir, "01-photo.jpg");
  writeSmallText(dir, "02-notes.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("j");  // move to 01-photo.jpg
  ASSERT_TRUE(previewSettled(controller));

  ASSERT_TRUE(controller.handleKey(" "));
  ASSERT_TRUE(controller.quickLookOpen());
  EXPECT_TRUE(controller.preview()->hasImage());  // same PreviewService instance as the pane

  // j is consumed by Quick Look: the photo stays previewed, the cursor stays on row 1, and there is no
  // text line to move on an image.
  controller.handleKey("j");
  ASSERT_TRUE(controller.quickLookOpen());
  EXPECT_EQ(controller.cursorRow(), 1);
  EXPECT_EQ(controller.preview()->name(), QStringLiteral("01-photo.jpg"));
  EXPECT_TRUE(controller.preview()->hasImage());
  EXPECT_EQ(controller.preview()->currentLineIndex(), -1);

  ASSERT_TRUE(controller.handleKey("Escape"));
  EXPECT_FALSE(controller.quickLookOpen());
  EXPECT_EQ(controller.cursorRow(), 1);

  // After closing, j navigates the listing again; reopening on the text file shows its lines.
  controller.handleKey("j");
  ASSERT_TRUE(previewSettled(controller));
  EXPECT_EQ(controller.preview()->name(), QStringLiteral("02-notes.txt"));
  ASSERT_TRUE(controller.handleKey(" "));
  ASSERT_TRUE(controller.quickLookOpen());
  ASSERT_TRUE(previewSettled(controller));
  EXPECT_TRUE(controller.preview()->hasText());
  EXPECT_GT(controller.preview()->textLineCount(), 1);
  EXPECT_EQ(controller.preview()->currentLineIndex(), 0);
  controller.handleKey("j");
  EXPECT_EQ(controller.preview()->currentLineIndex(), 1);
  EXPECT_EQ(controller.cursorRow(), 2);
  EXPECT_EQ(controller.preview()->name(), QStringLiteral("02-notes.txt"));
  ASSERT_TRUE(controller.handleKey("Escape"));
  EXPECT_FALSE(controller.quickLookOpen());
}

TEST(PreviewIntegration, EmptyDirectoryShowsThePlaceholderWithNoDecodeAttempted) {
  QTemporaryDir dir(fixturePattern("preview-empty-dir"));
  ASSERT_TRUE(dir.isValid());
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  // Non-root empty directory previews its parent row, named after the parent folder, with no decode.
  EXPECT_TRUE(controller.preview()->hasEntry());
  EXPECT_EQ(controller.preview()->name(), QFileInfo(QFileInfo(dir.path()).absolutePath()).fileName());
  EXPECT_FALSE(controller.preview()->hasImage());
  EXPECT_FALSE(controller.preview()->busy());
}

TEST(PreviewIntegration, SelectedFileEditsPermissionsReplacementRenameAndDeletionRefreshInPlace) {
  QTemporaryDir dir(fixturePattern("preview-watch"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeSmallText(dir, "selected.txt");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  controller.handleKey("j");  // move to selected.txt
  ASSERT_TRUE(previewSettled(controller));
  const auto replaceText = [&](const QString& filePath, const QByteArray& bytes) {
    QFile file(filePath);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    EXPECT_EQ(file.write(bytes), bytes.size());
  };
  replaceText(path, "edited selected content");
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->size() == 23; }));
  ASSERT_TRUE(QFile::setPermissions(path, QFileDevice::ReadOwner));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->permissions() == "-r--------"; }));
  const auto replacement = dir.filePath("replacement.tmp");
  replaceText(replacement, "atomic replacement");
  ASSERT_EQ(::rename(QFile::encodeName(replacement).constData(), QFile::encodeName(path).constData()), 0);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->size() == 18; }));
  replaceText(path, "reattached watch works");
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->size() == 22; }));
  const auto renamed = dir.filePath("renamed.txt");
  ASSERT_TRUE(QFile::rename(path, renamed));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->name() == "renamed.txt"; }));
  ASSERT_TRUE(previewSettled(controller));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->quickLookEligible(); }));
  controller.handleKey(" ");
  EXPECT_TRUE(controller.quickLookOpen());
  ASSERT_TRUE(QFile::remove(renamed));
  const auto parentName = QFileInfo(QFileInfo(dir.path()).absolutePath()).fileName();
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->name() == parentName; }));
  EXPECT_FALSE(controller.quickLookOpen());
}

class PendingQuickLook : public testing::TestWithParam<bool> {};

TEST_P(PendingQuickLook, EarlySpaceOpensPinnedLoadingThenResolvesSupportedOrUnsupported) {
  QTemporaryDir dir(fixturePattern("pending-quicklook"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeBytes(dir, GetParam() ? "a.txt" : "a.md", "hello\nworld\n");
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  QSemaphore entered;
  QSemaphore release;
  const auto guard = qScopeGuard([&] { release.release(); });
  PreviewServiceTestAccess::beforeDispatch(*controller.preview(), [&] {
    entered.release();
    release.acquire();
  });
  controller.handleKey("j");
  ASSERT_TRUE(entered.tryAcquire(1, 2000));
  ASSERT_TRUE(controller.handleKey(" "));
  EXPECT_TRUE(controller.quickLookOpen());
  EXPECT_TRUE(controller.preview()->quickLookBusy());
  const auto row = controller.cursorRow();
  controller.handleKey("j");
  EXPECT_EQ(controller.cursorRow(), row);
  EXPECT_EQ(controller.preview()->currentLineIndex(), -1);
  release.release();
  ASSERT_TRUE(previewSettled(controller));
  EXPECT_TRUE(controller.quickLookOpen());
  EXPECT_EQ(controller.preview()->hasText(), GetParam());
  EXPECT_EQ(controller.preview()->quickLookErrorKind(),
            GetParam() ? PreviewService::PreviewErrorKind::None : PreviewService::PreviewErrorKind::Unsupported);
  controller.handleKey("Escape");
  EXPECT_FALSE(controller.quickLookOpen());
  EXPECT_EQ(controller.preview()->textLineCount(), 0);
}

INSTANTIATE_TEST_SUITE_P(Classification, PendingQuickLook, testing::Bool());
