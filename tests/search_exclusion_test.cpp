#include "directory_fixtures.h"
#include "path_finder_model.h"
#include "path_scanner.h"
#include "settings_fixtures.h"

#include <QSemaphore>
#include <QTest>

#include <gtest/gtest.h>

using files_test::ScopedEnvironmentVariable;
using files_test::ScopedXdgConfigHome;

namespace {
QSet<QString> scan(const QString& root, bool hidden, const SearchExclusionPolicy& policy) {
  QSet<QString> paths;
  const std::atomic_bool cancelled = false;
  EXPECT_TRUE(scanPaths(root, hidden, policy, cancelled, [&](const QVector<PathCandidate>& batch) {
    for (const auto& candidate : batch) {
      paths.insert(candidate.path);
    }
  }));
  return paths;
}
void writeConfig(const QString& path, const QByteArray& content) {
  QDir().mkpath(QFileInfo(path).absolutePath());
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(content), content.size());
}
}  // namespace

TEST(SearchExclusions, StringListsValidateAsWholeValues) {
  for (const QByteArray& content :
       {QByteArray("[search]\nexclude_paths = [1, \"/ok\"]"), QByteArray("[search]\nexclude_paths = true")}) {
    SettingsRegistry registry;
    SearchSettings::declare(registry);
    const auto parsed = TomlDocument::parse(content);
    ASSERT_TRUE(parsed.diagnostics.empty());
    EXPECT_EQ(registry.apply(parsed.document).size(), 1U);
    EXPECT_TRUE(SearchSettings::read(registry).paths.isEmpty());
  }
  SettingsRegistry registry;
  SearchSettings::declare(registry);
  EXPECT_TRUE(
      registry
          .apply(TomlDocument::parse("[search]\nexclude_paths = [\"/café space\"]\nexclude_directory_patterns = []")
                     .document)
          .empty());
  EXPECT_EQ(SearchSettings::read(registry).paths, QStringList{"/café space"});
  EXPECT_TRUE(SearchSettings::read(registry).patterns.isEmpty());
}

TEST(SearchExclusions, BuiltinsUserUnionBoundariesAndLiteralWildcards) {
  QStringList diagnostics;
  const ScopedEnvironmentVariable home("HOME", QStringLiteral("/test-home"));
  const ScopedEnvironmentVariable cache("XDG_CACHE_HOME", QStringLiteral("/xdg/cache"));
  const ScopedEnvironmentVariable data("XDG_DATA_HOME", QStringLiteral("/xdg/data"));
  const auto policy = SearchExclusionPolicy::compile(
      {
          .paths =
              {
                  "~/large data",
                  "/nonexistent/café",
                  "/archive",
                  "/literal~name",
                  "relative",
                  "$HOME/cache",
                  "~other/cache",
              },
          .patterns = {"vendor", "dist?", "[literal]", "café *", "bad/path"},
      },
      diagnostics);
  EXPECT_EQ(diagnostics.size(), 4);
  for (const QString& path : {
           "/test-home/.cache/item",
           "/xdg/cache/item",
           "/xdg/data/Trash/item",
           "/test-home/large data/item",
           "/nonexistent/café/item",
           "/archive/item",
           "/literal~name/item",
       }) {
    EXPECT_TRUE(policy.excludes(path, false, "/")) << path.toStdString();
  }
  for (const QString& name : {
           ".git",
           ".venv",
           ".codex",
           ".claude",
           ".agents",
           "build",
           "build-debug",
           "node_modules",
           "__pycache__",
           "vendor",
           "dist1",
           "[literal]",
           "café data",
       }) {
    EXPECT_TRUE(policy.excludes("/nested/" + name, true, "/")) << name.toStdString();
    EXPECT_FALSE(policy.excludes("/nested/" + name, false, "/"));
  }
  for (const QString& path : {
           "/archive2",
           "/xdg/cache2",
           "/test-home/.config",
           "/test-home/.local/share",
           "/test-home/.local/state",
           "/nested/Build",
           "/nested/literal",
           "/nested/dist12",
       }) {
    EXPECT_FALSE(policy.excludes(path, true, "/")) << path.toStdString();
  }
}

