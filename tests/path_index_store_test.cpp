#include "path_index_store.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <gtest/gtest.h>

namespace {
QVector<PathCandidate> records(const QString& root, const QString& name = QStringLiteral("project/needle.txt")) {
  return {
      {.path = root + "/project", .relative_path = "project", .directory = true},
      {.path = root + u'/' + name, .relative_path = name, .directory = false},
  };
}
bool load(const QString& path, const QString& root, const QByteArray& policy, QVector<PathCandidate>& out) {
  const std::atomic_bool cancelled = false;
  qint64 completed = 0;
  return PathIndexStore::load(
      path, root, policy, cancelled, [&](const QVector<PathCandidate>& batch) { out += batch; }, completed);
}
}  // namespace

TEST(PathIndexStore, RoundTripFilesAndDirectoriesAndPolicyMismatch) {
  QTemporaryDir dir;
  const auto path = dir.filePath("snapshot");
  const QString root = "/home/test";
  std::atomic_bool cancelled = false;
  {
    PathIndexStore writer(path, root, "policy");
    writer.append(records(root));
    ASSERT_TRUE(writer.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
  }
  QVector<PathCandidate> restored;
  ASSERT_TRUE(load(path, root, "policy", restored));
  ASSERT_EQ(restored.size(), 2);
  EXPECT_TRUE(restored[0].directory);
  EXPECT_FALSE(restored[1].directory);
  EXPECT_EQ(restored[1].path, root + "/project/needle.txt");
  EXPECT_FALSE(load(path, "/other", "policy", restored));
  EXPECT_FALSE(load(path, root, "changed", restored));
}

TEST(PathIndexStore, ConcurrentWritersReplaceOnlyWithCompleteSnapshots) {
  QTemporaryDir dir;
  const auto path = dir.filePath("snapshot");
  const QString root = "/home/test";
  std::atomic_bool cancelled = false;
  PathIndexStore first(path, root, "policy");
  PathIndexStore second(path, root, "policy");
  first.append(records(root, "first.txt"));
  second.append(records(root, "second.txt"));
  EXPECT_FALSE(QFile::exists(path));
  ASSERT_TRUE(second.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
  QVector<PathCandidate> restored;
  ASSERT_TRUE(load(path, root, "policy", restored));
  EXPECT_EQ(restored.last().relative_path, "second.txt");
  ASSERT_TRUE(first.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
  restored.clear();
  ASSERT_TRUE(load(path, root, "policy", restored));
  EXPECT_EQ(restored.last().relative_path, "first.txt");
}

TEST(PathIndexStore, CancelledGenerationPreservesPreviousSnapshotAndWriteFailureIsNonfatal) {
  QTemporaryDir dir;
  const auto path = dir.filePath("snapshot");
  std::atomic_bool cancelled = false;
  PathIndexStore first(path, "/root", "policy");
  first.append(records("/root"));
  ASSERT_TRUE(first.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
  PathIndexStore second(path, "/root", "policy");
  second.append(records("/root", "obsolete.txt"));
  cancelled.store(true);
  EXPECT_FALSE(second.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
  QVector<PathCandidate> restored;
  ASSERT_TRUE(load(path, "/root", "policy", restored));
  EXPECT_EQ(restored.last().relative_path, "project/needle.txt");
  PathIndexStore failure(dir.path(), "/root", "policy");
  cancelled.store(false);
  EXPECT_FALSE(failure.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
}

TEST(PathIndexStore, RejectsTruncationCorruptionAndTrailingData) {
  QTemporaryDir dir;
  const auto path = dir.filePath("snapshot");
  std::atomic_bool cancelled = false;
  PathIndexStore writer(path, "/root", "policy");
  writer.append(records("/root"));
  ASSERT_TRUE(writer.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const auto original = file.readAll();
  file.close();
  for (int mode = 0; mode < 3; ++mode) {
    auto damaged = original;
    if (mode == 0) {
      damaged.chop(10);
    }
    if (mode == 1) {
      damaged[damaged.size() - 1] ^= 1;
    }
    if (mode == 2) {
      damaged.append('x');
    }
    ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    ASSERT_EQ(file.write(damaged), damaged.size());
    file.close();
    QVector<PathCandidate> restored;
    EXPECT_FALSE(load(path, "/root", "policy", restored));
  }
}

TEST(PathIndexStore, XdgCacheRequiresAbsolutePath) {
  const bool wasSet = qEnvironmentVariableIsSet("XDG_CACHE_HOME");
  const auto previous = qgetenv("XDG_CACHE_HOME");
  for (const QByteArray& value : {QByteArray(), QByteArray("relative")}) {
    qputenv("XDG_CACHE_HOME", value);
    EXPECT_EQ(PathIndexStore::cachePath(), QDir::homePath() + "/.cache/holonight-files/search/home-v1.index");
  }
  qunsetenv("XDG_CACHE_HOME");
  EXPECT_EQ(PathIndexStore::cachePath(), QDir::homePath() + "/.cache/holonight-files/search/home-v1.index");
  qputenv("XDG_CACHE_HOME", "/tmp/cache-test");
  EXPECT_EQ(PathIndexStore::cachePath(), "/tmp/cache-test/holonight-files/search/home-v1.index");
  if (wasSet) {
    qputenv("XDG_CACHE_HOME", previous);
  } else {
    qunsetenv("XDG_CACHE_HOME");
  }
}

TEST(PathIndexStore, RejectsInvalidRelativePathsAndInvalidEntryTypes) {
  QTemporaryDir dir;
  const auto path = dir.filePath("snapshot");
  const std::atomic_bool cancelled = false;
  for (const QString& relative : {
           QStringLiteral("."),
           QStringLiteral("../escape"),
           QStringLiteral("/absolute"),
           QStringLiteral("folder/../escape"),
       }) {
    PathIndexStore writer(path, "/root", "policy");
    writer.append({{.path = "/root/ignored", .relative_path = relative, .directory = false}});
    ASSERT_TRUE(writer.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
    QVector<PathCandidate> restored;
    EXPECT_FALSE(load(path, "/root", "policy", restored));
  }
}

TEST(PathIndexStore, RejectsInvalidTypeEvenWithValidIntegrity) {
  QTemporaryDir dir;
  const auto path = dir.filePath("snapshot");
  const std::atomic_bool cancelled = false;
  PathIndexStore writer(path, "/root", "policy");
  writer.append({{.path = "/root/needle", .relative_path = "needle", .directory = false}});
  ASSERT_TRUE(writer.commit(QDateTime::currentMSecsSinceEpoch(), cancelled));
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  auto bytes = file.readAll();
  file.close();
  // One-byte entry type immediately precedes the 12-byte footer and 32-byte digest.
  bytes[bytes.size() - 45] = 2;
  bytes.chop(32);
  bytes.append(QCryptographicHash::hash(bytes, QCryptographicHash::Sha256));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
  ASSERT_EQ(file.write(bytes), bytes.size());
  file.close();
  QVector<PathCandidate> restored;
  EXPECT_FALSE(load(path, "/root", "policy", restored));
}
