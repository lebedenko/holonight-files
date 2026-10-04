#pragma once

#include <QColor>
#include <QDir>
#include <QFile>
#include <QIcon>
#include <QImage>
#include <QScopeGuard>
#include <QTemporaryDir>

namespace files_test {

// Tests may replace the process-wide icon theme; production code never does (REQ-F-018).
[[nodiscard]] inline auto scopedIconTheme(const QStringList& searchPaths, const QString& themeName) {
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
[[nodiscard]] inline bool writeTinyTheme(const QTemporaryDir& root) {
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

}  // namespace files_test
