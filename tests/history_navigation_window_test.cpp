#include "engine_setup.h"
// docs/sdd/navigation-history: the header back/forward buttons and the Ctrl+O/Ctrl+I shortcuts in a
// real rendered window (DESIGN.md §8.3). Controller-level history semantics live in
// directory_controller_test.cpp; this file only proves the QML wiring delivers them.

#include "directory_controller.h"
#include "directory_fixtures.h"

#include <QDir>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTemporaryDir>
#include <QTest>

#include <gtest/gtest.h>

namespace {

bool settled(const DirectoryController& controller) {
  return QTest::qWaitFor([&] { return !controller.scanning(); });
}

struct HistoryWindow {
  QTemporaryDir dir{files_test::fixturePattern("history-window")};
  QString a = dir.filePath("a");
  QString b = dir.filePath("b");
  QString c = dir.filePath("c");
  DirectoryController controller;
  QQmlApplicationEngine engine;
  QQuickWindow* window = nullptr;
  QQuickItem* back = nullptr;
  QQuickItem* forward = nullptr;
  QQuickItem* list = nullptr;

  HistoryWindow() {
    initializeFilesEngine(engine);
    for (const auto& path : {a, b, c}) {
      QDir().mkpath(path);
      for (const auto* name : {"1.txt", "2.txt", "3.txt"}) {
        files_test::writeFile(dir, QDir(dir.path()).relativeFilePath(QDir(path).filePath(name)));
      }
    }
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    engine.loadFromModule("HolonightFiles", "Main");
  }

  // Loads the window with history [a, b, c] (current c) and the listing focused.
  bool start() {
    if (engine.rootObjects().size() != 1) {
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
    back = window->findChild<QQuickItem*>("historyBackButton");
    forward = window->findChild<QQuickItem*>("historyForwardButton");
    list = window->findChild<QQuickItem*>("directoryListView");
    if (back == nullptr || forward == nullptr || list == nullptr) {
      return false;
    }
    for (const auto& path : {a, b, c}) {
      controller.open(path);
      if (!settled(controller)) {
        return false;
      }
    }
    return QTest::qWaitFor([&] { return list->hasActiveFocus(); });
  }

  void click(QQuickItem* item) const {
    const auto center = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center);
  }

  [[nodiscard]] static qreal sceneRight(QQuickItem* item) { return item->mapToScene(QPointF(item->width(), 0)).x(); }
};

}  // namespace

TEST(WindowHistoryNavigation, BackButtonSitsLeftOfForwardButtonWithinSidebarWidth) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  const auto sidebarWidth = rendered.window->property("sidebarWidth").toReal();
  const auto* header = rendered.window->findChild<QQuickItem*>("appHeaderBar");
  ASSERT_NE(header, nullptr);
  const auto backLeft = rendered.back->mapToScene(QPointF()).x();
  const auto forwardLeft = rendered.forward->mapToScene(QPointF()).x();
  EXPECT_GE(backLeft, 0);
  EXPECT_LE(rendered.sceneRight(rendered.back), forwardLeft + 0.5);
  EXPECT_LT(rendered.sceneRight(rendered.forward), sidebarWidth);
  for (auto* button : {rendered.back, rendered.forward}) {
    const auto centerY = button->mapToScene(QPointF(0, button->height() / 2)).y();
    EXPECT_NEAR(centerY, header->mapToScene(QPointF(0, header->height() / 2)).y(), 1);
  }
}

TEST(WindowHistoryNavigation, ButtonsEnabledStateTracksCanGoBackForwardModeAndPrompt) {
  QTemporaryDir home(files_test::fixturePattern("history-window-home"));
  const files_test::ScopedXdgDataHome guard(home.path());
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  EXPECT_TRUE(rendered.back->isEnabled());      // canGoBack, NORMAL, no prompt
  EXPECT_FALSE(rendered.forward->isEnabled());  // canGoForward false

  rendered.controller.goBack();
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_TRUE(rendered.back->isEnabled());
  EXPECT_TRUE(rendered.forward->isEnabled());

  QTest::keyClick(rendered.window, Qt::Key_V);
  ASSERT_EQ(rendered.controller.vim()->currentMode(), VimModeController::Mode::Visual);
  EXPECT_FALSE(rendered.back->isEnabled());
  EXPECT_FALSE(rendered.forward->isEnabled());
  QTest::keyClick(rendered.window, Qt::Key_Escape);
  ASSERT_EQ(rendered.controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_TRUE(rendered.back->isEnabled());

  QTest::keyClick(rendered.window, 'D', Qt::ShiftModifier);
  ASSERT_TRUE(rendered.controller.tasks()->hasPrompt());
  EXPECT_FALSE(rendered.back->isEnabled());
  EXPECT_FALSE(rendered.forward->isEnabled());
  QTest::keyClick(rendered.window, Qt::Key_N);
  ASSERT_FALSE(rendered.controller.tasks()->hasPrompt());
  EXPECT_TRUE(rendered.back->isEnabled());

  rendered.controller.goBack();
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_FALSE(rendered.back->isEnabled());  // canGoBack false at the first entry
  EXPECT_TRUE(rendered.forward->isEnabled());
}

TEST(WindowHistoryNavigation, ClickingBackButtonNavigatesLikeCtrlO) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  rendered.controller.handleKey("2");  // a click is one step regardless of a pending count
  rendered.click(rendered.back);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.b);
  rendered.click(rendered.back);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.a);
}

