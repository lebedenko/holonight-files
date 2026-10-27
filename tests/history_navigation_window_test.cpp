#include "engine_setup.h"
// docs/sdd/navigation-history: the header back/forward buttons and the Ctrl+O/Ctrl+I shortcuts in a
// real rendered window (DESIGN.md §8.3). Controller-level history semantics live in
// directory_controller_test.cpp; this file only proves the QML wiring delivers them.

#include "directory_controller.h"
#include "directory_fixtures.h"
#include "icon_theme_fixtures.h"
#include "settings/files_preferences.h"
#include "settings_fixtures.h"
#include "sidebar_navigator.h"

#include <QDir>
#include <QFile>
#include <QIcon>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
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

  explicit HistoryWindow(FilesPreferences* preferences = nullptr) {
    initializeFilesEngine(engine);
    for (const auto& path : {a, b, c}) {
      QDir().mkpath(path);
      for (const auto* name : {"1.txt", "2.txt", "3.txt"}) {
        files_test::writeFile(dir, QDir(dir.path()).relativeFilePath(QDir(path).filePath(name)));
      }
    }
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    if (preferences != nullptr) {
      engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)},
                                   {QStringLiteral("preferences"), QVariant::fromValue(preferences)}});
    }
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

TEST(WindowHistoryNavigation, NormalBreadcrumbFitsExtraSmallHeader) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* header = rendered.window->findChild<QQuickItem*>("appHeaderBar");
  auto* breadcrumb = rendered.window->findChild<QQuickItem*>("breadcrumbContainer");
  ASSERT_NE(header, nullptr);
  ASSERT_NE(breadcrumb, nullptr);
  EXPECT_EQ(header->height(), 42);
  EXPECT_EQ(header->property("sizeRole").toInt(), 4);
  EXPECT_EQ(header->property("verticalPadding").toReal(), 5);
  EXPECT_EQ(breadcrumb->height(), 32);
  EXPECT_NEAR(breadcrumb->mapToItem(header, QPointF()).y(), 5, 0.5);
  EXPECT_NEAR(breadcrumb->mapToItem(header, QPointF(0, breadcrumb->height())).y(), 37, 0.5);
  auto* bar = rendered.window->findChild<QQuickItem*>("breadcrumbLabel");
  ASSERT_NE(bar, nullptr);
  EXPECT_EQ(bar->property("sizeRole").toInt(), 1);
  int segments = 0;
  const auto checkGeometry = [&](auto&& self, QQuickItem* item) -> void {
    if (item->isVisible() && item->objectName().startsWith("hnBreadcrumbSegment")) {
      ++segments;
      EXPECT_EQ(item->height(), 28);
      EXPECT_GE(item->mapToItem(bar, QPointF()).y(), -0.5);
      EXPECT_LE(item->mapToItem(bar, QPointF(0, item->height())).y(), bar->height() + 0.5);
      auto* content = item->property("contentItem").value<QQuickItem*>();
      ASSERT_NE(content, nullptr);
      EXPECT_TRUE(QTest::qWaitFor([&] {
        return qAbs(content->mapToItem(bar, QPointF(0, content->height() / 2)).y() - bar->height() / 2) <= 0.5;
      }));
    }
    for (auto* child : item->childItems()) {
      self(self, child);
    }
  };
  checkGeometry(checkGeometry, bar);
  EXPECT_GT(segments, 0);
  for (auto* button : {rendered.back, rendered.forward}) {
    EXPECT_EQ(button->width(), 28);
    EXPECT_EQ(button->height(), 28);
    auto* icon = button->findChild<QQuickItem*>("hnIconButtonIcon");
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(icon->width(), 16);
    EXPECT_EQ(icon->height(), 16);
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

  QTest::keyClick(rendered.window, Qt::Key_J);
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
  const auto expectedX = [&] { return header->property("breadcrumbLeftInset").toReal(); };
  for (const int width : {1000, 420}) {
    rendered.window->resize(width, 400);
    ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.window->width() == width; }));
    QTest::qWait(20);
    EXPECT_NEAR(breadcrumb->x(), expectedX(), 1) << width;
  }
  const auto before = breadcrumb->mapToScene(QPointF()).x();
  rendered.window->setProperty("sidebarWidth", 260);
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

