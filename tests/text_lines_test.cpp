#include "text_lines.h"

#include <QByteArray>

#include <gtest/gtest.h>

namespace {

QStringList split(const QByteArray& bytes) { return TextLines::splitLines(TextLines::decodeUtf8(bytes)); }

QStringList lines(std::initializer_list<const char*> values) {
  QStringList result;
  for (const char* value : values) {
    result.append(QString::fromUtf8(value));
  }
  return result;
}

}  // namespace

TEST(TextLines, EmptyInputYieldsOneEmptyLine) {
  EXPECT_EQ(split(QByteArray()), QStringList{QString()});
  EXPECT_EQ(TextLines::splitLines(QString()), QStringList{QString()});
}

TEST(TextLines, SplitsOnLfCrlfAndCrEquivalently) {
  const auto expected = lines({"a", "b", "c"});
  EXPECT_EQ(split("a\nb\nc"), expected);
  EXPECT_EQ(split("a\r\nb\r\nc"), expected);
  EXPECT_EQ(split("a\rb\rc"), expected);
}

TEST(TextLines, TrailingTerminatorAddsNoPhantomLine) {
  EXPECT_EQ(split("line1\nline2\n"), lines({"line1", "line2"}));
  EXPECT_EQ(split("a\r\n"), lines({"a"}));
  EXPECT_EQ(split("a\r"), lines({"a"}));
}

TEST(TextLines, BlankLinesArePreserved) {
  EXPECT_EQ(split("a\n\n"), lines({"a", ""}));
  EXPECT_EQ(split("\n"), lines({""}));
  EXPECT_EQ(split("\n\nb"), lines({"", "", "b"}));
  EXPECT_EQ(split("a\r\rb"), lines({"a", "", "b"}));
}

TEST(TextLines, CrlfIsOneTerminatorButLfCrIsTwo) {
  EXPECT_EQ(split("a\r\nb"), lines({"a", "b"}));
  EXPECT_EQ(split("a\n\rb"), lines({"a", "", "b"}));
}

TEST(TextLines, InvalidUtf8BecomesReplacementCharacters) {
  const QString text = TextLines::decodeUtf8(QByteArray("hello\xff\xfe", 7));
  EXPECT_TRUE(text.startsWith(QStringLiteral("hello")));
  EXPECT_TRUE(text.contains(QChar(0xFFFD)));
  EXPECT_FALSE(text.contains(QChar(0xFF)));
}

TEST(TextLines, NulAndBomAreHandled) {
  EXPECT_EQ(TextLines::decodeUtf8(QByteArray("a\0b", 3)), QStringLiteral("a�b"));
  EXPECT_EQ(TextLines::decodeUtf8("\xEF\xBB\xBF"
                                  "hi"),
            QStringLiteral("hi"));
}

TEST(TextLines, ValidMultibyteTextRoundTrips) { EXPECT_EQ(TextLines::decodeUtf8("日本語"), QStringLiteral("日本語")); }

TEST(TextLines, TrimDropsOnlyAnIncompleteTail) {
  const QByteArray twoByte = "ab\xC3";           // lead of a 2-byte sequence, no continuation
  const QByteArray threeByte = "ab\xE6\x97";     // 3-byte sequence missing its last byte
  const QByteArray fourByte = "ab\xF0\x9F\x98";  // 4-byte sequence missing its last byte
  EXPECT_EQ(TextLines::trimIncompleteUtf8Tail(twoByte), QByteArrayView("ab"));
  EXPECT_EQ(TextLines::trimIncompleteUtf8Tail(threeByte), QByteArrayView("ab"));
  EXPECT_EQ(TextLines::trimIncompleteUtf8Tail(fourByte), QByteArrayView("ab"));

  const QByteArray complete = "ab\xE6\x97\xA5";  // complete 3-byte character
  EXPECT_EQ(TextLines::trimIncompleteUtf8Tail(complete), QByteArrayView(complete));
  EXPECT_EQ(TextLines::trimIncompleteUtf8Tail(QByteArrayView("plain")), QByteArrayView("plain"));
  EXPECT_TRUE(TextLines::trimIncompleteUtf8Tail(QByteArrayView()).isEmpty());
}

TEST(TextLines, TrimAvoidsSpuriousReplacementAtTheCut) {
  const QByteArray full = QByteArray("abc") + QByteArray("日本語");
  const QByteArray cut = full.left(full.size() - 1);  // cap lands inside the last character
  EXPECT_TRUE(TextLines::decodeUtf8(cut).contains(QChar(0xFFFD)));
  EXPECT_FALSE(TextLines::decodeUtf8(TextLines::trimIncompleteUtf8Tail(cut)).contains(QChar(0xFFFD)));
}