TEST(WindowHistoryNavigation, ClickingForwardButtonNavigatesLikeCtrlI) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  rendered.controller.goBack();
  ASSERT_TRUE(settled(rendered.controller));
  rendered.controller.goBack();
  ASSERT_TRUE(settled(rendered.controller));
  ASSERT_EQ(rendered.controller.currentPath(), rendered.a);
  rendered.click(rendered.forward);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.b);
  rendered.click(rendered.forward);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
}

TEST(WindowHistoryNavigation, ClickingButtonLeavesFocusOnListingAndVimKeysStillWork) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  rendered.controller.handleKey("G");
  rendered.click(rendered.back);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_TRUE(rendered.list->hasActiveFocus());
  EXPECT_FALSE(rendered.back->hasActiveFocus());
  QTest::keyClick(rendered.window, Qt::Key_J);
  EXPECT_EQ(rendered.controller.cursorRow(), 1);
  rendered.click(rendered.forward);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.cursorRow(), 2);  // "3.txt", restored from before the back click
  EXPECT_TRUE(rendered.list->hasActiveFocus());
  QTest::keyClick(rendered.window, Qt::Key_K);
  EXPECT_EQ(rendered.controller.cursorRow(), 1);
  QTest::keyClick(rendered.window, Qt::Key_H);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.dir.path());
}

TEST(WindowHistoryNavigation, BothButtonsFitWithinSidebarAtMinimumWindowWidth) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* breadcrumb = rendered.window->findChild<QQuickItem*>("breadcrumbContainer");
  ASSERT_NE(breadcrumb, nullptr);
  rendered.window->resize(420, 400);
  ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.window->width() == 420; }));
  QTest::qWait(20);
  for (auto* button : {rendered.back, rendered.forward}) {
    EXPECT_GT(button->width(), 0);
    EXPECT_GT(button->height(), 0);
    EXPECT_TRUE(button->isVisible());
    EXPECT_LE(rendered.sceneRight(button), breadcrumb->mapToScene(QPointF()).x());
    EXPECT_LE(rendered.sceneRight(button), rendered.window->property("sidebarWidth").toReal());
  }
}

TEST(WindowHistoryNavigation, BreadcrumbContainerXUnchangedAtDefaultAndMinimumWidth) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* header = rendered.window->findChild<QQuickItem*>("appHeaderBar");
  auto* breadcrumb = rendered.window->findChild<QQuickItem*>("breadcrumbContainer");
  ASSERT_NE(header, nullptr);
  ASSERT_NE(breadcrumb, nullptr);
  const auto expectedX = [&] {
    return header->property("breadcrumbLeftInset").toReal() - header->property("breadcrumbPadding").toReal();
  };
  for (const int width : {1000, 420}) {
    rendered.window->resize(width, 400);
    ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.window->width() == width; }));
    QTest::qWait(20);
    EXPECT_NEAR(breadcrumb->x(), expectedX(), 1) << width;
  }
  const auto before = breadcrumb->mapToScene(QPointF()).x();
  rendered.window->setProperty("sidebarWidth", 240);
  QTest::qWait(20);
  EXPECT_NEAR(breadcrumb->mapToScene(QPointF()).x() - before, 40, 1);
  EXPECT_NEAR(breadcrumb->x(), expectedX(), 1);
}

TEST(WindowHistoryNavigation, ButtonsHaveBackAndForwardAccessibleNames) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  const auto accessibleName = [](QQuickItem* item) {
    return QQmlProperty(item, QStringLiteral("Accessible.name"), qmlContext(item)).read().toString();
  };
  EXPECT_EQ(accessibleName(rendered.back), "Back");
  EXPECT_EQ(accessibleName(rendered.forward), "Forward");
}