TEST(WindowHistoryNavigation, HistoryButtonIconsLoadFromTheme) {
  QTemporaryDir theme(files_test::fixturePattern("history-icons"));
  ASSERT_TRUE(files_test::writeTinyTheme(theme));
  const auto directory = theme.filePath("tiny-test-theme/16x16/places/");
  for (const auto* name : {"go-previous", "go-next"}) {
    ASSERT_TRUE(QFile::copy(directory + "folder.png", directory + name + ".png"));
  }
  const auto restoreTheme = files_test::scopedIconTheme({theme.path()}, "tiny-test-theme");
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  for (auto* button : {rendered.back, rendered.forward}) {
    const auto source = QQmlProperty(button, QStringLiteral("icon.source")).read().toUrl();
    EXPECT_TRUE(source.isEmpty());
    EXPECT_EQ(QQmlProperty(button, QStringLiteral("icon.name")).read().toString(),
              button == rendered.back ? "go-previous" : "go-next");
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
  QTest::keyClick(rendered.window, Qt::Key_J);
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

TEST(WindowHistoryNavigation, QuickLookConsumesHistoryShortcutsUntilClosed) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  QTest::keyClick(rendered.window, Qt::Key_O, Qt::ControlModifier);
  ASSERT_TRUE(settled(rendered.controller));
  ASSERT_EQ(rendered.controller.currentPath(), rendered.b);  // both history directions have a target
  auto* popup = rendered.window->findChild<QObject*>("quickLookOverlay");
  ASSERT_NE(popup, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !rendered.controller.preview()->busy(); }));
  QTest::keyClick(rendered.window, Qt::Key_J);
  ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.controller.preview()->quickLookEligible(); }, 5000));
  QTest::keyClick(rendered.window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->property("opened").toBool(); }));
  const auto row = rendered.controller.cursorRow();
  const auto name = rendered.controller.preview()->name();
  for (const auto key : {Qt::Key_O, Qt::Key_I}) {
    QTest::keyClick(rendered.window, key, Qt::ControlModifier);
    EXPECT_TRUE(popup->property("opened").toBool());
    EXPECT_TRUE(rendered.controller.quickLookOpen());
    EXPECT_EQ(rendered.controller.currentPath(), rendered.b);
    EXPECT_EQ(rendered.controller.cursorRow(), row);
    EXPECT_EQ(rendered.controller.preview()->name(), name);
  }
  QTest::keyClick(rendered.window, Qt::Key_Escape);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }));
  ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.list->hasActiveFocus(); }));
  QTest::keyClick(rendered.window, Qt::Key_O, Qt::ControlModifier);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_FALSE(rendered.controller.quickLookOpen());
  EXPECT_EQ(rendered.controller.currentPath(), rendered.a);
  QTest::keyClick(rendered.window, Qt::Key_I, Qt::ControlModifier);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.b);
  EXPECT_TRUE(QTest::qWaitFor([&] { return rendered.list->hasActiveFocus(); }));
}

TEST(WindowHistoryNavigation, MouseHistoryNavigatesOnceAcrossWindowRegionsAndPreservesFocus) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  for (const auto* region : {"appHeaderBar", "directoryListing", "sidebarContainer", "previewContainer"}) {
    auto* item = rendered.window->findChild<QQuickItem*>(region);
    ASSERT_NE(item, nullptr);
    const auto point = item->mapToScene(QPointF(item->width() / 2, item->height() / 2)).toPoint();
    ASSERT_TRUE(rendered.list->hasActiveFocus());
    rendered.controller.handleKey("2");
    QTest::mouseClick(rendered.window, Qt::BackButton, Qt::NoModifier, point);
    ASSERT_TRUE(settled(rendered.controller));
    EXPECT_EQ(rendered.controller.currentPath(), rendered.b);
    EXPECT_TRUE(rendered.list->hasActiveFocus());
    QTest::mouseClick(rendered.window, Qt::ForwardButton, Qt::NoModifier, point);
    ASSERT_TRUE(settled(rendered.controller));
    EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
    QTest::mouseClick(rendered.window, Qt::ForwardButton, Qt::NoModifier, point);
    EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
  }
}

