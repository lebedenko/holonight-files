#include "directory_controller.h"
#include "directory_fixtures.h"

#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTest>

#include <gtest/gtest.h>

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
