#include "directory_controller.h"
#include "directory_fixtures.h"

#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTest>

#include <gtest/gtest.h>

using files_test::findPlaceRow;

namespace {
struct PlacesWindow {
  QTemporaryDir dir{files_test::fixturePattern("places-window")};
  DirectoryController controller;
  QQmlApplicationEngine engine;
  QQuickWindow* window = nullptr;
  QQuickItem* places = nullptr;
  QQuickItem* listing = nullptr;
  bool start() {
    files_test::writeFile(dir, "file.txt");
    QDir().mkdir(dir.filePath("target"));
    engine.setInitialProperties({{"controller", QVariant::fromValue(&controller)}});
    engine.loadFromModule("HolonightFiles", "Main");
    if (engine.rootObjects().isEmpty()) {
      return false;
    }
    window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    if (window == nullptr) {
      return false;
    }
    window->requestActivate();
    if (!QTest::qWaitForWindowActive(window)) {
      return false;
    }
    places = window->findChild<QQuickItem*>("placesListView");
    listing = window->findChild<QQuickItem*>("directoryListView");
    controller.open(dir.path());
    return places != nullptr && listing != nullptr &&
           QTest::qWaitFor([&] { return !controller.scanning() && row() != nullptr; });
  }
  [[nodiscard]] QQuickItem* row() const { return places->property("currentItem").value<QQuickItem*>(); }
  [[nodiscard]] QQuickItem* button() const { return row()->findChild<QQuickItem*>("placeDelegate"); }
  void target() const { row()->setProperty("path", dir.filePath("target")); }
  void click() const {
    const auto point = button()->mapToScene(QPointF(button()->width() / 2, button()->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, point);
  }
};
}  // namespace

TEST(PlacesWindow, KeyboardMouseHistorySelectionAndFocus) {
  PlacesWindow view;
  ASSERT_TRUE(view.start());
  view.target();
  EXPECT_FALSE(view.button()->property("highlighted").toBool());
  EXPECT_FALSE(view.button()->property("selected").toBool());
  // Reach the sidebar using actual Tab traversal from the focused listing.
  for (int i = 0; i < 20 && !view.places->hasActiveFocus(); ++i) {
    QTest::keyClick(view.window, Qt::Key_Tab);
  }
  ASSERT_TRUE(view.places->hasActiveFocus());
  QTest::keyClick(view.window, Qt::Key_Down);
  EXPECT_EQ(view.places->property("currentIndex").toInt(), 1);
  QTest::keyClick(view.window, Qt::Key_Up);
  EXPECT_EQ(view.places->property("currentIndex").toInt(), 0);
  QTest::keyClick(view.window, Qt::Key_Return);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !view.controller.scanning(); }));
  EXPECT_EQ(view.controller.currentPath(), view.dir.filePath("target"));
  EXPECT_TRUE(view.button()->property("highlighted").toBool());
  EXPECT_TRUE(view.listing->hasActiveFocus());
  view.controller.goBack();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !view.controller.scanning(); }));
  EXPECT_EQ(view.controller.currentPath(), view.dir.path());
  view.places->forceActiveFocus();
  QTest::keyClick(view.window, Qt::Key_Space);
  EXPECT_EQ(view.controller.currentPath(), view.dir.filePath("target"));
  view.controller.goBack();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !view.controller.scanning(); }));
  view.click();
  EXPECT_EQ(view.controller.currentPath(), view.dir.filePath("target"));
  EXPECT_TRUE(view.listing->hasActiveFocus());
}

TEST(PlacesWindow, GuardsBlockActivation) {
  PlacesWindow view;
  ASSERT_TRUE(view.start());
  view.target();
  for (const auto* key : {"v", "/", "a"}) {
    view.controller.handleKey(key);
    EXPECT_FALSE(view.places->isEnabled()) << key;
    view.click();
    EXPECT_EQ(view.controller.currentPath(), view.dir.path());
    QTest::keyClick(view.window, Qt::Key_Escape);
  }
  view.controller.handleKey("D");
  ASSERT_TRUE(view.controller.tasks()->hasPrompt());
  EXPECT_FALSE(view.places->isEnabled());
  view.click();
  EXPECT_EQ(view.controller.currentPath(), view.dir.path());
  QTest::keyClick(view.window, Qt::Key_Escape);
  view.controller.handleKey(" ");
  ASSERT_TRUE(view.controller.quickLookOpen());
  EXPECT_FALSE(view.places->isEnabled());
  view.click();
  EXPECT_EQ(view.controller.currentPath(), view.dir.path());
}