TEST(WindowHistoryNavigation, MouseHistoryIsBlockedByEditingQuickLookAndFinder) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  for (const auto* key : {"v", "/", "i", " "}) {
    rendered.controller.handleKey(key);
    ASSERT_FALSE(rendered.controller.historyNavigationEnabled());
    QTest::mouseClick(rendered.window, Qt::BackButton);
    EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
    rendered.controller.handleKey("Escape");
  }
  ASSERT_TRUE(rendered.controller.setProperty("finderOpen", true));
  EXPECT_FALSE(rendered.controller.historyNavigationEnabled());
  QTest::mouseClick(rendered.window, Qt::BackButton);
  EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
  rendered.controller.goBack();
  EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
}

TEST(WindowHistoryNavigation, BreadcrumbFrameTracksListingEdges) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* bar = rendered.window->findChild<QQuickItem*>("breadcrumbLabel");
  auto* listing = rendered.window->findChild<QQuickItem*>("directoryListing");
  auto* close = rendered.window->findChild<QQuickItem*>("closeButton");
  ASSERT_NE(bar, nullptr);
  ASSERT_NE(listing, nullptr);
  ASSERT_NE(close, nullptr);
  auto* selector = rendered.window->findChild<QQuickItem*>("viewSelector");
  ASSERT_NE(selector, nullptr);
  const auto checkSelector = [&] {
    EXPECT_NEAR(selector->mapToScene(QPointF()).x() - HistoryWindow::sceneRight(bar), 8, 0.5);
    EXPECT_LE(HistoryWindow::sceneRight(selector) + 8, close->mapToScene(QPointF()).x() + 0.5);
    EXPECT_NEAR(selector->mapToScene(QPointF(0, selector->height() / 2)).y(),
                bar->mapToScene(QPointF(0, bar->height() / 2)).y(), 0.5);
    EXPECT_NEAR(HistoryWindow::sceneRight(close), rendered.window->width() - 12, 0.5);
  };
  auto* preview = rendered.window->findChild<QQuickItem*>("previewContainer");
  ASSERT_NE(preview, nullptr);
  for (int width : {1000, 420, 800}) {
    rendered.window->setWidth(width);
    for (int sidebar : {160, 220, 260}) {
      rendered.window->setProperty("sidebarWidth", sidebar);
      ASSERT_TRUE(QTest::qWaitFor([&] {
        return qAbs(bar->mapToScene(QPointF()).x() - listing->mapToScene(QPointF()).x()) < 0.5 &&
               HistoryWindow::sceneRight(bar) <= HistoryWindow::sceneRight(listing) + 0.5 &&
               qAbs(HistoryWindow::sceneRight(close) - (width - 12)) < 0.5;
      }));
      checkSelector();
    }
  }
  rendered.window->setWidth(1000);
  ASSERT_TRUE(QTest::qWaitFor([&] { return qAbs(HistoryWindow::sceneRight(close) - 988) < 0.5; }));
  for (int previewWidth : {220, 300, 260}) {
    ASSERT_TRUE(QQmlProperty::write(preview, "Controls.SplitView.preferredWidth", previewWidth, qmlContext(preview)));
    ASSERT_TRUE(QTest::qWaitFor([&] { return qAbs(preview->width() - previewWidth) < 0.5; }));
    EXPECT_LE(HistoryWindow::sceneRight(bar), HistoryWindow::sceneRight(listing) + 0.5);
    checkSelector();
  }
}

