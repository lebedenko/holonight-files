#include "places/bookmark_store.h"

#include "directory_fixtures.h"
#include "settings_fixtures.h"

#include <QDir>
#include <QFileInfo>

#include <gtest/gtest.h>

using BookmarkStore::Bookmark;
using files_test::fixturePattern;
using files_test::RecordingWarningSink;
using files_test::writeFile;

TEST(BookmarkStore, MissingFileYieldsNoBookmarksAndNoWarnings) {
  QTemporaryDir dir(fixturePattern("places-missing"));
  ASSERT_TRUE(dir.isValid());
  RecordingWarningSink warnings;
  EXPECT_TRUE(BookmarkStore::read(dir.filePath("places.toml"), warnings).empty());
  EXPECT_TRUE(warnings.messages.isEmpty());
}

TEST(BookmarkStore, ValidBookmarksLoadInFileOrderWithNameDefaulting) {
  QTemporaryDir dir(fixturePattern("places-valid"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(dir, "places.toml",
                              "version = 1\n\n"
                              "[[bookmarks]]\n"
                              "path = \"/mnt/data\"\n"
                              "name = \"Data Drive\"\n\n"
                              "[[bookmarks]]\n"
                              "path = \"~/Media\"\n\n"
                              "[[bookmarks]]\n"
                              "path = \"/srv/projects\"\n");
  RecordingWarningSink warnings;
  const auto bookmarks = BookmarkStore::read(path, warnings);
  ASSERT_EQ(bookmarks.size(), 3U);
  EXPECT_EQ(bookmarks[0].path, "/mnt/data");
  EXPECT_EQ(bookmarks[0].name, "Data Drive");
  EXPECT_EQ(bookmarks[1].path, QDir::homePath() + "/Media");
  EXPECT_EQ(bookmarks[1].name, "Media");
  EXPECT_EQ(bookmarks[2].path, "/srv/projects");
  EXPECT_EQ(bookmarks[2].name, "projects");
  EXPECT_TRUE(warnings.messages.isEmpty());
}

TEST(BookmarkStore, UnparseableTomlDropsAllBookmarksWithOneWarning) {
  QTemporaryDir dir(fixturePattern("places-syntax-error"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(dir, "places.toml", "version = 1\n[[bookmarks\npath = \"/x\"\n");
  RecordingWarningSink warnings;
  EXPECT_TRUE(BookmarkStore::read(path, warnings).empty());
  ASSERT_EQ(warnings.messages.size(), 1);
  EXPECT_TRUE(warnings.messages.front().contains(path));
  EXPECT_TRUE(warnings.messages.front().contains("line"));
}

TEST(BookmarkStore, VersionMissingOrMismatchedDropsAllBookmarksWithOneWarning) {
  QTemporaryDir dir(fixturePattern("places-version"));
  ASSERT_TRUE(dir.isValid());
  {
    const auto path = writeFile(dir, "bad-version.toml", "version = 999\n[[bookmarks]]\npath = \"/x\"\n");
    RecordingWarningSink warnings;
    EXPECT_TRUE(BookmarkStore::read(path, warnings).empty());
    ASSERT_EQ(warnings.messages.size(), 1);
    EXPECT_TRUE(warnings.messages.front().contains(path));
    EXPECT_TRUE(warnings.messages.front().contains("version"));
  }
  {
    const auto path = writeFile(dir, "no-version.toml", "[[bookmarks]]\npath = \"/x\"\n");
    RecordingWarningSink warnings;
    EXPECT_TRUE(BookmarkStore::read(path, warnings).empty());
    ASSERT_EQ(warnings.messages.size(), 1);
    EXPECT_TRUE(warnings.messages.front().contains(path));
    EXPECT_TRUE(warnings.messages.front().contains("version"));
  }
}

TEST(BookmarkStore, InvalidEntriesAreSkippedWithOneWarningEachWhileOthersLoad) {
  QTemporaryDir dir(fixturePattern("places-invalid-entries"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(dir, "places.toml",
                              "version = 1\n\n"
                              "[[bookmarks]]\n"
                              "name = \"No Path\"\n\n"
                              "[[bookmarks]]\n"
                              "path = 42\n\n"
                              "[[bookmarks]]\n"
                              "path = \"relative/path\"\n\n"
                              "[[bookmarks]]\n"
                              "path = \"/valid/one\"\n\n"
                              "[[bookmarks]]\n"
                              "path = \"~/valid-two\"\n");
  RecordingWarningSink warnings;
  const auto bookmarks = BookmarkStore::read(path, warnings);
  ASSERT_EQ(bookmarks.size(), 2U);
  EXPECT_EQ(bookmarks[0].path, "/valid/one");
  EXPECT_EQ(bookmarks[1].path, QDir::homePath() + "/valid-two");
  EXPECT_EQ(warnings.messages.size(), 3);
}

TEST(BookmarkStore, DollarVarPathIsRejected) {
  QTemporaryDir dir(fixturePattern("places-dollar-var"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(dir, "places.toml", "version = 1\n[[bookmarks]]\npath = \"$HOME/subdir\"\n");
  RecordingWarningSink warnings;
  EXPECT_TRUE(BookmarkStore::read(path, warnings).empty());
  ASSERT_EQ(warnings.messages.size(), 1);
  EXPECT_TRUE(warnings.messages.front().contains("$HOME/subdir"));
}

TEST(BookmarkStore, UnknownTopLevelAndEntryKeysWarnButBookmarkStillLoads) {
  QTemporaryDir dir(fixturePattern("places-unknown-keys"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(dir, "places.toml",
                              "version = 1\n\n"
                              "[extra_field]\n"
                              "x = 1\n\n"
                              "[[bookmarks]]\n"
                              "path = \"/mnt/data\"\n"
                              "custom_icon = \"star\"\n");
  RecordingWarningSink warnings;
  const auto bookmarks = BookmarkStore::read(path, warnings);
  ASSERT_EQ(bookmarks.size(), 1U);
  EXPECT_EQ(bookmarks[0].path, "/mnt/data");
  ASSERT_EQ(warnings.messages.size(), 2);
  EXPECT_TRUE(warnings.messages[0].contains("extra_field"));
  EXPECT_TRUE(warnings.messages[1].contains("custom_icon"));
}

TEST(BookmarkStore, ReadingDoesNotModifyTheFile) {
  QTemporaryDir dir(fixturePattern("places-readonly"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(dir, "places.toml", "version = 1\n[[bookmarks]]\npath = \"/mnt/data\"\n");
  const auto before = QFileInfo(path).lastModified();
  RecordingWarningSink warnings;
  BookmarkStore::read(path, warnings);
  EXPECT_EQ(QFileInfo(path).lastModified(), before);
  EXPECT_FALSE(QFile::exists(dir.filePath("places.toml.bak")));
}
