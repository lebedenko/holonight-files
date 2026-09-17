#include "initial_directory.h"

#include "directory_fixtures.h"
#include "restore_outcome.h"

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

TEST(InitialDirectory, RestoreOutcomeReasonsAreExact) {
  EXPECT_TRUE(restoreOutcomeReason(RestoreOutcome::Ok).isEmpty());
  EXPECT_EQ(restoreOutcomeReason(RestoreOutcome::DoesNotExist), "last location does not exist");
  EXPECT_EQ(restoreOutcomeReason(RestoreOutcome::NotDirectory), "last location is not a directory");
  EXPECT_EQ(restoreOutcomeReason(RestoreOutcome::NotReadable), "last location is not readable");
  EXPECT_EQ(restoreOutcomeReason(RestoreOutcome::NotLocal), "last location is not local");
}

TEST(InitialDirectory, PlanStartupPrecedence) {
  QTemporaryDir dir(files_test::fixturePattern("startup-plan"));
  ASSERT_TRUE(dir.isValid());
  const auto home = QStandardPaths::writableLocation(QStandardPaths::HomeLocation);
  const std::optional<QString> stored = dir.path();

  // REQ-F-011: the argument wins, with resolveInitialDirectory's unchanged result.
  const auto withArgument = planStartup({dir.filePath("missing")}, true, stored);
  ASSERT_TRUE(withArgument.resolved.has_value());
  EXPECT_EQ(withArgument.resolved->path, home);
  EXPECT_EQ(withArgument.resolved->fallback_reason, "path does not exist");
  EXPECT_TRUE(withArgument.pending_candidate_path.isEmpty());

  // REQ-F-015
  const auto disabled = planStartup({}, false, stored);
  ASSERT_TRUE(disabled.resolved.has_value());
  EXPECT_EQ(disabled.resolved->path, home);
  EXPECT_EQ(disabled.resolved->fallback_reason, "no folder given");

  // REQ-F-014's fifth reason
  const auto nothingStored = planStartup({}, true, std::nullopt);
  ASSERT_TRUE(nothingStored.resolved.has_value());
  EXPECT_EQ(nothingStored.resolved->path, home);
  EXPECT_EQ(nothingStored.resolved->fallback_reason, "no stored location");

  // REQ-F-016: the candidate is handed on untouched, even a path that no longer exists.
  const auto pending = planStartup({}, true, dir.filePath("gone"));
  EXPECT_FALSE(pending.resolved.has_value());
  EXPECT_EQ(pending.pending_candidate_path, dir.filePath("gone"));
}