TEST(WindowHistoryNavigation, ViewSelectorHighlightsControllerSelectionThroughHoverAndModalGuard) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* list = rendered.window->findChild<QQuickItem*>("listViewButton");
  auto* icons = rendered.window->findChild<QQuickItem*>("iconViewButton");
  ASSERT_NE(list, nullptr);
  ASSERT_NE(icons, nullptr);
  const auto paletteColor = [&](const QString& token) {
    QQmlExpression expression(qmlContext(list), list, "HoloniightPalette." + token);
    return expression.evaluate().value<QColor>();
  };
  const auto background = [](QQuickItem* button) { return button->property("background").value<QQuickItem*>(); };
  for (bool iconView : {false, true, false}) {
    rendered.controller.setIconView(iconView);
    auto* selected = iconView ? icons : list;
    auto* other = iconView ? list : icons;
    ASSERT_TRUE(QTest::qWaitFor([&] { return selected->property("checked").toBool(); }));
    EXPECT_FALSE(other->property("checked").toBool());
    EXPECT_EQ(background(other)->property("color").value<QColor>(), QColor(Qt::transparent));
    EXPECT_EQ(selected->property("focusPolicy").toInt(), Qt::NoFocus);
    auto* icon = selected->findChild<QQuickItem*>("hnIconButtonIcon");
    ASSERT_NE(icon, nullptr);
    ASSERT_NE(background(selected), nullptr);
    EXPECT_EQ(icon->property("resolvedColor").value<QColor>(), paletteColor("primary"));
    EXPECT_EQ(background(selected)->property("color").value<QColor>(), paletteColor("surfaceRaised"));
    QTest::mouseMove(rendered.window,
                     selected->mapToScene(QPointF(selected->width() / 2, selected->height() / 2)).toPoint());
    ASSERT_TRUE(QTest::qWaitFor([&] { return selected->property("hovered").toBool(); }));
    EXPECT_TRUE(selected->property("checked").toBool());
    EXPECT_EQ(icon->property("resolvedColor").value<QColor>(), paletteColor("primary"));
    EXPECT_EQ(background(selected)->property("color").value<QColor>(), paletteColor("surfaceHover"));
    const auto center = selected->mapToScene(QPointF(selected->width() / 2, selected->height() / 2)).toPoint();
    QTest::mousePress(rendered.window, Qt::LeftButton, Qt::NoModifier, center);
    EXPECT_TRUE(selected->property("checked").toBool());
    EXPECT_EQ(background(selected)->property("color").value<QColor>(), paletteColor("surface"));
    QTest::mouseRelease(rendered.window, Qt::LeftButton, Qt::NoModifier, center);
    rendered.controller.setProperty("finderOpen", true);
    ASSERT_FALSE(selected->isEnabled());
    EXPECT_TRUE(selected->property("checked").toBool());
    EXPECT_EQ(icon->property("resolvedColor").value<QColor>(), icon->property("disabledColor").value<QColor>());
    EXPECT_EQ(background(selected)->property("color").value<QColor>(), paletteColor("surfaceRaised"));
    rendered.controller.setProperty("finderOpen", false);
    QTest::mouseMove(rendered.window, QPoint(1, 1));
    ASSERT_TRUE(QTest::qWaitFor([&] { return !selected->property("hovered").toBool(); }));
  }
}

TEST(WindowHistoryNavigation, CloseButtonClosesOwningWindow) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* close = rendered.window->findChild<QQuickItem*>("closeButton");
  ASSERT_NE(close, nullptr);
  rendered.click(close);
  EXPECT_FALSE(rendered.window->isVisible());
}

