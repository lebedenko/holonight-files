#include "settings/app_settings.h"

#include "directory_fixtures.h"
#include "settings/xdg_paths.h"
#include "settings_fixtures.h"

#include <QDir>
#include <QFileInfo>
#include <QThread>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::RecordingWarningSink;
using files_test::ScopedEnvironmentVariable;
using files_test::ScopedXdgConfigHome;

namespace {
QString writeConfig(const QString& configHome, const QByteArray& content) {
  QDir().mkpath(configHome + "/holonight-files");
  auto path = configHome + "/holonight-files/config.toml";
  QFile file(path);
  if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
    return {};
  }
  return path;
}
}  // namespace

TEST(AppSettings, DefaultsDisableRestore) {
  const auto settings = AppSettings::defaults();
  EXPECT_FALSE(settings.general().restoreLastLocation());
  ASSERT_EQ(settings.settingInfo().size(), 3U);
  EXPECT_EQ(settings.settingInfo()[0].key, "restore_last_location");
  EXPECT_EQ(settings.settingInfo()[0].source, SettingSource::Default);
}

TEST(AppSettings, MissingConfigUsesDefaultsSilentlyAndCreatesNothing) {
  QTemporaryDir dir(fixturePattern("config-missing"));
  ASSERT_TRUE(dir.isValid());
  const ScopedXdgConfigHome config(dir.path());
  RecordingWarningSink warnings;
  const auto settings = AppSettings::load(XdgPaths::configFilePath(), warnings);
  EXPECT_FALSE(settings.general().restoreLastLocation());
  EXPECT_TRUE(warnings.messages.isEmpty());
  EXPECT_TRUE(QDir(dir.path()).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden).isEmpty());
}