TEST(WindowHistoryNavigation, HistoryButtonIconsLoadFromBundledResources) {
  const auto previousTheme = QIcon::themeName();
  const auto restoreTheme = qScopeGuard([&] { QIcon::setThemeName(previousTheme); });
  QIcon::setThemeName(QStringLiteral("does-not-exist"));
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  for (auto* button : {rendered.back, rendered.forward}) {
    const auto source = QQmlProperty(button, QStringLiteral("icon.source")).read().toUrl();
    EXPECT_EQ(source.scheme(), "qrc") << source.toString().toStdString();
    auto* icon = button->findChild<QQuickItem*>("hnIconButtonIcon");
    ASSERT_NE(icon, nullptr);
    QQuickItem* image = nullptr;
    for (auto* child : icon->childItems()) {
      if (child->property("status").isValid()) {
        image = child;
      }
    }
    ASSERT_NE(image, nullptr);
    // QQuickImageBase::Ready == 1.
    EXPECT_TRUE(QTest::qWaitFor([&] { return image->property("status").toInt() == 1; }))
        << source.toString().toStdString();
    EXPECT_FALSE(icon->property("hasError").toBool());
  }
}

TEST(WindowHistoryNavigation, CtrlOAndCtrlIKeyClicksNavigateFromListingFocus) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  QTest::keyClick(rendered.window, Qt::Key_O, Qt::ControlModifier);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.b);
  EXPECT_TRUE(rendered.list->hasActiveFocus());
  QTest::keyClick(rendered.window, Qt::Key_O, Qt::ControlModifier);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.a);
  QTest::keyClick(rendered.window, Qt::Key_I, Qt::ControlModifier);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.b);
  EXPECT_TRUE(rendered.list->hasActiveFocus());
  // Plain o/i keep their NORMAL meaning.
  EXPECT_EQ(rendered.controller.vim()->currentMode(), VimModeController::Mode::Normal);
}

TEST(WindowHistoryNavigation, CtrlIDoesNotMoveFocusOrNavigateInInsertMode) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  rendered.controller.goBack();
  ASSERT_TRUE(settled(rendered.controller));
  ASSERT_TRUE(rendered.controller.canGoForward());
  QTest::keyClick(rendered.window, Qt::Key_I);
  ASSERT_EQ(rendered.controller.vim()->currentMode(), VimModeController::Mode::Insert);
  // Delegates are not QObject children of the window, so reach the editor through focus.
  ASSERT_TRUE(QTest::qWaitFor([&] {
    auto* focused = rendered.window->activeFocusItem();
    return focused != nullptr && focused->objectName() == "inlineNameEditor";
  }));
  auto* editor = rendered.window->activeFocusItem();
  const auto text = editor->property("text").toString();
  QTest::keyClick(rendered.window, Qt::Key_I, Qt::ControlModifier);
  QTest::keyClick(rendered.window, Qt::Key_O, Qt::ControlModifier);
  QTest::qWait(20);
  EXPECT_EQ(rendered.controller.currentPath(), rendered.b);
  EXPECT_EQ(rendered.controller.vim()->currentMode(), VimModeController::Mode::Insert);
  EXPECT_TRUE(editor->hasActiveFocus());
  EXPECT_EQ(editor->property("text").toString(), text);
  QTest::keyClick(rendered.window, Qt::Key_Escape);
  ASSERT_EQ(rendered.controller.vim()->currentMode(), VimModeController::Mode::Normal);
  QTest::keyClick(rendered.window, Qt::Key_V);
  QTest::keyClick(rendered.window, Qt::Key_I, Qt::ControlModifier);
  EXPECT_EQ(rendered.controller.currentPath(), rendered.b);  // VISUAL is inert as well
}

TEST(WindowHistoryNavigation, CountThenCtrlOTraversesCountEntries) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  QTest::keyClick(rendered.window, Qt::Key_2);
  QTest::keyPress(rendered.window, Qt::Key_Control);  // a lone modifier event must not eat the count
  QTest::keyClick(rendered.window, Qt::Key_O, Qt::ControlModifier);
  QTest::keyRelease(rendered.window, Qt::Key_Control);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.a);
  QTest::keyClick(rendered.window, Qt::Key_J);  // count consumed: moves one row
  EXPECT_EQ(rendered.controller.cursorRow(), 1);
  QTest::keyClick(rendered.window, Qt::Key_9);
  QTest::keyClick(rendered.window, Qt::Key_I, Qt::ControlModifier);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
}

TEST(WindowHistoryNavigation, QuickLookOpenThenCtrlOClosesItAndNavigates) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* popup = rendered.window->findChild<QObject*>("quickLookOverlay");
  ASSERT_NE(popup, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !rendered.controller.preview()->busy(); }));
  QTest::keyClick(rendered.window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->property("opened").toBool(); }));
  QTest::keyClick(rendered.window, Qt::Key_O, Qt::ControlModifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }));
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_FALSE(rendered.controller.quickLookOpen());
  EXPECT_EQ(rendered.controller.currentPath(), rendered.b);
  EXPECT_TRUE(QTest::qWaitFor([&] { return rendered.list->hasActiveFocus(); }));
}