TEST(WindowPanelFocus, RoutesEveryDirectionWithoutChangingBrowsingState) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* sidebar = rendered.window->findChild<QQuickItem*>("sidebarPanel");
  auto* bar = rendered.window->findChild<QQuickItem*>("breadcrumbLabel");
  ASSERT_NE(sidebar, nullptr);
  ASSERT_NE(bar, nullptr);
  const auto currentName = QString("hnBreadcrumbSegment%1").arg(rendered.controller.breadcrumbLabels().size() - 1);
  const auto findItem = [](auto&& self, QQuickItem* item, const QString& name) -> QQuickItem* {
    if (item->objectName() == name) {
      return item;
    }
    for (auto* child : item->childItems()) {
      if (auto* found = self(self, child, name)) {
        return found;
      }
    }
    return nullptr;
  };
  auto* current = findItem(findItem, bar, currentName);
  ASSERT_NE(current, nullptr);
  const auto row = rendered.controller.cursorRow();
  const auto section = rendered.controller.sidebarNavigator()->section();
  const auto index = rendered.controller.sidebarNavigator()->index();
  rendered.controller.handleKey("2");
  struct Route {
    QQuickItem* from;
    Qt::Key key;
    QQuickItem* to;
  };
  for (int repeat = 0; repeat < 2; ++repeat) {
    for (const auto& route : {
             Route{.from = rendered.list, .key = Qt::Key_H, .to = sidebar},
             {.from = rendered.list, .key = Qt::Key_J, .to = rendered.list},
             {.from = rendered.list, .key = Qt::Key_K, .to = current},
             {.from = rendered.list, .key = Qt::Key_L, .to = rendered.list},
             {.from = sidebar, .key = Qt::Key_H, .to = sidebar},
             {.from = sidebar, .key = Qt::Key_J, .to = sidebar},
             {.from = sidebar, .key = Qt::Key_K, .to = current},
             {.from = sidebar, .key = Qt::Key_L, .to = rendered.list},
             {.from = current, .key = Qt::Key_H, .to = sidebar},
             {.from = current, .key = Qt::Key_J, .to = rendered.list},
             {.from = current, .key = Qt::Key_K, .to = current},
             {.from = current, .key = Qt::Key_L, .to = current},
         }) {
      route.from->forceActiveFocus(Qt::TabFocusReason);
      QTest::keyClick(rendered.window, route.key, Qt::ControlModifier);
      ASSERT_TRUE(QTest::qWaitFor([&] { return route.to->hasActiveFocus(); }))
          << route.key << " from " << route.from->objectName().toStdString();
      EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
      EXPECT_EQ(rendered.controller.cursorRow(), row);
      EXPECT_EQ(rendered.controller.sidebarNavigator()->section(), section);
      EXPECT_EQ(rendered.controller.sidebarNavigator()->index(), index);
      EXPECT_EQ(rendered.controller.vim()->selectedCount(), 0);
    }
  }
  EXPECT_TRUE(current->property("visualFocus").toBool());
  rendered.list->forceActiveFocus();
  QTest::keyClick(rendered.window, Qt::Key_J);
  EXPECT_EQ(rendered.controller.cursorRow(), row + 2);  // pending count survived
}

TEST(WindowPanelFocus, HeaderEntryTargetsCurrentSegmentAtRootAndNarrowWidths) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  for (const auto& path : {rendered.c, QStringLiteral("/")}) {
    rendered.controller.open(path);
    ASSERT_TRUE(settled(rendered.controller));
    for (const int width : {1000, 600}) {
      rendered.window->resize(width, 400);
      QTest::qWait(20);
      rendered.list->forceActiveFocus();
      QTest::keyClick(rendered.window, Qt::Key_K, Qt::ControlModifier);
      ASSERT_TRUE(QTest::qWaitFor([&] {
        return rendered.window->activeFocusItem()->objectName() ==
               QString("hnBreadcrumbSegment%1").arg(rendered.controller.breadcrumbLabels().size() - 1);
      }));
      EXPECT_TRUE(rendered.window->activeFocusItem()->property("visualFocus").toBool());
      EXPECT_EQ(rendered.controller.currentPath(), path);
    }
  }
}

