#include "settings/files_preferences.h"

#include "directory_fixtures.h"
#include "settings_fixtures.h"

#include <QDir>
#include <QSaveFile>
#include <QSignalSpy>
#include <QTest>

#include <gtest/gtest.h>

namespace {
void save(const QString& path, const QByteArray& bytes) {
  ASSERT_TRUE(QDir().mkpath(QFileInfo(path).absolutePath()));
  QSaveFile file(path);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  ASSERT_EQ(file.write(bytes), bytes.size());
  ASSERT_TRUE(file.commit());
}
constexpr auto hidden = "[header]\nshow_close_button = false\n";
constexpr auto visible = "[header]\nshow_close_button = true\n";
}  // namespace

TEST(FilesPreferences, LayersTypesAndProvenance) {
  QTemporaryDir dir(files_test::fixturePattern("header-layers"));
  files_test::RecordingWarningSink warnings;
  const auto base = dir.filePath("config.toml");
  const auto profile = dir.filePath("config-labwc.toml");
  EXPECT_TRUE(AppSettings::defaults().header().showCloseButton());
  save(base, hidden);
  for (const auto& value : {QByteArray("[general]\nrestore_last_location = true\n"),
                            QByteArray("[header]\nshow_close_button = 1\n"), QByteArray("[broken")}) {
    save(profile, value);
    const auto settings = AppSettings::load(base, profile, warnings);
    EXPECT_FALSE(settings.header().showCloseButton());
    for (const auto& info : settings.settingInfo()) {
      if (info.key == "show_close_button") {
        EXPECT_EQ(info.source, SettingSource::ConfigFile);
      }
    }
  }
  save(base, visible);
  save(profile, hidden);
  const auto settings = AppSettings::load(base, profile, warnings);
  EXPECT_FALSE(settings.header().showCloseButton());
  for (const auto& info : settings.settingInfo()) {
    if (info.key == "show_close_button") {
      EXPECT_EQ(info.source, SettingSource::SessionOverride);
    }
  }
}

TEST(FilesPreferences, WatchesCreationReplacementDeletionAndPriorityWithoutStaleValues) {
  QTemporaryDir dir(files_test::fixturePattern("header-watch"));
  files_test::RecordingWarningSink warnings;
  const auto base = dir.filePath("missing/app/config.toml");
  const auto profile = dir.filePath("missing/app/config-labwc.toml");
  const auto fallback = dir.filePath("missing/app/config-other.toml");
  FilesPreferences preferences(base, "labwc:other", warnings);
  QSignalSpy changes(&preferences, &FilesPreferences::showCloseButtonChanged);
  save(base, hidden);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !preferences.showCloseButton(); }));
  save(base, visible);
  ASSERT_TRUE(QTest::qWaitFor([&] { return preferences.showCloseButton(); }));
  save(fallback, hidden);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !preferences.showCloseButton(); }));
  save(profile, "[header]\nshow_close_button = 'invalid'\n");
  ASSERT_TRUE(QTest::qWaitFor([&] { return preferences.showCloseButton(); }));
  save(profile, hidden);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !preferences.showCloseButton(); }));
  save(profile, "[broken");
  ASSERT_TRUE(QTest::qWaitFor([&] { return preferences.showCloseButton(); }));
  const auto count = changes.count();
  save(base, visible);
  QTest::qWait(250);
  EXPECT_EQ(changes.count(), count);
  ASSERT_TRUE(QDir(dir.filePath("missing")).removeRecursively());
  QTest::qWait(250);
  save(base, hidden);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !preferences.showCloseButton(); }));
  ASSERT_TRUE(QFile::remove(base));
  ASSERT_TRUE(QTest::qWaitFor([&] { return preferences.showCloseButton(); }));
}