TEST(PlacesWindow, FallbackIconsAndShortWindow) {
  const auto theme = QIcon::themeName();
  const auto paths = QIcon::themeSearchPaths();
  const auto fallback = QIcon::fallbackThemeName();
  const auto fallbackPaths = QIcon::fallbackSearchPaths();
  const auto restore = qScopeGuard([&] {
    QIcon::setThemeName(theme);
    QIcon::setThemeSearchPaths(paths);
    QIcon::setFallbackThemeName(fallback);
    QIcon::setFallbackSearchPaths(fallbackPaths);
  });
  QIcon::setThemeName("places-missing-theme");
  QIcon::setThemeSearchPaths({});
  QIcon::setFallbackThemeName("");
  QIcon::setFallbackSearchPaths({});
  PlacesWindow view;
  ASSERT_TRUE(view.start());
  view.row()->setProperty("iconName", "places-deliberately-unavailable-icon");
  auto* icon = view.button()->findChild<QQuickItem*>("placeFallbackIcon");
  ASSERT_NE(icon, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    const auto images = icon->findChildren<QQuickItem*>();
    return icon->isVisible() && !images.isEmpty() && images.first()->property("status").toInt() == 1;
  }));
  EXPECT_FALSE(icon->property("hasError").toBool());
  EXPECT_GT(view.button()->mapToScene(QPointF()).x(), 0);
  view.window->setMinimumHeight(0);
  view.window->resize(800, 220);
  view.places->forceActiveFocus();
  for (int i = 0; i < view.controller.places()->rowCount(); ++i) {
    QTest::keyClick(view.window, Qt::Key_Down);
  }
  ASSERT_TRUE(QTest::qWaitFor([&] {
    const auto top = view.row()->mapToItem(view.places, QPointF()).y();
    return view.places->property("contentY").toReal() > 0 && top >= 0 &&
           top + view.row()->height() <= view.places->height() + 1;
  }));
  EXPECT_EQ(view.places->property("currentIndex").toInt(), view.controller.places()->rowCount() - 1);
  const auto capture = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
  if (!capture.isEmpty()) {
    QTest::qWait(100);
    EXPECT_TRUE(view.window->grabWindow().save(capture + "-places-short.png"));
  }
}

TEST(PlacesWindow, UnavailableBookmarkRendersMutedWithWarningBadgeAndAccessibleDescription) {
  QTemporaryDir configHome(files_test::fixturePattern("places-window-config"));
  QTemporaryDir dataHome(files_test::fixturePattern("places-window-data"));
  ASSERT_TRUE(configHome.isValid() && dataHome.isValid());
  QDir(dataHome.path()).mkpath("holonight/holonight-files");
  const auto missing = dataHome.filePath("missing-bm");
  files_test::writeFile(dataHome, "holonight/holonight-files/places.toml",
                        "version = 1\n[[bookmarks]]\npath = \"" + missing.toUtf8() + "\"\n");
  const files_test::ScopedXdgConfigHome configGuard(configHome.path());
  const files_test::ScopedXdgDataHome dataGuard(dataHome.path());
  PlacesWindow view;
  ASSERT_TRUE(view.start());
  const auto row = findPlaceRow(*view.controller.places(), missing);
  ASSERT_GE(row, 0);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    return view.controller.places()
               ->data(view.controller.places()->index(row), PlacesModel::StatusRole)
               .value<PlacesModel::Status>() == PlacesModel::Status::Unavailable;
  }));
  view.places->forceActiveFocus();
  for (int i = 0; i < row; ++i) {
    QTest::keyClick(view.window, Qt::Key_Down);
  }
  ASSERT_EQ(view.places->property("currentIndex").toInt(), row);
  auto* delegate = view.button();
  ASSERT_NE(delegate, nullptr);
  EXPECT_DOUBLE_EQ(delegate->property("opacity").toDouble(), 0.5);
  const auto accessibleName =
      QQmlProperty(delegate, QStringLiteral("Accessible.name"), qmlContext(delegate)).read().toString();
  EXPECT_EQ(accessibleName, "missing-bm");
  const auto accessibleDescription =
      QQmlProperty(delegate, QStringLiteral("Accessible.description"), qmlContext(delegate)).read().toString();
  EXPECT_TRUE(accessibleDescription.contains("unavailable")) << accessibleDescription.toStdString();
  auto* warningTheme = delegate->findChild<QQuickItem*>("placeWarningThemeIcon");
  auto* warningFallback = delegate->findChild<QQuickItem*>("placeWarningFallbackIcon");
  ASSERT_TRUE(warningTheme != nullptr || warningFallback != nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    return (warningTheme != nullptr && warningTheme->isVisible()) ||
           (warningFallback != nullptr && warningFallback->isVisible());
  }));
  // Enter activates the bookmark; the directory still doesn't exist, so no navigation happens.
  const auto before = view.controller.currentPath();
  QTest::keyClick(view.window, Qt::Key_Return);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return view.controller.statusMessage() == QStringLiteral("Location is currently unavailable"); }));
  EXPECT_EQ(view.controller.currentPath(), before);
}