TEST(WindowPanelFocus, ShortcutsPreserveFocusInModesAndOverlays) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  const auto checkKeys = [&] {
    auto* focused = rendered.window->activeFocusItem();
    const auto path = rendered.controller.currentPath();
    const auto row = rendered.controller.cursorRow();
    const auto mode = rendered.controller.vim()->currentMode();
    for (const auto key : {Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L}) {
      QTest::keyClick(rendered.window, key, Qt::ControlModifier);
      EXPECT_EQ(rendered.window->activeFocusItem(), focused)
          << "mode " << static_cast<int>(mode) << " key " << key << " original " << focused->objectName().toStdString()
          << " now " << rendered.window->activeFocusItem()->objectName().toStdString();
      EXPECT_EQ(rendered.controller.currentPath(), path);
      EXPECT_EQ(rendered.controller.cursorRow(), row);
      EXPECT_EQ(rendered.controller.vim()->currentMode(), mode);
    }
  };
  QTest::keyClick(rendered.window, Qt::Key_J);
  for (const auto key : {Qt::Key_V, Qt::Key_Slash, Qt::Key_I}) {
    rendered.list->forceActiveFocus();
    QTest::keyClick(rendered.window, key);
    ASSERT_NE(rendered.controller.vim()->currentMode(), VimModeController::Mode::Normal);
    QCoreApplication::processEvents();
    checkKeys();
    QTest::keyClick(rendered.window, Qt::Key_Escape);
  }
  QTest::keyClick(rendered.window, 'D', Qt::ShiftModifier);
  ASSERT_TRUE(rendered.controller.tasks()->hasPrompt());
  checkKeys();
  ASSERT_TRUE(rendered.controller.tasks()->hasPrompt());
  QTest::keyClick(rendered.window, Qt::Key_N);
  ASSERT_FALSE(rendered.controller.tasks()->hasPrompt());
  QTest::keyClick(rendered.window, Qt::Key_F);
  QTest::keyClick(rendered.window, Qt::Key_F);
  ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.controller.property("finderOpen").toBool(); }));
  checkKeys();
  QTest::keyClick(rendered.window, Qt::Key_Escape);
  ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.list->hasActiveFocus(); }));
  ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.controller.preview()->quickLookEligible(); }));
  QTest::keyClick(rendered.window, Qt::Key_Space);
  auto* popup = rendered.window->findChild<QObject*>("quickLookOverlay");
  ASSERT_NE(popup, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->property("opened").toBool(); }));
  checkKeys();
}

TEST(WindowPanelFocus, RecognizesSidebarChildrenAndExcludesPreviewAndFooter) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* places = rendered.window->findChild<QQuickItem*>("placesPanel");
  ASSERT_NE(places, nullptr);
  places->forceActiveFocus();
  QTest::keyClick(rendered.window, Qt::Key_L, Qt::ControlModifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.list->hasActiveFocus(); }));
  for (const auto* name : {"previewContainer", "footerBar"}) {
    auto* item = rendered.window->findChild<QQuickItem*>(name);
    ASSERT_NE(item, nullptr);
    item->forceActiveFocus();
    for (const auto key : {Qt::Key_H, Qt::Key_J, Qt::Key_K, Qt::Key_L}) {
      QTest::keyClick(rendered.window, key, Qt::ControlModifier);
      EXPECT_TRUE(item->hasActiveFocus());
    }
  }
}

TEST(WindowPanelFocus, ListingOwnsStartupFocus) {
  HistoryWindow rendered;
  ASSERT_EQ(rendered.engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(rendered.engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(window));
  auto* list = window->findChild<QQuickItem*>("directoryListView");
  ASSERT_NE(list, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return list->hasActiveFocus(); }));
}

TEST(WindowPanelFocus, PlainHLMovesWithinBreadcrumbsWithoutNavigating) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  QTest::keyClick(rendered.window, Qt::Key_K, Qt::ControlModifier);
  auto* current = rendered.window->activeFocusItem();
  ASSERT_TRUE(current->objectName().startsWith("hnBreadcrumbSegment"));
  QTest::keyClick(rendered.window, Qt::Key_H);
  EXPECT_NE(rendered.window->activeFocusItem(), current);
  EXPECT_TRUE(rendered.window->activeFocusItem()->property("visualFocus").toBool());
  EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
  QTest::keyClick(rendered.window, Qt::Key_L);
  EXPECT_EQ(rendered.window->activeFocusItem(), current);
  QTest::keyClick(rendered.window, Qt::Key_L);
  EXPECT_EQ(rendered.window->activeFocusItem(), current);
  EXPECT_EQ(rendered.controller.currentPath(), rendered.c);
}

