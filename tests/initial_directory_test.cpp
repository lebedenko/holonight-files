#include "initial_directory.h"

#include "directory_fixtures.h"

#include <QScopeGuard>
#include <QStandardPaths>

#include <gtest/gtest.h>

TEST(InitialDirectory, ValidAbsentMissingAndFileArguments) {
  QTemporaryDir dir(files_test::fixturePattern("startup"));
  ASSERT_TRUE(dir.isValid());
  const auto home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  EXPECT_EQ(resolveInitialDirectory({dir.path()}).path, dir.path());
  EXPECT_TRUE(resolveInitialDirectory({dir.path()}).fallback_reason.isEmpty());
  EXPECT_EQ(resolveInitialDirectory({}).path, home);
  EXPECT_EQ(resolveInitialDirectory({}).fallback_reason, "no folder given");
  EXPECT_EQ(resolveInitialDirectory({dir.filePath("missing")}).path, home);
  EXPECT_EQ(resolveInitialDirectory({dir.filePath("missing")}).fallback_reason, "path does not exist");
  const auto file = files_test::writeFile(dir, "file");
  ASSERT_FALSE(file.isEmpty());
  EXPECT_EQ(resolveInitialDirectory({file}).path, home);
  EXPECT_EQ(resolveInitialDirectory({file}).fallback_reason, "not a directory");
}

TEST(InitialDirectory, UnreadableArgumentFallsBackWithReason) {
  if (files_test::runningAsRoot()) {
    GTEST_SKIP() << "root bypasses chmod 000 permission checks";
  }
  QTemporaryDir dir(files_test::fixturePattern("startup-unreadable"));
  ASSERT_TRUE(dir.isValid());
  const auto restore = qScopeGuard([&] { ::chmod(QFile::encodeName(dir.path()).constData(), 0700); });
  ASSERT_EQ(::chmod(QFile::encodeName(dir.path()).constData(), 0), 0);
  const auto resolved = resolveInitialDirectory({dir.path()});
  EXPECT_EQ(resolved.path, QStandardPaths::writableLocation(QStandardPaths::HomeLocation));
  EXPECT_EQ(resolved.fallback_reason, "permission denied");
}
