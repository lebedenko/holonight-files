#include "capacity_probe.h"

#include "directory_fixtures.h"

#include <QStorageInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

// The only tests that touch a real filesystem's capacity: a private temp directory and a path that
// does not exist (device-actions SPEC REQ-C-006 keeps every other capacity test on a fake).
TEST(CapacityProbe, ReadableDirectoryReportsConsistentFigures) {
  const QTemporaryDir dir(files_test::fixturePattern("capacity-probe"));
  ASSERT_TRUE(dir.isValid());
  const auto capacity = StorageInfoCapacityProbe().measure(dir.path());
  ASSERT_TRUE(capacity.valid);
  EXPECT_GT(capacity.bytesTotal, 0U);
  EXPECT_LE(capacity.bytesAvailable, capacity.bytesTotal);
}

TEST(CapacityProbe, NonexistentPathIsInvalid) {
  const QTemporaryDir dir(files_test::fixturePattern("capacity-probe"));
  ASSERT_TRUE(dir.isValid());
  const auto capacity = StorageInfoCapacityProbe().measure(dir.filePath("missing/nowhere"));
  EXPECT_FALSE(capacity.valid);
  EXPECT_EQ(capacity.bytesAvailable, 0U);
  EXPECT_EQ(capacity.bytesTotal, 0U);
}

TEST(CapacityProbe, AvailableExcludesReservedBlocks) {
  const QTemporaryDir dir(files_test::fixturePattern("capacity-probe"));
  ASSERT_TRUE(dir.isValid());
  const QStorageInfo storage(dir.path());
  if (storage.bytesAvailable() == storage.bytesFree()) {
    GTEST_SKIP() << "no reserved blocks on this mount, so available and free coincide";
  }
  const auto capacity = StorageInfoCapacityProbe().measure(dir.path());
  ASSERT_TRUE(capacity.valid);
  // Both figures move under a live filesystem; the probe must track the unprivileged one.
  const auto available = static_cast<quint64>(storage.bytesAvailable());
  const auto free = static_cast<quint64>(storage.bytesFree());
  EXPECT_LT(
      capacity.bytesAvailable > available ? capacity.bytesAvailable - available : available - capacity.bytesAvailable,
      capacity.bytesAvailable > free ? capacity.bytesAvailable - free : free - capacity.bytesAvailable);
}