TEST(SearchExclusions, EquivalentRulesAndXdgFallbacks) {
  const ScopedEnvironmentVariable home("HOME", QStringLiteral("/test-home"));
  const ScopedEnvironmentVariable cache("XDG_CACHE_HOME", QStringLiteral("relative"));
  const ScopedEnvironmentVariable data("XDG_DATA_HOME", QString{});
  QStringList diagnostics;
  const auto defaults = SearchExclusionPolicy::compile(SearchSettings(), diagnostics);
  const auto redundant =
      SearchExclusionPolicy::compile({.paths = {"~/folder/../.cache"}, .patterns = {".git", "build*"}}, diagnostics);
  EXPECT_EQ(defaults, redundant);
  EXPECT_TRUE(defaults.excludes("/test-home/.cache/item", false, "/"));
  EXPECT_TRUE(defaults.excludes("/test-home/.local/share/Trash/item", false, "/"));
}

TEST(SearchExclusions, TraversalPrunesBeforeEmissionAndAllowsExplicitRoots) {
  QTemporaryDir directory;
  ASSERT_TRUE(directory.isValid());
  for (const QString& folder :
       {".config", ".git/sub/build-child", "nested/.venv", "build-debug", "archive/sub/vendor", "archive2"}) {
    ASSERT_TRUE(QDir().mkpath(directory.filePath(folder)));
    ASSERT_FALSE(files_test::writeFile(directory, folder + "/needle.txt").isEmpty());
  }
  QStringList diagnostics;
  const auto policy =
      SearchExclusionPolicy::compile({.paths = {directory.filePath("archive")}, .patterns = {"vendor"}}, diagnostics);
  const auto visible = scan(directory.path(), false, policy);
  const auto hidden = scan(directory.path(), true, policy);
  EXPECT_FALSE(visible.contains(directory.filePath(".config/needle.txt")));
  EXPECT_TRUE(hidden.contains(directory.filePath(".config/needle.txt")));
  EXPECT_TRUE(hidden.contains(directory.filePath("archive2/needle.txt")));
  for (const QString& path : hidden) {
    EXPECT_FALSE(path.contains("/.git"));
    EXPECT_FALSE(path.contains("/.venv"));
    EXPECT_FALSE(path.contains("/build-debug"));
    EXPECT_FALSE(path.contains("/archive/"));
  }
  ASSERT_FALSE(files_test::writeFile(directory, ".git/sub/needle.txt").isEmpty());
  EXPECT_TRUE(scan(directory.filePath(".git"), true, policy).contains(directory.filePath(".git/sub/needle.txt")));
  const auto descendant = scan(directory.filePath(".git/sub"), true, policy);
  EXPECT_TRUE(descendant.contains(directory.filePath(".git/sub/needle.txt")));
  EXPECT_FALSE(descendant.contains(directory.filePath(".git/sub/build-child")));
  ASSERT_FALSE(files_test::writeFile(directory, "archive/sub/needle.txt").isEmpty());
  EXPECT_TRUE(scan(directory.filePath("archive"), true, policy).contains(directory.filePath("archive/sub/needle.txt")));
  const auto explicitPath = scan(directory.filePath("archive/sub"), true, policy);
  EXPECT_TRUE(explicitPath.contains(directory.filePath("archive/sub/needle.txt")));
  EXPECT_FALSE(explicitPath.contains(directory.filePath("archive/sub/vendor")));
}

