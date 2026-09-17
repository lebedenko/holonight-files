#include "places/place_availability_checker.h"

#include "directory_fixtures.h"
#include "settings_fixtures.h"

#include <QSemaphore>
#include <QTest>
#include <QThread>

#include <atomic>
#include <gtest/gtest.h>
#include <sys/stat.h>
#include <tuple>

using files_test::FakePlaceAvailabilityChecker;
using files_test::fixturePattern;
using files_test::runningAsRoot;
using files_test::writeFile;

TEST(PlaceAvailabilityChecker, StatCheckerDistinguishesDirectoriesFilesAndMissingPaths) {
  QTemporaryDir dir(fixturePattern("place-availability"));
  ASSERT_TRUE(dir.isValid());
  const auto filePath = writeFile(dir, "file.txt");
  ASSERT_FALSE(filePath.isEmpty());
  const StatPlaceAvailabilityChecker checker;
  EXPECT_TRUE(checker.isAvailable(dir.path()));
  EXPECT_FALSE(checker.isAvailable(dir.filePath("missing")));
  EXPECT_FALSE(checker.isAvailable(filePath));
  if (!runningAsRoot()) {
    const auto blocked = dir.filePath("blocked");
    QDir().mkpath(blocked);
    ::chmod(blocked.toLocal8Bit().constData(), 0);
    EXPECT_FALSE(checker.isAvailable(blocked));
    ::chmod(blocked.toLocal8Bit().constData(), 0700);
  }
}

TEST(PlaceAvailabilityChecker, FakeCheckerRecordsCallingThreadAndSupportsOverrides) {
  FakePlaceAvailabilityChecker fake;
  fake.overrides["/a"] = false;
  EXPECT_TRUE(fake.isAvailable("/b"));
  EXPECT_FALSE(fake.isAvailable("/a"));
  ASSERT_EQ(fake.paths().size(), 2);
  EXPECT_EQ(fake.paths(), QStringList({"/b", "/a"}));
  ASSERT_EQ(fake.threads().size(), 2);
  EXPECT_EQ(fake.threads().front(), QThread::currentThread());
}

TEST(PlaceAvailabilityChecker, FakeCheckerGateBlocksUntilReleased) {
  FakePlaceAvailabilityChecker fake;
  const auto gate = std::make_shared<QSemaphore>();
  fake.gates["/gated"] = gate;
  std::atomic_bool returned = false;
  QThread worker;
  QObject context;
  context.moveToThread(&worker);
  worker.start();
  QMetaObject::invokeMethod(
      &context,
      [&] {
        std::ignore = fake.isAvailable("/gated");
        returned = true;
      },
      Qt::QueuedConnection);
  QTest::qWait(50);
  EXPECT_FALSE(returned.load());
  gate->release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return returned.load(); }));
  worker.quit();
  worker.wait();
}
