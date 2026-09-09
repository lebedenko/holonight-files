#include "text_preview_service.h"

#include "directory_fixtures.h"
#include "preview_fixtures.h"

#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeLargeText;
using files_test::writeRandomBinary;
using files_test::writeSmallText;
using files_test::writeUtf8Multilang;

TEST(TextPreviewService, LooksBinaryDetectsNulByte) {
  const QByteArray withNul("abc\0def", 7);
  EXPECT_TRUE(TextPreviewService::looksBinary(withNul));
}

TEST(TextPreviewService, LooksBinaryIsFalseForPlainAscii) {
  EXPECT_FALSE(TextPreviewService::looksBinary(QByteArrayLiteral("The quick brown fox.")));
}

TEST(TextPreviewService, LooksBinaryIsFalseForUtf8MultilingualText) {
  const auto utf8 = QStringLiteral("Café 日本語 😀 Здравствуй").toUtf8();
  EXPECT_FALSE(TextPreviewService::looksBinary(utf8));
}

TEST(TextPreviewService, RandomBinaryFixtureIsDetectedAsBinary) {
  QTemporaryDir dir(fixturePattern("text-random"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeRandomBinary(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_TRUE(TextPreviewService::looksBinary(file.read(8192)));
}

TEST(TextPreviewService, ReadHeadReturnsFullContentForASmallFile) {
  QTemporaryDir dir(fixturePattern("text-small"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeSmallText(dir);
  const auto result = TextPreviewService::readHead(path, 65536);
  EXPECT_FALSE(result.wasTruncated);
  EXPECT_EQ(result.totalSize, QFileInfo(path).size());
  EXPECT_EQ(result.content.size(), result.totalSize);
}

TEST(TextPreviewService, ReadHeadTruncatesALargeFileAtTheRequestedBoundary) {
  QTemporaryDir dir(fixturePattern("text-large"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeLargeText(dir);
  const auto result = TextPreviewService::readHead(path, 65536);
  EXPECT_TRUE(result.wasTruncated);
  EXPECT_GT(result.totalSize, 65536);
  EXPECT_EQ(result.content.toUtf8().size(), 65536);
  EXPECT_TRUE(result.content.endsWith(QStringLiteral("A")));  // 64 KiB head is all 'A', per the fixture
}

TEST(TextPreviewService, Utf8MultilangFixtureIsNotFalselyDetectedAsBinary) {
  QTemporaryDir dir(fixturePattern("text-utf8"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeUtf8Multilang(dir);
  QFile file(path);
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  EXPECT_FALSE(TextPreviewService::looksBinary(file.read(8192)));
}

TEST(TextPreviewService, OpenAndReadErrorsAreExplicit) {
  const auto missing = TextPreviewService::readHead(QStringLiteral("/nonexistent/preview-input"), 65536);
  EXPECT_FALSE(missing.error.isEmpty());
  QFile closed;
  const auto unreadable = TextPreviewService::readHead(closed, 65536);
  EXPECT_FALSE(unreadable.error.isEmpty());
  EXPECT_TRUE(unreadable.content.isEmpty());
}