TEST(PlacesWindow, ExtraSpacingGapPrecedesFirstBookmarkRow) {
  QTemporaryDir configHome(files_test::fixturePattern("places-window-gap-config"));
  QTemporaryDir dataHome(files_test::fixturePattern("places-window-gap-data"));
  ASSERT_TRUE(configHome.isValid() && dataHome.isValid());
  const auto homeDir = QDir::homePath();
  files_test::writeFile(configHome, "user-dirs.dirs", "XDG_DESKTOP_DIR=\"" + homeDir.toUtf8() + "\"\n");
  QDir(dataHome.path()).mkpath("holonight/holonight-files");
  const auto bm1 = dataHome.filePath("bm1");
  const auto bm2 = dataHome.filePath("bm2");
  QDir().mkpath(bm1);
  QDir().mkpath(bm2);
  files_test::writeFile(
      dataHome, "holonight/holonight-files/places.toml",
      "version = 1\n[[bookmarks]]\npath = \"" + bm1.toUtf8() + "\"\n[[bookmarks]]\npath = \"" + bm2.toUtf8() + "\"\n");
  const files_test::ScopedXdgConfigHome configGuard(configHome.path());
  const files_test::ScopedXdgDataHome dataGuard(dataHome.path());
  PlacesWindow view;
  ASSERT_TRUE(view.start());
  // XDG_DESKTOP_DIR resolves to $HOME here, so REQ-F-004 hides it: Home, bm1, bm2 (3 rows).
  ASSERT_TRUE(QTest::qWaitFor([&] { return view.controller.places()->rowCount() == 3; }));
  const auto bm1Row = findPlaceRow(*view.controller.places(), bm1);
  const auto bm2Row = findPlaceRow(*view.controller.places(), bm2);
  ASSERT_EQ(bm1Row, 1);
  ASSERT_EQ(bm2Row, 2);
  const auto itemAt = [&](int index) {
    QQuickItem* found = nullptr;
    QMetaObject::invokeMethod(view.places, "itemAtIndex", Qt::DirectConnection, qReturnArg(found), Q_ARG(int, index));
    return found;
  };
  auto* homeRowItem = itemAt(0);
  auto* bm1RowItem = itemAt(1);
  auto* bm2RowItem = itemAt(2);
  ASSERT_TRUE(homeRowItem != nullptr && bm1RowItem != nullptr && bm2RowItem != nullptr);
  // The extra gap lives inside the (taller) first-bookmark row's own box -- its visible delegate
  // is bottom-anchored -- so it must be measured via each row's actual delegate position, not the
  // row boxes' own y/height (which ListView spaces uniformly regardless of each box's content).
  const auto delegateTop = [&](QQuickItem* rowItem) {
    auto* delegate = rowItem->findChild<QQuickItem*>("placeDelegate");
    return delegate->mapToItem(view.places, QPointF()).y();
  };
  const auto delegateBottom = [&](QQuickItem* rowItem) {
    auto* delegate = rowItem->findChild<QQuickItem*>("placeDelegate");
    return delegate->mapToItem(view.places, QPointF(0, delegate->height())).y();
  };
  const auto gapBeforeBookmarks = delegateTop(bm1RowItem) - delegateBottom(homeRowItem);
  const auto gapBetweenBookmarks = delegateTop(bm2RowItem) - delegateBottom(bm1RowItem);
  const auto compactSpacing = view.places->property("spacing").toReal();
  EXPECT_NEAR(gapBeforeBookmarks - gapBetweenBookmarks, compactSpacing, 1.0);
}

TEST(PlacesWindow, NoSeparatorOrBookmarksHeadingExistsInThePanel) {
  PlacesWindow view;
  ASSERT_TRUE(view.start());
  auto* panel = view.places->parentItem();
  ASSERT_NE(panel, nullptr);
  for (auto* child : panel->findChildren<QQuickItem*>()) {
    EXPECT_FALSE(QString::fromUtf8(child->metaObject()->className()).contains("Separator"))
        << child->metaObject()->className();
    const auto text = child->property("text");
    if (text.isValid()) {
      EXPECT_FALSE(text.toString().contains("Bookmarks")) << text.toString().toStdString();
    }
    const auto rawText = child->property("rawText");
    if (rawText.isValid()) {
      EXPECT_FALSE(rawText.toString().contains("Bookmarks")) << rawText.toString().toStdString();
    }
  }
}

TEST(PlacesWindow, MissingPlaceUsesDirectoryError) {
  PlacesWindow view;
  ASSERT_TRUE(view.start());
  view.row()->setProperty("path", view.dir.filePath("missing"));
  view.click();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !view.controller.scanning(); }));
  EXPECT_EQ(view.controller.currentPath(), view.dir.filePath("missing"));
  EXPECT_FALSE(view.controller.directoryError().isEmpty());
  auto* error = view.window->findChild<QQuickItem*>("directoryErrorState");
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->isVisible());
  EXPECT_TRUE(view.button()->property("highlighted").toBool());
}
