#include "state/state_store.h"

#include "directory_fixtures.h"
#include "settings/xdg_paths.h"
#include "settings_fixtures.h"

#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QSaveFile>
#include <QScopeGuard>

#include <gtest/gtest.h>
#include <sys/stat.h>

using files_test::fixturePattern;
using files_test::RecordingWarningSink;
using files_test::runningAsRoot;
using files_test::ScopedXdgStateHome;

namespace {
StateStore storeUnder(const QString& stateHome) {
  const ScopedXdgStateHome state(stateHome);
  return {XdgPaths::stateFilePath(), XdgPaths::stateDirPath()};
}

QString writeState(const QString& stateHome, const QByteArray& content) {
  QDir().mkpath(stateHome + "/holonight-files");
  auto path = stateHome + "/holonight-files/state.toml";
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
    return {};
  }
  return path;
}

QByteArray readAll(const QString& path) {
  QFile file(path);
  return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray{};
}
}  // namespace

TEST(StateStore, SaveCreatesPrivateDirectoryAndRoundTrips) {
  QTemporaryDir dir(fixturePattern("state-roundtrip"));
  ASSERT_TRUE(dir.isValid());
  const auto stateHome = dir.filePath("nested/state");
  const auto store = storeUnder(stateHome);
  RecordingWarningSink warnings;
  const QString location = QStringLiteral("/home/user/Проекты/\"quoted\"");
  ASSERT_TRUE(store.save(location, warnings));
  EXPECT_TRUE(warnings.messages.isEmpty());
  struct stat info{};
  ASSERT_EQ(::stat(QFile::encodeName(stateHome + "/holonight-files").constData(), &info), 0);
  EXPECT_EQ(info.st_mode & 0777, 0700U);
  EXPECT_TRUE(readAll(stateHome + "/holonight-files/state.toml").startsWith("version = 1\n"));
  EXPECT_EQ(store.load(warnings).last_location, location);
  EXPECT_TRUE(warnings.messages.isEmpty());
}

TEST(StateStore, MissingStateIsSilentlyEmpty) {
  QTemporaryDir dir(fixturePattern("state-missing"));
  ASSERT_TRUE(dir.isValid());
  RecordingWarningSink warnings;
  EXPECT_FALSE(storeUnder(dir.path()).load(warnings).last_location.has_value());
  EXPECT_TRUE(warnings.messages.isEmpty());
}

TEST(StateStore, UnusableStateFilesWarnOnceAndYieldNoLocation) {
  const std::vector<QByteArray> bad = {
      "version = 1\n[navigation\nlast_location = \"/tmp\"\n",            // corrupt TOML
      "version = 999\n[navigation]\nlast_location = \"/tmp\"\n",         // wrong version
      "[navigation]\nlast_location = \"/tmp\"\n",                        // no version
      "version = 1\n[navigation]\nlast_location = \"relative/path\"\n",  // REQ-C-007
      "version = 1\n[navigation]\nlast_location = 7\n",                  // wrong type
      "version = 1\n",                                                   // no location
  };
  for (const auto& content : bad) {
    QTemporaryDir dir(fixturePattern("state-bad"));
    ASSERT_TRUE(dir.isValid());
    const auto path = writeState(dir.path(), content);
    RecordingWarningSink warnings;
    EXPECT_FALSE(storeUnder(dir.path()).load(warnings).last_location.has_value()) << content.toStdString();
    ASSERT_EQ(warnings.messages.size(), 1) << content.toStdString();
    EXPECT_TRUE(warnings.messages[0].startsWith(path + ": "));
  }
}

TEST(StateStore, UnreadableStateFileWarnsOnce) {
  if (runningAsRoot()) {
    GTEST_SKIP() << "root bypasses chmod 000 permission checks";
  }
  QTemporaryDir dir(fixturePattern("state-unreadable"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeState(dir.path(), "version = 1\n[navigation]\nlast_location = \"/tmp\"\n");
  ASSERT_EQ(::chmod(QFile::encodeName(path).constData(), 0), 0);
  RecordingWarningSink warnings;
  EXPECT_FALSE(storeUnder(dir.path()).load(warnings).last_location.has_value());
  EXPECT_EQ(warnings.messages.size(), 1);
}

// SPEC.md REQ-F-009/REQ-NF-001: an uncommitted QSaveFile never disturbs the existing state.
TEST(StateStore, AbortedWriteLeavesPreviousStateByteIdentical) {
  QTemporaryDir dir(fixturePattern("state-abort"));
  ASSERT_TRUE(dir.isValid());
  const auto store = storeUnder(dir.path());
  RecordingWarningSink warnings;
  ASSERT_TRUE(store.save("/previous", warnings));
  const auto path = dir.filePath("holonight-files/state.toml");
  const auto before = readAll(path);
  {
    QSaveFile file(path);
    ASSERT_TRUE(file.open(QIODevice::WriteOnly));
    file.write("version = 1\n[navigation]\nlast_location = \"/half");
    file.cancelWriting();
    EXPECT_FALSE(file.commit());
  }
  EXPECT_EQ(readAll(path), before);
  EXPECT_EQ(store.load(warnings).last_location, "/previous");
  EXPECT_EQ(QDir(dir.filePath("holonight-files")).entryList(QDir::Files | QDir::Hidden).size(), 1);
}

TEST(StateStore, WriteFailureWarnsOnceAndReturnsPromptly) {
  if (runningAsRoot()) {
    GTEST_SKIP() << "root bypasses chmod 0555 permission checks";
  }
  QTemporaryDir dir(fixturePattern("state-readonly"));
  ASSERT_TRUE(dir.isValid());
  const auto restore = qScopeGuard([&] { ::chmod(QFile::encodeName(dir.path()).constData(), 0700); });
  ASSERT_EQ(::chmod(QFile::encodeName(dir.path()).constData(), 0555), 0);
  RecordingWarningSink warnings;
  QElapsedTimer elapsed;
  elapsed.start();
  EXPECT_FALSE(storeUnder(dir.path()).save("/somewhere", warnings));
  EXPECT_LT(elapsed.elapsed(), 1000);
  ASSERT_EQ(warnings.messages.size(), 1);
  EXPECT_TRUE(warnings.messages[0].contains("cannot save state"));
}

// SPEC.md REQ-F-021: two instances, no coordination; the later save wins.
TEST(StateStore, LastSaveWins) {
  QTemporaryDir dir(fixturePattern("state-last-wins"));
  ASSERT_TRUE(dir.isValid());
  RecordingWarningSink warnings;
  const auto first = storeUnder(dir.path());
  const auto second = storeUnder(dir.path());
  ASSERT_TRUE(first.save("/first", warnings));
  ASSERT_TRUE(second.save("/second", warnings));
  EXPECT_EQ(first.load(warnings).last_location, "/second");
}
