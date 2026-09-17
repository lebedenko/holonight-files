#include "places/user_dirs_parser.h"

#include "directory_fixtures.h"

#include <QFileInfo>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeFile;
using UserDirsParser::Entry;
using UserDirsParser::Key;

namespace {
constexpr auto kHome = "/home/tester";
}

TEST(UserDirsParser, ParsesValidLinesAndIgnoresJunkWithoutWarnings) {
  const auto entries = UserDirsParser::parse(QStringLiteral("# a comment\n"
                                                            "\n"
                                                            "garbage line\n"
                                                            "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
                                                            "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n"
                                                            "XDG_UNKNOWN_DIR=\"$HOME/Nope\"\n"
                                                            "  XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"  \n"
                                                            "XDG_MISSING_QUOTES_DIR=$HOME/x\n"),
                                             QString(kHome));
  ASSERT_EQ(entries.size(), 3U);
  EXPECT_EQ(entries[0].key, Key::Desktop);
  EXPECT_EQ(entries[0].path, QString(kHome) + "/Desktop");
  EXPECT_EQ(entries[1].key, Key::Documents);
  EXPECT_EQ(entries[1].path, QString(kHome) + "/Documents");
  EXPECT_EQ(entries[2].key, Key::Downloads);
  EXPECT_EQ(entries[2].path, QString(kHome) + "/Downloads");
}

TEST(UserDirsParser, UnescapesQuotedAndDollarSequences) {
  const auto entries = UserDirsParser::parse(QStringLiteral("XDG_DOCUMENTS_DIR=\"$HOME/My Documents\"\n"
                                                            "XDG_PICTURES_DIR=\"$HOME/Say \\\"Hi\\\"\"\n"
                                                            "XDG_MUSIC_DIR=\"/srv/\\$music\"\n"),
                                             QString(kHome));
  ASSERT_EQ(entries.size(), 3U);
  EXPECT_EQ(entries[0].path, QString(kHome) + "/My Documents");
  EXPECT_EQ(entries[1].path, QString(kHome) + "/Say \"Hi\"");
  EXPECT_EQ(entries[2].path, "/srv/$music");
}

TEST(UserDirsParser, HomeEqualEntryIsDisabledOthersKept) {
  const auto entries = UserDirsParser::parse(QStringLiteral("XDG_DOCUMENTS_DIR=\"$HOME\"\n"
                                                            "XDG_TEMPLATES_DIR=\"$HOME/\"\n"
                                                            "XDG_DOWNLOAD_DIR=\"/home/user/dl\"\n"),
                                             QStringLiteral("/home/user"));
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].key, Key::Downloads);
  EXPECT_EQ(entries[0].path, "/home/user/dl");
}

TEST(UserDirsParser, DisplayOrderIsFixedRegardlessOfFileOrder) {
  const auto entries = UserDirsParser::parse(QStringLiteral("XDG_PUBLICSHARE_DIR=\"$HOME/Public\"\n"
                                                            "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"
                                                            "XDG_PROJECTS_DIR=\"$HOME/Projects\"\n"
                                                            "XDG_TEMPLATES_DIR=\"$HOME/Templates\"\n"
                                                            "XDG_VIDEOS_DIR=\"$HOME/Videos\"\n"
                                                            "XDG_MUSIC_DIR=\"$HOME/Music\"\n"
                                                            "XDG_PICTURES_DIR=\"$HOME/Pictures\"\n"
                                                            "XDG_DOWNLOAD_DIR=\"$HOME/Downloads\"\n"
                                                            "XDG_DOCUMENTS_DIR=\"$HOME/Documents\"\n"),
                                             QString(kHome));
  ASSERT_EQ(entries.size(), 9U);
  const std::vector<Key> expected{Key::Desktop, Key::Documents, Key::Downloads, Key::Pictures, Key::Music,
                                  Key::Videos,  Key::Projects,  Key::Templates, Key::Public};
  for (std::size_t i = 0; i < expected.size(); ++i) {
    EXPECT_EQ(entries[i].key, expected[i]) << i;
  }
  // A subset preserves the same fixed order among the entries actually present.
  const auto subset = UserDirsParser::parse(
      QStringLiteral("XDG_VIDEOS_DIR=\"$HOME/Videos\"\nXDG_DESKTOP_DIR=\"$HOME/Desktop\"\n"), QString(kHome));
  ASSERT_EQ(subset.size(), 2U);
  EXPECT_EQ(subset[0].key, Key::Desktop);
  EXPECT_EQ(subset[1].key, Key::Videos);
}

TEST(UserDirsParser, LabelsAndIconsMatchSpecTable) {
  const std::vector<std::pair<Key, std::pair<const char*, const char*>>> table{
      {Key::Desktop, {"Desktop", "user-desktop"}},
      {Key::Documents, {"Documents", "folder-documents"}},
      {Key::Downloads, {"Downloads", "folder-download"}},
      {Key::Pictures, {"Pictures", "folder-pictures"}},
      {Key::Music, {"Music", "folder-music"}},
      {Key::Videos, {"Videos", "folder-videos"}},
      {Key::Projects, {"Projects", "folder-development"}},
      {Key::Templates, {"Templates", "folder-templates"}},
      {Key::Public, {"Public", "folder-publicshare"}},
  };
  for (const auto& [key, expected] : table) {
    EXPECT_EQ(UserDirsParser::label(key), QLatin1String(expected.first));
    EXPECT_EQ(UserDirsParser::iconName(key), QLatin1String(expected.second));
  }
}

TEST(UserDirsParser, MissingFileYieldsNoEntriesAndFileTimeUnchanged) {
  QTemporaryDir dir(fixturePattern("user-dirs-missing"));
  ASSERT_TRUE(dir.isValid());
  EXPECT_TRUE(UserDirsParser::parseFile(dir.filePath("absent-user-dirs.dirs"), QString(kHome)).empty());
}

TEST(UserDirsParser, ParseFileReadsRealFileWithoutModifyingIt) {
  QTemporaryDir dir(fixturePattern("user-dirs-file"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(dir, "user-dirs.dirs", "XDG_DESKTOP_DIR=\"$HOME/Desktop\"\n");
  ASSERT_FALSE(path.isEmpty());
  const auto before = QFileInfo(path).lastModified();
  const auto entries = UserDirsParser::parseFile(path, QString(kHome));
  ASSERT_EQ(entries.size(), 1U);
  EXPECT_EQ(entries[0].path, QString(kHome) + "/Desktop");
  EXPECT_EQ(QFileInfo(path).lastModified(), before);
}