TEST(SearchExclusions, ReopenReloadInvalidatesBothCachesAndRetainsLastGoodPolicy) {
  QTemporaryDir config;
  QTemporaryDir directory;
  const ScopedXdgConfigHome configHome(config.path());
  const QString configPath = config.filePath("holonight-files/config.toml");
  ASSERT_TRUE(QDir().mkpath(directory.filePath("vendor")));
  ASSERT_FALSE(files_test::writeFile(directory, "vendor/needle.txt").isEmpty());
  std::atomic_int scans = 0;
  PathFinderModel model([&](const QString& root, bool hidden, const SearchExclusionPolicy& policy,
                            const std::atomic_bool& cancel, const PathBatchReady& ready) {
    ++scans;
    return scanPaths(root, hidden, policy, cancel, ready);
  });
  auto open = [&] {
    model.start(directory.path(), false);
    return QTest::qWaitFor([&] { return !model.scanning(); });
  };
  ASSERT_TRUE(open());
  model.setIncludeHidden(true);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !model.scanning(); }));
  EXPECT_EQ(scans, 2);
  model.stop();
  ASSERT_TRUE(open());
  EXPECT_EQ(scans, 2);
  writeConfig(configPath, "[search]\nexclude_directory_patterns = [\"vendor\"]");
  model.stop();
  ASSERT_TRUE(open());
  EXPECT_EQ(model.indexedCount(), 0);
  model.setIncludeHidden(false);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !model.scanning(); }));
  EXPECT_EQ(model.indexedCount(), 0);
  EXPECT_EQ(scans, 4);
  writeConfig(configPath, "[search]\nexclude_directory_patterns = [\"vendor\", \"vendor\", \".git\"]");
  model.stop();
  ASSERT_TRUE(open());
  EXPECT_EQ(scans, 4);
  writeConfig(configPath, "[search\ninvalid");
  model.stop();
  ASSERT_TRUE(open());
  EXPECT_EQ(scans, 4);
  EXPECT_EQ(model.indexedCount(), 0);
  EXPECT_FALSE(model.error().isEmpty());
  model.setQuery("needle");
  EXPECT_FALSE(model.error().isEmpty());
  // Removing the file restores built-ins, without creating it on reopen.
  ASSERT_TRUE(QFile::remove(configPath));
  model.stop();
  ASSERT_TRUE(open());
  EXPECT_EQ(scans, 5);
  EXPECT_EQ(model.indexedCount(), 2);
  EXPECT_TRUE(model.error().isEmpty());
  EXPECT_FALSE(QFile::exists(configPath));
}

TEST(SearchExclusions, ChangedPolicyRejectsLateScanCallbacks) {
  QTemporaryDir config;
  const ScopedXdgConfigHome configHome(config.path());
  QSemaphore entered;
  QSemaphore release;
  std::atomic_int scans = 0;
  PathFinderModel model([&](const QString& root, bool, const SearchExclusionPolicy&, const std::atomic_bool&,
                            const PathBatchReady& ready) {
    if (++scans == 1) {
      entered.release();
      release.acquire();
      ready({{.path = root + "/obsolete", .relative_path = "obsolete", .directory = false}});
    }
    return true;
  });
  model.start("/synthetic", false);
  const bool started = entered.tryAcquire(1, 5000);
  if (!started) {
    release.release();
    FAIL() << "first scan did not start";
  }
  writeConfig(config.filePath("holonight-files/config.toml"), "[search]\nexclude_paths = [\"/obsolete\"]");
  model.start("/synthetic", false);
  release.release();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !model.scanning() && scans == 2; }));
  model.setQuery("obsolete");
  EXPECT_TRUE(QTest::qWaitFor([&] { return model.rowCount() == 0 && model.indexedCount() == 0; }));
  model.stop();
  model.start("/synthetic", false);
  EXPECT_FALSE(model.scanning());
  EXPECT_EQ(model.indexedCount(), 0);
}

TEST(SearchExclusions, FirstMalformedConfigUsesBuiltinsAndUnreadableConfigRetainsPolicy) {
  QTemporaryDir config;
  QTemporaryDir directory;
  const ScopedXdgConfigHome configHome(config.path());
  const QString path = config.filePath("holonight-files/config.toml");
  ASSERT_TRUE(QDir().mkpath(directory.filePath("build-debug")));
  ASSERT_FALSE(files_test::writeFile(directory, "build-debug/needle.txt").isEmpty());
  ASSERT_FALSE(files_test::writeFile(directory, "needle.txt").isEmpty());
  writeConfig(path, "[search");
  PathFinderModel model;
  auto open = [&] {
    model.start(directory.path(), false);
    return QTest::qWaitFor([&] { return !model.scanning(); });
  };
  ASSERT_TRUE(open());
  EXPECT_EQ(model.indexedCount(), 1);
  EXPECT_FALSE(model.error().isEmpty());
  writeConfig(path, "[search]\nexclude_paths = [\"relative\", \"" + directory.filePath("needle.txt").toUtf8() +
                        "\"]\nexclude_directory_patterns = [\"bad/path\"]");
  model.stop();
  ASSERT_TRUE(open());
  EXPECT_EQ(model.indexedCount(), 0);
  EXPECT_TRUE(model.error().contains("relative"));
  EXPECT_TRUE(model.error().contains("bad/path"));
  ASSERT_TRUE(QFile::remove(path));
  ASSERT_TRUE(QDir().mkdir(path));
  model.stop();
  ASSERT_TRUE(open());
  EXPECT_EQ(model.indexedCount(), 0);
  EXPECT_FALSE(model.error().isEmpty());
  EXPECT_TRUE(model.error().contains("Retaining"));
}