TEST(WindowHistoryNavigation, IconSelectorAndUpRetainListingFocus) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  auto* icons = rendered.window->findChild<QQuickItem*>("iconViewButton");
  auto* listButton = rendered.window->findChild<QQuickItem*>("listViewButton");
  auto* upButton = rendered.window->findChild<QQuickItem*>("parentUpButton");
  ASSERT_NE(icons, nullptr);
  ASSERT_NE(listButton, nullptr);
  ASSERT_NE(upButton, nullptr);
  rendered.click(icons);
  EXPECT_TRUE(rendered.controller.iconView());
  EXPECT_TRUE(icons->property("checked").toBool());
  EXPECT_FALSE(listButton->property("checked").toBool());
  auto* grid = rendered.window->findChild<QQuickItem*>("directoryGridView");
  ASSERT_NE(grid, nullptr);
  EXPECT_EQ(rendered.window->findChild<QQuickItem*>("directoryListView"), nullptr);
  EXPECT_EQ(rendered.window->findChild<QQuickItem*>("lineNumberGutterStrip"), nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return grid->hasActiveFocus(); }));
  QTest::keyClick(rendered.window, Qt::Key_Right);
  EXPECT_EQ(rendered.controller.cursorRow(), 1);
  rendered.click(upButton);
  ASSERT_TRUE(settled(rendered.controller));
  EXPECT_EQ(rendered.controller.currentPath(), rendered.dir.path());
  EXPECT_EQ(rendered.controller.listing()
                ->index(rendered.controller.cursorRow(), 0)
                .data(DirectoryModel::NameRole)
                .toString(),
            "c");
  rendered.click(listButton);
  EXPECT_FALSE(rendered.controller.iconView());
  EXPECT_TRUE(listButton->property("checked").toBool());
}

TEST(WindowHistoryNavigation, MouseSelectsThenDoubleClickOpensFolderInBothViews) {
  for (bool icons : {false, true}) {
    HistoryWindow rendered;
    ASSERT_TRUE(rendered.start());
    rendered.controller.open(rendered.dir.path());
    ASSERT_TRUE(settled(rendered.controller));
    rendered.controller.setIconView(icons);
    QQuickItem* entry = nullptr;
    ASSERT_TRUE(QTest::qWaitFor([&] {
      auto* view = rendered.window->findChild<QQuickItem*>(icons ? "directoryGridView" : "directoryListView");
      if (!view) return false;
      for (auto* cell : view->property("contentItem").value<QQuickItem*>()->childItems()) {
        auto* candidate = icons ? cell->findChild<QQuickItem*>("directoryIconDelegate") : cell;
        if (!candidate) {
          continue;
        }
        if (candidate->objectName() == (icons ? "directoryIconDelegate" : "directoryEntryDelegate") &&
            candidate->property("name").toString() == "b") {
          entry = candidate;
          return candidate->isVisible();
        }
      }
      return false;
    }));
    rendered.click(entry);
    EXPECT_EQ(rendered.controller.currentPath(), rendered.dir.path());
    EXPECT_EQ(rendered.controller.listing()
                  ->index(rendered.controller.cursorRow(), 0)
                  .data(DirectoryModel::NameRole)
                  .toString(),
              "b");
    const auto center = entry->mapToScene(QPointF(entry->width() / 2, entry->height() / 2)).toPoint();
    QTest::mouseDClick(rendered.window, Qt::LeftButton, Qt::NoModifier, center);
    ASSERT_TRUE(QTest::qWaitFor([&] { return rendered.controller.currentPath() == rendered.b; }));
    ASSERT_TRUE(settled(rendered.controller));
  }
}

