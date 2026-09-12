#include "icon_image_provider.h"

#include "directory_fixtures.h"
#include "icon_image_provider_test_access.h"

#include <QColor>
#include <QDir>
#include <QIcon>
#include <QImage>
#include <QScopeGuard>
#include <QTemporaryDir>

#include <gtest/gtest.h>

using files_test::fixturePattern;

namespace {
// Tests may replace the process-wide icon theme; production code never does (REQ-F-018).
[[nodiscard]] auto scopedIconTheme(const QStringList& searchPaths, const QString& themeName) {
  const auto previousPaths = QIcon::themeSearchPaths();
  const auto previousFallbackPaths = QIcon::fallbackSearchPaths();
  const auto previousTheme = QIcon::themeName();
  const auto previousFallbackTheme = QIcon::fallbackThemeName();
  QIcon::setThemeSearchPaths(searchPaths);
  QIcon::setFallbackSearchPaths({});
  QIcon::setThemeName(themeName);
  QIcon::setFallbackThemeName(themeName);
  return qScopeGuard([=] {
    QIcon::setThemeSearchPaths(previousPaths);
    QIcon::setFallbackSearchPaths(previousFallbackPaths);
    QIcon::setThemeName(previousTheme);
    QIcon::setFallbackThemeName(previousFallbackTheme);
  });
}

// A one-icon theme with a 16 px PNG "folder", so pixmap sizing can be checked without any icon
// theme installed on the machine running the tests.
bool writeTinyTheme(const QTemporaryDir& root) {
  const QDir themeDir(root.filePath("tiny-test-theme"));
  if (!themeDir.mkpath("16x16/places")) {
    return false;
  }
  QFile index(themeDir.filePath("index.theme"));
  if (!index.open(QIODevice::WriteOnly) ||
      index.write("[Icon Theme]\nName=tiny-test-theme\nDirectories=16x16/places\n\n"
                  "[16x16/places]\nSize=16\nContext=Places\nType=Fixed\n") < 0) {
    return false;
  }
  QImage image(16, 16, QImage::Format_ARGB32);
  image.fill(QColor(0x20, 0xa0, 0xf0));
  return image.save(themeDir.filePath("16x16/places/folder.png"));
}

}  // namespace

TEST(IconImageProvider, UnresolvableChainReturnsNullAndLeavesSizeUnset) {
  QTemporaryDir empty(fixturePattern("icon-empty-theme"));
  ASSERT_TRUE(empty.isValid());
  const auto restore = scopedIconTheme({empty.path()}, QStringLiteral("nonexistent-test-theme"));
  IconImageProvider provider;
  QSize size(-7, -7);
  const auto pixmap = provider.requestPixmap("unknown-icon-name/another-unknown-name", &size, QSize(128, 128));
  EXPECT_TRUE(pixmap.isNull());
  EXPECT_EQ(size, QSize(-7, -7));
}

TEST(IconImageProvider, NoInstalledThemeResolvesNeitherFolderNorGenericFile) {
  // REQ-F-016's bare-container case: with no theme reachable, the provider must fail so QML shows
  // the bundled glyphs, never substitute something of its own.
  QTemporaryDir empty(fixturePattern("icon-empty-theme"));
  ASSERT_TRUE(empty.isValid());
  const auto restore = scopedIconTheme({empty.path()}, QStringLiteral("nonexistent-test-theme"));
  IconImageProvider provider;
  EXPECT_TRUE(provider.requestPixmap("folder/inode-directory", nullptr, QSize(20, 20)).isNull());
  EXPECT_TRUE(provider.requestPixmap("application-x-generic", nullptr, QSize(20, 20)).isNull());
}

TEST(IconImageProvider, ThemeHitIsRenderedAtTheRequestedPhysicalSize) {
  QTemporaryDir root(fixturePattern("icon-tiny-theme"));
  ASSERT_TRUE(root.isValid());
  ASSERT_TRUE(writeTinyTheme(root));
  const auto restore = scopedIconTheme({root.path()}, QStringLiteral("tiny-test-theme"));
  IconImageProvider provider;
  QSize size;
  // 20 logical px at 1.5x: Qt Quick asks for 30 physical px; a 16 px theme PNG must not be returned
  // undersized, and the first missing candidate must fall through to the next one.
  const auto pixmap = provider.requestPixmap("not-in-this-theme/folder", &size, QSize(30, 30));
  ASSERT_FALSE(pixmap.isNull());
  EXPECT_EQ(pixmap.size(), QSize(30, 30));
  EXPECT_EQ(size, QSize(30, 30));
  EXPECT_DOUBLE_EQ(pixmap.devicePixelRatio(), 1.0);
}

TEST(IconImageProvider, RepeatedRequestsAreServedFromCache) {
  QTemporaryDir root(fixturePattern("icon-tiny-theme"));
  ASSERT_TRUE(root.isValid());
  ASSERT_TRUE(writeTinyTheme(root));
  const auto restore = scopedIconTheme({root.path()}, QStringLiteral("tiny-test-theme"));
  IconImageProvider provider;
  // 1000 rows sharing three distinct chains: one theme lookup per distinct (name, size), hit or miss.
  for (int row = 0; row < 1000; ++row) {
    provider.requestPixmap("folder", nullptr, QSize(20, 20));
    provider.requestPixmap("text-x-python/application-x-generic", nullptr, QSize(20, 20));
    provider.requestPixmap("image-jpeg/application-x-generic", nullptr, QSize(20, 20));
  }
  EXPECT_EQ(IconImageProviderTestAccess::themeLookupCount(provider), 4);
  // The preview pane's 128 px request is a distinct cache entry.
  EXPECT_FALSE(provider.requestPixmap("folder", nullptr, QSize(128, 128)).isNull());
  EXPECT_EQ(IconImageProviderTestAccess::themeLookupCount(provider), 5);
}

TEST(IconImageProvider, MissingAndMalformedChainsStayNullAndReuseCachedMisses) {
  QTemporaryDir empty(fixturePattern("icon-empty-theme"));
  ASSERT_TRUE(empty.isValid());
  const auto restore = scopedIconTheme({empty.path()}, QStringLiteral("nonexistent-test-theme"));
  IconImageProvider provider;
  for (int row = 0; row < 500; ++row) {
    EXPECT_TRUE(provider.requestPixmap("text-plain/application-x-generic", nullptr, QSize(20, 20)).isNull());
    EXPECT_TRUE(provider.requestPixmap("folder/inode-directory", nullptr, QSize(20, 20)).isNull());
  }
  EXPECT_TRUE(provider.requestPixmap("", nullptr, QSize()).isNull());
  EXPECT_TRUE(provider.requestPixmap("///", nullptr, QSize(-1, -1)).isNull());
  // Equivalent malformed URLs have the same candidate chain; misses stay cached across rows.
  EXPECT_TRUE(provider.requestPixmap("/folder//inode-directory/", nullptr, QSize(20, 20)).isNull());
  EXPECT_EQ(IconImageProviderTestAccess::themeLookupCount(provider), 4);
}
