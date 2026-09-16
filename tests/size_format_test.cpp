#include "size_format.h"

#include <QCoreApplication>
#include <QLocale>
#include <QTranslator>

#include <gtest/gtest.h>
#include <limits>

TEST(SizeFormat, BoundariesRoundingAndSignedRange) {
  const SizeFormat formatter;
  EXPECT_TRUE(formatter.formatSize(-1).isEmpty());
  EXPECT_TRUE(formatter.formatSize(std::numeric_limits<qint64>::min()).isEmpty());
  EXPECT_EQ(formatter.formatSize(0), "0 B");
  EXPECT_EQ(formatter.formatSize(1023), "1023 B");
  EXPECT_EQ(formatter.formatSize(1024), "1.0 KB");
  EXPECT_EQ(formatter.formatSize(1536), "1.5 KB");
  EXPECT_EQ(formatter.formatSize(1588), "1.6 KB");
  qint64 boundary = 1024;
  for (const auto* unit : {"MB", "GB", "TB"}) {
    boundary *= 1024;
    EXPECT_EQ(formatter.formatSize(boundary), QStringLiteral("1.0 %1").arg(unit));
  }
  EXPECT_EQ(formatter.formatSize((1024 * 1024) - 1), "1024.0 KB");
  EXPECT_EQ(formatter.formatSize(boundary * 1024), "1024.0 TB");
  EXPECT_EQ(formatter.formatSize(std::numeric_limits<qint64>::max()), "8388608.0 TB");
  const QLocale previous;
  QLocale::setDefault(QLocale(QLocale::German));
  EXPECT_EQ(formatter.formatSize(1536), "1.5 KB");
  QLocale::setDefault(previous);
}

namespace {
class SizeTranslator : public QTranslator {
 public:
  QString translate(const char* context, const char* source, const char* /*disambiguation*/, int /*n*/) const override {
    if (QByteArray(context) != "SizeFormat") {
      return {};
    }
    if (QByteArray(source) == "%1 B") {
      return QStringLiteral("bytes: %1");
    }
    if (QByteArray(source) == "%1 %2") {
      return QStringLiteral("size: %1 %2");
    }
    return {};
  }
};
}  // namespace

TEST(SizeFormat, PreservesTranslationContextAndSourceStrings) {
  const SizeFormat formatter;
  SizeTranslator translator;
  QCoreApplication::installTranslator(&translator);
  EXPECT_EQ(formatter.formatSize(2), "bytes: 2");
  EXPECT_EQ(formatter.formatSize(1536), "size: 1.5 KB");
  QCoreApplication::removeTranslator(&translator);
}