TEST(WindowHistoryNavigation, IconInlineCreateAndRenameReturnFocus) {
  HistoryWindow rendered;
  ASSERT_TRUE(rendered.start());
  rendered.controller.setIconView(true);
  auto* grid = rendered.window->findChild<QQuickItem*>("directoryGridView");
  ASSERT_NE(grid, nullptr);
  for (const auto* command : {"o", "a"}) {
    if (QString(command) == "a") {
      int createdRow = -1;
      ASSERT_TRUE(QTest::qWaitFor([&] {
        for (int row = 0; row < rendered.controller.listing()->rowCount(); ++row) {
          if (rendered.controller.listing()->index(row, 0).data(DirectoryModel::NameRole).toString() == "created.txt") {
            createdRow = row;
            return true;
          }
        }
        return false;
      }));
      rendered.controller.selectEntry(createdRow);
    }
    ASSERT_TRUE(rendered.controller.handleKey(command));
    QQuickItem* editor = nullptr;
    ASSERT_TRUE(QTest::qWaitFor([&] {
      for (auto* entry : grid->property("contentItem").value<QQuickItem*>()->childItems()) {
        auto* field = entry->findChild<QQuickItem*>("inlineNameEditor");
        if (field && field->isVisible() && field->hasActiveFocus()) {
          editor = field;
          return true;
        }
      }
      return false;
    }));
    const QString name = QString(command) == "o" ? "created.txt" : "renamed.txt";
    rendered.controller.updateInsertText(name);
    QTest::keyClick(rendered.window, Qt::Key_Return);
    ASSERT_TRUE(QTest::qWaitFor([&] { return QFile::exists(QDir(rendered.c).filePath(name)); }));
    ASSERT_TRUE(QTest::qWaitFor([&] { return grid->hasActiveFocus(); }));
  }
  EXPECT_FALSE(QFile::exists(QDir(rendered.c).filePath("created.txt")));
}

TEST(HistoryWindow, HeaderPreferencesReloadWithoutMovingFocusOrReservingCloseSpace) {
  QTemporaryDir config(files_test::fixturePattern("header-window"));
  const auto path = config.filePath("config.toml");
  const auto write = [&](bool visible) {
    QFile file(path);
    EXPECT_TRUE(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
    file.write(visible ? "[header]\nshow_close_button = true\n" : "[header]\nshow_close_button = false\n");
  };
  write(false);
  files_test::RecordingWarningSink warnings;
  FilesPreferences preferences(path, "labwc", warnings);
  HistoryWindow rendered(&preferences);
  ASSERT_TRUE(rendered.start());
  auto* close = rendered.window->findChild<QQuickItem*>("closeButton");
  auto* breadcrumb = rendered.window->findChild<QQuickItem*>("breadcrumbContainer");
  auto* selector = rendered.window->findChild<QQuickItem*>("viewSelector");
  ASSERT_NE(close, nullptr);
  ASSERT_NE(breadcrumb, nullptr);
  ASSERT_NE(selector, nullptr);
  EXPECT_FALSE(close->isVisible());
  // Fill the listing region so the right header bound, rather than the listing-width cap,
  // determines breadcrumb width. The normal preview layout is covered above.
  auto* preview = rendered.window->findChild<QQuickItem*>("previewContainer");
  ASSERT_NE(preview, nullptr);
  preview->setVisible(false);
  for (const auto width : {420, 1000}) {
    rendered.window->setWidth(width);
    for (const bool fullscreen : {false, true}) {
      rendered.window->setVisibility(fullscreen ? QWindow::FullScreen : QWindow::Windowed);
      QTest::qWait(20);
      write(false);
      ASSERT_TRUE(QTest::qWaitFor([&] { return !close->isVisible(); }));
      ASSERT_TRUE(QTest::qWaitFor([&] {
        return selector->mapToScene(QPointF(selector->width(), 0)).x() <= rendered.window->width() - 12 + 1;
      }));
      const auto hiddenWidth = breadcrumb->width();
      auto* focus = rendered.window->activeFocusItem();
      EXPECT_FALSE(close->isEnabled());
      EXPECT_LE(selector->mapToScene(QPointF(selector->width(), 0)).x(), rendered.window->width() - 12 + 1);
      write(true);
      ASSERT_TRUE(QTest::qWaitFor([&] { return close->isVisible(); }));
      ASSERT_TRUE(QTest::qWaitFor([&] {
        return selector->mapToScene(QPointF(selector->width(), 0)).x() <= close->mapToScene(QPointF()).x() - 8 + 1;
      }));
      EXPECT_GT(hiddenWidth, breadcrumb->width());
      EXPECT_EQ(rendered.window->activeFocusItem(), focus);
      EXPECT_EQ(close->property("focusPolicy").toInt(), Qt::NoFocus);
    }
  }
}
