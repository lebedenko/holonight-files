#include "text_preview_service.h"

#include "directory_fixtures.h"
#include "preview_fixtures.h"

#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using files_test::fixturePattern;
using files_test::writeBytes;
using files_test::writeCrlfText;
using files_test::writeCrText;
using files_test::writeCutMidUtf8Text;
using files_test::writeEmptyText;
using files_test::writeInvalidUtf8Text;
using files_test::writeLargeText;
using files_test::writeNumberedLines;
using files_test::writeRandomBinary;
using files_test::writeSingleLongLine;
using files_test::writeSmallText;
using files_test::writeTextOfSize;
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
  ASSERT_FALSE(result.lines.isEmpty());
  // The fixture ends with a terminator, which is not a line separator (one byte).
  EXPECT_EQ(result.lines.join(QLatin1Char('\n')).toUtf8().size() + 1, result.totalSize);
}

TEST(TextPreviewService, ReadHeadTruncatesALargeFileAtTheRequestedBoundary) {
  QTemporaryDir dir(fixturePattern("text-large"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeLargeText(dir);
  const auto result = TextPreviewService::readHead(path, 102400);
  EXPECT_TRUE(result.wasTruncated);
  EXPECT_GT(result.totalSize, 102400);
  ASSERT_EQ(result.lines.size(), 1);  // the fixture has no line terminators
  EXPECT_EQ(result.lines.first().toUtf8().size(), 102400);
  EXPECT_TRUE(result.lines.first().endsWith(QStringLiteral("B")));  // past the fixture's 64 KiB of 'A'
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
  EXPECT_TRUE(unreadable.lines.isEmpty());
}

TEST(TextPreviewService, FileBelowTheCapIsFullyLoadedAndNotTruncated) {
  QTemporaryDir dir(fixturePattern("text-50k"));
  ASSERT_TRUE(dir.isValid());
  const auto result = TextPreviewService::readHead(writeTextOfSize(dir, QStringLiteral("a.txt"), 50000), 102400);
  EXPECT_FALSE(result.wasTruncated);
  EXPECT_EQ(result.totalSize, 50000);
  // The fixture's last byte is a terminator, which is not a line separator.
  EXPECT_EQ(result.lines.join(QLatin1Char('\n')).toUtf8().size() + 1, 50000);
}

TEST(TextPreviewService, FileOfExactlyTheCapIsNotTruncated) {
  QTemporaryDir dir(fixturePattern("text-cap"));
  ASSERT_TRUE(dir.isValid());
  const auto result = TextPreviewService::readHead(writeTextOfSize(dir, QStringLiteral("a.txt"), 102400), 102400);
  EXPECT_FALSE(result.wasTruncated);
  EXPECT_EQ(result.totalSize, 102400);
}

TEST(TextPreviewService, OneByteOverTheCapIsTruncated) {
  QTemporaryDir dir(fixturePattern("text-cap1"));
  ASSERT_TRUE(dir.isValid());
  const auto result = TextPreviewService::readHead(writeTextOfSize(dir, QStringLiteral("a.txt"), 102401), 102400);
  EXPECT_TRUE(result.wasTruncated);
  EXPECT_EQ(result.totalSize, 102401);
  EXPECT_EQ(result.lines.join(QLatin1Char('\n')).toUtf8().size() + 1, 102400);  // trailing terminator
}

TEST(TextPreviewService, TwoHundredKilobyteFileLoadsExactlyTheCap) {
  QTemporaryDir dir(fixturePattern("text-200k"));
  ASSERT_TRUE(dir.isValid());
  const auto result = TextPreviewService::readHead(writeTextOfSize(dir, QStringLiteral("a.txt"), 200000), 102400);
  EXPECT_TRUE(result.wasTruncated);
  // 102,400 bytes with a newline after every 100th byte: 1024 complete lines, no phantom line.
  EXPECT_EQ(result.lines.size(), 1024);
  EXPECT_EQ(result.lines.join(QLatin1Char('\n')).toUtf8().size() + 1, 102400);
}

TEST(TextPreviewService, CapCuttingAMultibyteCharacterDropsItInsteadOfDecodingReplacement) {
  QTemporaryDir dir(fixturePattern("text-cut"));
  ASSERT_TRUE(dir.isValid());
  const auto result = TextPreviewService::readHead(writeCutMidUtf8Text(dir), 102400);
  EXPECT_TRUE(result.wasTruncated);
  ASSERT_EQ(result.lines.size(), 1);
  EXPECT_FALSE(result.lines.first().contains(QChar(0xFFFD)));
  EXPECT_EQ(result.lines.first().size(), 102400 - 1);
}

TEST(TextPreviewService, EmptyFileYieldsOneEmptyLine) {
  QTemporaryDir dir(fixturePattern("text-empty"));
  ASSERT_TRUE(dir.isValid());
  const auto result = TextPreviewService::readHead(writeEmptyText(dir), 102400);
  EXPECT_TRUE(result.error.isEmpty());
  EXPECT_FALSE(result.wasTruncated);
  EXPECT_EQ(result.lines, QStringList{QString()});
}

TEST(TextPreviewService, InvalidUtf8IsReplacedWithoutAnError) {
  QTemporaryDir dir(fixturePattern("text-invalid"));
  ASSERT_TRUE(dir.isValid());
  const auto result = TextPreviewService::readHead(writeInvalidUtf8Text(dir), 102400);
  EXPECT_TRUE(result.error.isEmpty());
  ASSERT_EQ(result.lines.size(), 1);
  EXPECT_TRUE(result.lines.first().startsWith(QStringLiteral("hello")));
  EXPECT_TRUE(result.lines.first().contains(QChar(0xFFFD)));
}

TEST(TextPreviewService, CrlfAndCrFilesYieldTheSameLinesAsLf) {
  QTemporaryDir dir(fixturePattern("text-eol"));
  ASSERT_TRUE(dir.isValid());
  const QStringList expected{QStringLiteral("a"), QStringLiteral("b"), QStringLiteral("c")};
  EXPECT_EQ(TextPreviewService::readHead(writeBytes(dir, QStringLiteral("lf.txt"), "a\nb\nc\n"), 102400).lines,
            expected);
  EXPECT_EQ(TextPreviewService::readHead(writeCrlfText(dir), 102400).lines, expected);
  EXPECT_EQ(TextPreviewService::readHead(writeCrText(dir), 102400).lines, expected);
}

TEST(TextPreviewService, UnreadableFileReportsAnErrorWithNoLines) {
  QTemporaryDir dir(fixturePattern("text-denied"));
  ASSERT_TRUE(dir.isValid());
  const auto path = writeBytes(dir, QStringLiteral("d.txt"), "secret");
  ASSERT_TRUE(QFile::setPermissions(path, QFileDevice::Permissions()));
  QFile probe(path);
  if (probe.open(QIODevice::ReadOnly)) {
    GTEST_SKIP() << "running with privileges that bypass file permissions";
  }
  const auto result = TextPreviewService::readHead(path, 102400);
  EXPECT_FALSE(result.error.isEmpty());
  EXPECT_TRUE(result.lines.isEmpty());
}

TEST(TextPreviewService, NamedFixturesProduceTheirDocumentedShapes) {
  QTemporaryDir dir(fixturePattern("text-fixtures"));
  ASSERT_TRUE(dir.isValid());
  EXPECT_EQ(QFileInfo(files_test::writeEmptyText(dir)).size(), 0);
  EXPECT_EQ(TextPreviewService::readHead(writeNumberedLines(dir, "n.txt", 7), 102400).lines.size(), 7);
  EXPECT_EQ(QFileInfo(writeSingleLongLine(dir, 300)).size(), 300);
  EXPECT_EQ(TextPreviewService::readHead(writeSingleLongLine(dir, 300), 102400).lines.size(), 1);
  EXPECT_EQ(QFileInfo(writeTextOfSize(dir, QStringLiteral("s.txt"), 102401)).size(), 102401);
}

// Risk R2 in the design: worst-case shapes at the cap must load quickly on the worker.
TEST(TextPreviewService, WorstCaseShapesAtTheCapLoadQuickly) {
  QTemporaryDir dir(fixturePattern("text-worst"));
  ASSERT_TRUE(dir.isValid());
  QElapsedTimer timer;
  timer.start();
  const auto oneLine = TextPreviewService::readHead(writeSingleLongLine(dir, 102400), 102400);
  EXPECT_EQ(oneLine.lines.size(), 1);
  EXPECT_FALSE(oneLine.wasTruncated);
  const auto manyLines =
      TextPreviewService::readHead(writeBytes(dir, QStringLiteral("newlines.txt"), QByteArray(102400, '\n')), 102400);
  EXPECT_EQ(manyLines.lines.size(), 102400);  // every terminator ends an (empty) line; none is synthesized after
  EXPECT_LT(timer.elapsed(), 500);
}
