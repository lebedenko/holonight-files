#include "directory_controller.h"
#include "directory_fixtures.h"

#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

using files_test::fixturePattern;

namespace {
bool settled(const DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.scanning(); });
}
bool previewSettled(DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.preview()->busy(); }, 5000);
}
// A stand-in for genuine 5 MB camera photos: large enough (a few megapixels, uncompressed) that
// decoding is real work, without spending the test suite's time writing 100+ literal 5 MB files
// to a QTemporaryDir on every run. Memory-peak and on-screen frame-rate assertions belong to a
// manual profiling pass on target hardware (see TASKS.md T-025), not an offscreen unit test.
void writeLargeImage(const QTemporaryDir& dir, const QString& name) {
  QImage image(QSize(2400, 1600), QImage::Format_RGB32);
  image.fill(Qt::darkBlue);
  ASSERT_TRUE(image.save(dir.filePath(name), "PNG"));
}
}  // namespace

TEST(PreviewStress, RapidPagingThroughManyLargeImagesShowsOnlyTheFinalEntry) {
  QTemporaryDir dir(fixturePattern("preview-stress"));
  ASSERT_TRUE(dir.isValid());
  constexpr int kEntryCount = 30;
  for (int i = 0; i < kEntryCount; ++i) {
    writeLargeImage(dir, QStringLiteral("img-%1.png").arg(i, 3, 10, QLatin1Char('0')));
  }
  DirectoryController controller;
  controller.open(dir.path());
  ASSERT_TRUE(settled(controller));
  ASSERT_EQ(controller.listing()->rowCount(), kEntryCount);
  ASSERT_TRUE(previewSettled(controller));

  QElapsedTimer elapsed;
  elapsed.start();
  // Faster than any human keyboard-repeat rate: each handleKey("j") call dispatches (and, for
  // the ones already superseded, cancels) a worker job without waiting for the previous one to
  // finish decoding — exactly the "one-outstanding-job" discipline DESIGN.md calls for.
  for (int i = 0; i < kEntryCount - 1; ++i) {
    controller.handleKey("j");
  }
  ASSERT_TRUE(previewSettled(controller));
  // No backlog: settling after the burst takes a bounded amount of time proportional to one
  // decode, not kEntryCount decodes queued up behind each other.
  EXPECT_LT(elapsed.elapsed(), 5000);
  EXPECT_EQ(controller.cursorRow(), kEntryCount - 1);
  EXPECT_EQ(controller.preview()->name(), QStringLiteral("img-%1.png").arg(kEntryCount - 1, 3, 10, QLatin1Char('0')));
  EXPECT_TRUE(controller.preview()->hasImage());
}
