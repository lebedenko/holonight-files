#include "settings/toml_document.h"

#include "directory_fixtures.h"

#include <QDir>
#include <QDirIterator>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeFile;

TEST(TomlDocument, MissingFileIsNotAnError) {
  QTemporaryDir dir(fixturePattern("toml-missing"));
  ASSERT_TRUE(dir.isValid());
  const auto result = TomlDocument::parseFile(dir.filePath("absent.toml"));
  EXPECT_FALSE(result.file_exists);
  EXPECT_TRUE(result.diagnostics.empty());
  EXPECT_TRUE(result.document.sections().empty());
}

TEST(TomlDocument, SeveralSyntaxErrorsYieldExactlyOneDiagnosticWithLine) {
  QTemporaryDir dir(fixturePattern("toml-invalid"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeFile(dir, "config.toml", "[general]\nrestore_last_location = tru\n[broken\nx = 1979-13-45\n");
  const auto result = TomlDocument::parseFile(path);
  EXPECT_TRUE(result.file_exists);
  ASSERT_EQ(result.diagnostics.size(), 1U);
  EXPECT_EQ(result.diagnostics.front().kind, TomlDiagnostic::Kind::ParseError);
  EXPECT_EQ(result.diagnostics.front().line, 2);
  EXPECT_FALSE(result.diagnostics.front().message.isEmpty());
  EXPECT_TRUE(result.document.sections().empty());
  EXPECT_EQ(result.document.value("general", "restore_last_location").type, TomlValue::Type::Missing);
}

TEST(TomlDocument, ReportsDeclaredTypesAndStructure) {
  const auto result = TomlDocument::parse(
      "stray = 3\nversion = 1\n\n[general]\nflag = true\nname = \"x\"\n"
      "count = 42\nratio = 1.5\nlist = [1]\n[other]\n");
  ASSERT_TRUE(result.diagnostics.empty());
  const auto& doc = result.document;
  EXPECT_EQ(doc.value("general", "flag").type, TomlValue::Type::Bool);
  EXPECT_TRUE(doc.value("general", "flag").bool_value);
  EXPECT_EQ(doc.value("general", "flag").line, 5);
  EXPECT_EQ(doc.value("general", "name").type, TomlValue::Type::String);
  EXPECT_EQ(doc.value("general", "name").string_value, "x");
  EXPECT_EQ(doc.value("general", "count").type, TomlValue::Type::Integer);
  EXPECT_EQ(doc.value("general", "count").int_value, 42);
  EXPECT_EQ(doc.value("general", "ratio").type, TomlValue::Type::Other);
  EXPECT_EQ(doc.value("general", "list").type, TomlValue::Type::Other);
  EXPECT_EQ(doc.value("general", "absent").type, TomlValue::Type::Missing);
  EXPECT_EQ(doc.value({}, "version").int_value, 1);
  EXPECT_EQ(doc.rootKeys(), (std::vector<QString>{"stray", "version"}));
  EXPECT_EQ(doc.sections(), (std::vector<QString>{"general", "other"}));
  EXPECT_EQ(doc.keys("general"), (std::vector<QString>{"flag", "name", "count", "ratio", "list"}));
}

TEST(TomlDocument, QuotedStringsRoundTrip) {
  const QString awkward = QStringLiteral("/tmp/quote\"back\\slash\ttab\nnewline\x01 café 📁");
  const auto result = TomlDocument::parse(("value = " + TomlDocument::quoteString(awkward)).toUtf8());
  ASSERT_TRUE(result.diagnostics.empty());
  EXPECT_EQ(result.document.value({}, "value").string_value, awkward);
}

TEST(TomlDocument, ArrayOfTablesAccessorsCoverPresentAbsentAndOutOfRange) {
  const auto result = TomlDocument::parse(
      "version = 1\n\n"
      "[[bookmarks]]\n"
      "path = \"/mnt/data\"\n"
      "name = \"Data\"\n\n"
      "[[bookmarks]]\n"
      "path = \"~/Media\"\n"
      "custom_icon = \"star\"\n");
  ASSERT_TRUE(result.diagnostics.empty());
  const auto& doc = result.document;
  ASSERT_EQ(doc.arrayOfTablesSize("bookmarks"), 2);
  EXPECT_EQ(doc.arrayOfTablesSize("version"), 0);  // not an array key
  EXPECT_EQ(doc.arrayOfTablesSize("absent"), 0);   // absent key
  EXPECT_EQ(doc.arrayOfTablesValue("bookmarks", 0, "path").string_value, "/mnt/data");
  EXPECT_EQ(doc.arrayOfTablesValue("bookmarks", 0, "name").string_value, "Data");
  EXPECT_EQ(doc.arrayOfTablesValue("bookmarks", 0, "absent").type, TomlValue::Type::Missing);
  EXPECT_EQ(doc.arrayOfTablesValue("bookmarks", 2, "path").type, TomlValue::Type::Missing);  // out of range
  EXPECT_EQ(doc.arrayOfTablesValue("absent", 0, "path").type, TomlValue::Type::Missing);
  EXPECT_EQ(doc.arrayOfTablesKeys("bookmarks", 0), (std::vector<QString>{"path", "name"}));
  EXPECT_EQ(doc.arrayOfTablesKeys("bookmarks", 1), (std::vector<QString>{"path", "custom_icon"}));
  EXPECT_TRUE(doc.arrayOfTablesKeys("bookmarks", 2).empty());
  EXPECT_GT(doc.arrayOfTablesLine("bookmarks", 0), 0);
  EXPECT_GT(doc.arrayOfTablesLine("bookmarks", 1), doc.arrayOfTablesLine("bookmarks", 0));
  EXPECT_EQ(doc.arrayOfTablesLine("bookmarks", 2), 0);
  EXPECT_EQ(doc.arrayOfTablesLine("absent", 0), 0);
}

// SPEC.md REQ-F-028: toml++ is included by exactly one translation unit and no header.
TEST(TomlDocument, TomlLibraryIsIncludedOnlyByTheAdapter) {
  const QDir apps(QStringLiteral(FILES_SOURCE_DIR "/apps"));
  ASSERT_TRUE(apps.exists());
  QStringList includers;
  QDirIterator files(apps.path(), {"*.h", "*.cpp"}, QDir::Files, QDirIterator::Subdirectories);
  while (files.hasNext()) {
    QFile file(files.next());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    if (file.readAll().contains("toml++")) {
      includers.append(apps.relativeFilePath(file.fileName()));
    }
  }
  EXPECT_EQ(includers, QStringList{"files/settings/toml_document.cpp"});
}