TEST(AppSettings, ReadsFromXdgConfigHome) {
  QTemporaryDir dir(fixturePattern("config-xdg"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(writeConfig(dir.path(), "[general]\nrestore_last_location = true\n").isEmpty());
  const ScopedXdgConfigHome config(dir.path());
  RecordingWarningSink warnings;
  const auto settings = AppSettings::load(XdgPaths::configFilePath(), warnings);
  EXPECT_TRUE(settings.general().restoreLastLocation());
  EXPECT_EQ(settings.settingInfo()[0].source, SettingSource::ConfigFile);
  EXPECT_TRUE(warnings.messages.isEmpty());
}

TEST(AppSettings, UnsetOrEmptyXdgConfigHomeReadsFromHomeConfig) {
  QTemporaryDir home(fixturePattern("config-home"));
  ASSERT_TRUE(home.isValid());
  ASSERT_FALSE(writeConfig(home.filePath(".config"), "[general]\nrestore_last_location = true\n").isEmpty());
  const ScopedEnvironmentVariable homeVariable("HOME", home.path());
  for (const std::optional<QString>& value : {std::optional<QString>{}, std::optional<QString>{QString{}}}) {
    const ScopedXdgConfigHome config(value);
    RecordingWarningSink warnings;
    EXPECT_EQ(XdgPaths::configFilePath(), home.filePath(".config/holonight-files/config.toml"));
    EXPECT_TRUE(AppSettings::load(XdgPaths::configFilePath(), warnings).general().restoreLastLocation());
  }
}

TEST(AppSettings, UnparseableConfigUsesDefaultsWithOneWarningNamingPathAndLine) {
  QTemporaryDir dir(fixturePattern("config-invalid"));
  ASSERT_TRUE(dir.isValid());
  const auto path =
      writeConfig(dir.path(), "[general]\nrestore_last_location = true\n\n[broken\nalso = [unterminated\nx = =\n");
  RecordingWarningSink warnings;
  const auto settings = AppSettings::load(path, warnings);
  EXPECT_FALSE(settings.general().restoreLastLocation());
  ASSERT_EQ(warnings.messages.size(), 1);
  EXPECT_TRUE(warnings.messages[0].startsWith(path + ":4: ")) << warnings.messages[0].toStdString();
  EXPECT_FALSE(warnings.messages[0].contains('\n'));
}

TEST(AppSettings, WrongTypeWarnsNamingKeyAndTypeAndUsesDefault) {
  QTemporaryDir dir(fixturePattern("config-wrong-type"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeConfig(dir.path(), "[general]\nrestore_last_location = \"yes\"\n");
  RecordingWarningSink warnings;
  const auto settings = AppSettings::load(path, warnings);
  EXPECT_FALSE(settings.general().restoreLastLocation());
  ASSERT_EQ(warnings.messages.size(), 1);
  EXPECT_TRUE(warnings.messages[0].contains("restore_last_location"));
  EXPECT_TRUE(warnings.messages[0].contains("expected a boolean"));
  EXPECT_TRUE(warnings.messages[0].startsWith(path + ":2: "));
}

TEST(AppSettings, UnknownEntriesWarnWhileKnownKeysApply) {
  QTemporaryDir dir(fixturePattern("config-unknown"));
  ASSERT_TRUE(dir.isValid());
  const auto path =
      writeConfig(dir.path(), "[general]\nrestore_last_location = true\nunknown_key = 1\n[unknown_section]\nkey = 2\n");
  RecordingWarningSink warnings;
  const auto settings = AppSettings::load(path, warnings);
  EXPECT_TRUE(settings.general().restoreLastLocation());
  ASSERT_EQ(warnings.messages.size(), 2);
  EXPECT_TRUE(warnings.messages[0].contains("[general] unknown_key"));
  EXPECT_TRUE(warnings.messages[1].contains("[unknown_section] key"));
}

// SPEC.md REQ-C-003: loading (even with warnings) never writes config.toml.
TEST(AppSettings, ExistingConfigIsNeverModified) {
  QTemporaryDir dir(fixturePattern("config-readonly"));
  ASSERT_TRUE(dir.isValid());
  const QByteArray content = "[general]\nrestore_last_location = true\nunknown = 1\n";
  const auto path = writeConfig(dir.path(), content);
  const auto before = QFileInfo(path).lastModified();
  QThread::msleep(20);
  RecordingWarningSink warnings;
  const auto settings = AppSettings::load(path, warnings);
  EXPECT_TRUE(settings.general().restoreLastLocation());
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_EQ(file.readAll(), content);
  EXPECT_EQ(QFileInfo(path).lastModified(), before);
  EXPECT_EQ(QDir(dir.filePath("holonight-files")).entryList(QDir::Files | QDir::Hidden).size(), 1);
}

TEST(AppSettings, SessionOverrideMergesIndividualKeysAndReplacesArrays) {
  QTemporaryDir dir;
  const auto base = writeConfig(dir.path(),
                                "[general]\nrestore_last_location = true\n[search]\nexclude_paths = [\"/base\"]\n"
                                "exclude_directory_patterns = [\"vendor\"]\n");
  const auto profile = dir.filePath("holonight-files/config-holonight.toml");
  QFile file(profile);
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("[search]\nexclude_paths = []\n");
  file.close();
  const ScopedEnvironmentVariable desktop("XDG_CURRENT_DESKTOP", ":Missing::HoloNight:Hyprland:");
  RecordingWarningSink warnings;
  const auto settings = AppSettings::load(base, warnings);
  EXPECT_TRUE(settings.general().restoreLastLocation());
  EXPECT_TRUE(settings.search().paths.isEmpty());
  EXPECT_EQ(settings.search().patterns, QStringList{"vendor"});
  EXPECT_EQ(settings.settingInfo()[0].source, SettingSource::ConfigFile);
  EXPECT_EQ(settings.settingInfo()[1].source, SettingSource::SessionOverride);
  EXPECT_EQ(settings.settingInfo()[2].source, SettingSource::ConfigFile);
  EXPECT_TRUE(warnings.messages.isEmpty());
}

TEST(AppSettings, InvalidSelectedProfileRetainsBaseAndNeverFallsThrough) {
  QTemporaryDir dir;
  const auto base = writeConfig(dir.path(), "[general]\nrestore_last_location = true\n");
  const ScopedEnvironmentVariable desktop("XDG_CURRENT_DESKTOP", "First:Later");
  auto writeProfile = [&](const QString& name, const QByteArray& content) {
    QFile file(dir.filePath("holonight-files/config-" + name + ".toml"));
    EXPECT_TRUE(file.open(QIODevice::WriteOnly));
    EXPECT_EQ(file.write(content), content.size());
  };
  writeProfile("later", "[general]\nrestore_last_location = false\n");
  for (const auto& content : {QByteArray("[general]\nrestore_last_location = 42\n"), QByteArray("[general\ninvalid"),
                              QByteArray("general = false\n")}) {
    writeProfile("first", content);
    RecordingWarningSink warnings;
    const auto settings = AppSettings::load(base, warnings);
    EXPECT_TRUE(settings.general().restoreLastLocation());
    EXPECT_EQ(settings.settingInfo()[0].source, SettingSource::ConfigFile);
    ASSERT_EQ(warnings.messages.size(), 1);
    EXPECT_TRUE(warnings.messages[0].contains("config-first.toml"));
  }
  ASSERT_TRUE(QFile::remove(dir.filePath("holonight-files/config-first.toml")));
  ASSERT_TRUE(QDir().mkdir(dir.filePath("holonight-files/config-first.toml")));
  RecordingWarningSink warnings;
  EXPECT_TRUE(AppSettings::load(base, warnings).general().restoreLastLocation());
  EXPECT_FALSE(warnings.messages.isEmpty());
}

TEST(AppSettings, SessionOnlyConfigurationAndSafeDesktopNames) {
  QTemporaryDir dir;
  const auto base = dir.filePath("config.toml");
  QFile file(dir.filePath("config-holonight-hyprland.toml"));
  ASSERT_TRUE(file.open(QIODevice::WriteOnly));
  file.write("[general]\nrestore_last_location = true\n");
  file.close();
  const ScopedEnvironmentVariable desktop("XDG_CURRENT_DESKTOP", "../escape:/absolute:é:HoloNight-Hyprland");
  RecordingWarningSink warnings;
  const auto settings = AppSettings::load(base, warnings);
  EXPECT_TRUE(settings.general().restoreLastLocation());
  EXPECT_EQ(settings.settingInfo()[0].source, SettingSource::SessionOverride);
  EXPECT_EQ(warnings.messages.size(), 3);
  for (const auto& identity : {QString{}, QString("Missing")}) {
    const ScopedEnvironmentVariable absent("XDG_CURRENT_DESKTOP", identity);
    RecordingWarningSink absentWarnings;
    EXPECT_FALSE(AppSettings::load(base, absentWarnings).general().restoreLastLocation());
    EXPECT_TRUE(absentWarnings.messages.isEmpty());
  }
}
