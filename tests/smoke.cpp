#include "directory_controller.h"
#include "directory_fixtures.h"
#include "preview_fixtures.h"
#include "preview_service_test_access.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QWheelEvent>
#include <QtQml/QQmlExtensionPlugin>

#include <atomic>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>

Q_IMPORT_QML_PLUGIN(HolonightFilesPlugin)

class FileUrlReceiver : public QObject {
  Q_OBJECT
 public:
  QUrl received;
 public slots:
  void receive(const QUrl& url) { received = url; }
};

TEST(Files, PopulatedWindowKeyboardAndInlineError) {
  QTemporaryDir dir(files_test::fixturePattern("window"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("child"));
  ASSERT_FALSE(files_test::writeFile(dir, "child/nested.txt").isEmpty());
  ASSERT_TRUE(QFile::link(dir.filePath("missing-target"), dir.filePath("child/dangling-link")));
  files_test::populateEntries(dir, 30);
  ASSERT_FALSE(files_test::writeFile(dir, ".hidden").isEmpty());
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(window));
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  auto* list = window->findChild<QQuickItem*>("directoryListView");
  ASSERT_NE(list, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return list->property("count").toInt() == 31; }));
  auto expectCursor = [&](int row) {
    EXPECT_EQ(controller.cursorRow(), row);
    EXPECT_TRUE(QTest::qWaitFor([&] { return list->property("currentIndex").toInt() == row; }));
    auto* item = qvariant_cast<QQuickItem*>(list->property("currentItem"));
    EXPECT_NE(item, nullptr);
    if (item) {
      EXPECT_TRUE(item->isVisible());
      EXPECT_GT(item->height(), 0);
      EXPECT_TRUE(QTest::qWaitFor([&] {
        return item->mapRectToScene(item->boundingRect()).intersects(list->mapRectToScene(list->boundingRect()));
      }));
      EXPECT_TRUE(item->property("highlighted").toBool());
      EXPECT_EQ(item->property("title").toString(),
                controller.listing()->data(controller.listing()->index(row, 0), DirectoryModel::NameRole).toString());
    }
  };
  expectCursor(0);
  QTest::keyClick(window, Qt::Key_J);
  expectCursor(1);
  QTest::keyClick(window, Qt::Key_5);
  QTest::keyClick(window, Qt::Key_J);
  expectCursor(6);
  QTest::keyClick(window, Qt::Key_K);
  expectCursor(5);
  QTest::keyClick(window, Qt::Key_0);
  QTest::keyClick(window, Qt::Key_J);
  expectCursor(5);
  for (int i = 0; i < 40; ++i) {
    QTest::keyClick(window, Qt::Key_9);
  }
  QTest::keyClick(window, Qt::Key_J);
  expectCursor(30);
  for (int i = 0; i < 40; ++i) {
    QTest::keyClick(window, Qt::Key_9);
  }
  QTest::keyClick(window, Qt::Key_K);
  expectCursor(0);
  QTest::keyClick(window, Qt::Key_5);
  QKeyEvent unicodeDigit(QEvent::KeyPress, 0, Qt::NoModifier, QString::fromUtf8("٥"));
  QCoreApplication::sendEvent(window, &unicodeDigit);
  QTest::keyClick(window, Qt::Key_J);
  expectCursor(1);
  QTest::keyClick(window, 'G', Qt::ShiftModifier);
  expectCursor(30);
  QTest::keyClick(window, Qt::Key_G);
  QTest::keyClick(window, Qt::Key_G);
  expectCursor(0);
  // Both toggles consume counts and pending g chords.
  for (const auto toggle : {Qt::Key_Period, Qt::Key_S}) {
    QTest::keyClick(window, Qt::Key_5);
    QTest::keyClick(window, Qt::Key_G);
    QTest::keyClick(window, toggle);
    QTest::keyClick(window, Qt::Key_J);
    expectCursor(1);
    QTest::keyClick(window, Qt::Key_G);
    expectCursor(1);
    QTest::keyClick(window, Qt::Key_G);
    expectCursor(0);
  }
  EXPECT_TRUE(controller.listing()->hiddenVisible());
  EXPECT_TRUE(controller.listing()->sortDescending());
  EXPECT_EQ(list->property("count").toInt(), 32);
  QTest::keyClick(window, Qt::Key_S);
  QTest::keyClick(window, Qt::Key_Period);
  EXPECT_EQ(list->property("count").toInt(), 31);
  for (const auto enter : {Qt::Key_L, Qt::Key_Return, Qt::Key_Enter}) {
    QTest::keyClick(window, enter);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
    EXPECT_EQ(controller.currentPath(), dir.filePath("child"));
    EXPECT_EQ(list->property("count").toInt(), 2);
    expectCursor(0);
    auto* placeholder = qvariant_cast<QQuickItem*>(list->property("currentItem"));
    ASSERT_NE(placeholder, nullptr);
    EXPECT_TRUE(placeholder->property("statFailed").toBool());
    EXPECT_EQ(placeholder->property("statError").toString(), "Broken symbolic link");
    EXPECT_TRUE(
        QTest::qWaitFor([&] { return qvariant_cast<QQuickItem*>(placeholder->property("trailingItem")) != nullptr; }));
    QTest::keyClick(window, Qt::Key_H);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
    EXPECT_EQ(controller.currentPath(), dir.path());
  }
  FileUrlReceiver receiver;
  QDesktopServices::setUrlHandler("file", &receiver, "receive");
  const auto cleanup = qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
  QTest::keyClick(window, Qt::Key_J);
  const auto expectedUrl = QUrl::fromLocalFile(dir.filePath("entry-00000.txt"));
  for (const auto enter : {Qt::Key_L, Qt::Key_Return}) {
    receiver.received = QUrl{};
    QTest::keyClick(window, enter);
    EXPECT_EQ(receiver.received, expectedUrl);
  }
  const auto capture = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
  if (!capture.isEmpty()) {
    QTest::qWait(100);
    EXPECT_TRUE(window->grabWindow().save(capture + "-populated.png"));
  }
  controller.open(dir.filePath("missing"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  EXPECT_FALSE(controller.directoryError().isEmpty());
  EXPECT_EQ(controller.currentPath(), dir.filePath("missing"));
  auto* error = window->findChild<QQuickItem*>("directoryErrorState");
  ASSERT_NE(error, nullptr);
  EXPECT_TRUE(error->isVisible());
  EXPECT_FALSE(list->isVisible());
  EXPECT_TRUE(window->findChild<QObject*>("normalStatusLabel")
                  ->property("rawText")
                  .toString()
                  .contains(dir.filePath("missing")));
  if (!capture.isEmpty()) {
    QTest::qWait(100);
    EXPECT_TRUE(window->grabWindow().save(capture + "-error.png"));
  }
}

TEST(Files, WindowColumnAlignmentAndNarrowNames) {
  QTemporaryDir dir(files_test::fixturePattern("columns"));
  ASSERT_FALSE(files_test::writeFile(dir, "example.txt").isEmpty());
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  controller.open(dir.path());
  auto* list = window->findChild<QQuickItem*>("directoryListView");
  ASSERT_NE(list, nullptr);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return !controller.scanning() && list->property("currentItem").value<QQuickItem*>() != nullptr; }));
  auto* row = list->property("currentItem").value<QQuickItem*>();
  auto* icon = row->findChild<QQuickItem*>("iconColumnField");
  auto* nameHeader = window->findChild<QQuickItem*>("nameColumnHeader");
  auto* name = row->findChild<QQuickItem*>("nameColumnField");
  auto* size = row->findChild<QQuickItem*>("sizeColumnField");
  auto* modified = row->findChild<QQuickItem*>("modifiedColumnField");
  auto* sizeHeader = window->findChild<QQuickItem*>("sizeColumnHeader");
  auto* modifiedHeader = window->findChild<QQuickItem*>("modifiedColumnHeader");
  auto* breadcrumb = window->findChild<QQuickItem*>("breadcrumbLabel");
  ASSERT_NE(icon, nullptr);
  ASSERT_NE(nameHeader, nullptr);
  ASSERT_NE(name, nullptr);
  ASSERT_NE(size, nullptr);
  ASSERT_NE(modified, nullptr);
  ASSERT_NE(sizeHeader, nullptr);
  ASSERT_NE(modifiedHeader, nullptr);
  ASSERT_NE(breadcrumb, nullptr);
  for (const int width : {1000, 850, 700, 1000}) {
    window->resize(width, 400);
    ASSERT_TRUE(QTest::qWaitFor([&] {
      return name->width() >= 120 && size->isVisible() == (width != 700) && modified->isVisible() == (width == 1000);
    }));
    // main-view-icons REQ-NF-002: the row's content now starts with the icon cell, so the breadcrumb
    // (app-window-layout REQ-F-004) aligns with that leading edge rather than the Name text.
    EXPECT_NEAR(breadcrumb->mapToScene(QPointF()).x(), icon->mapToScene(QPointF()).x(), 1);
    EXPECT_EQ(sizeHeader->isVisible(), size->isVisible());
    EXPECT_EQ(modifiedHeader->isVisible(), modified->isVisible());
    if (size->isVisible()) {
      EXPECT_NEAR(sizeHeader->mapToScene(QPointF()).x(), size->mapToScene(QPointF()).x(), 1);
      EXPECT_NEAR(sizeHeader->width(), size->width(), 1);
    }
    if (modified->isVisible()) {
      EXPECT_NEAR(modifiedHeader->mapToScene(QPointF()).x(), modified->mapToScene(QPointF()).x(), 1);
      EXPECT_NEAR(modifiedHeader->width(), modified->width(), 1);
    }
  }
  // main-view-icons REQ-F-008/009/026: fixed 20 px icon cell, header Name label inset to match.
  for (const int width : {420, 700, 1000, 1600}) {
    window->resize(width, 400);
    ASSERT_TRUE(
        QTest::qWaitFor([&] { return window->width() == width && qFuzzyCompare(row->width(), list->width()); }));
    QTest::qWait(20);  // let both RowLayouts finish polishing at the new width
    EXPECT_EQ(icon->width(), 20) << width;
    EXPECT_NEAR(nameHeader->mapToScene(QPointF()).x(), name->mapToScene(QPointF()).x(), 1) << width;
    EXPECT_LT(icon->mapToScene(QPointF()).x() + icon->width(), name->mapToScene(QPointF()).x()) << width;
  }
}

namespace {
// A one-icon theme providing only "folder", so a test can observe both the theme path and the
// bundled-glyph path in one listing regardless of the icon themes installed on the machine.
bool writeFolderOnlyTheme(const QTemporaryDir& root) {
  const QDir themeDir(root.filePath("folder-only-theme"));
  QFile index(themeDir.filePath("index.theme"));
  QImage image(32, 32, QImage::Format_ARGB32);
  image.fill(Qt::red);
  return themeDir.mkpath("32x32/places") && index.open(QIODevice::WriteOnly) &&
         index.write(
             "[Icon Theme]\nName=folder-only-theme\nDirectories=32x32/places\n\n"
             "[32x32/places]\nSize=32\nContext=Places\nType=Fixed\n") > 0 &&
         index.flush() && image.save(themeDir.filePath("32x32/places/folder.png"));
}

struct RowIcons {
  QQuickItem* theme = nullptr;
  QQuickItem* fallback = nullptr;
};
RowIcons rowIcons(QQuickItem* list, int row) {
  // Delegates are parented to the ListView's contentItem only visually, not in the QObject tree.
  for (auto* delegate : list->property("contentItem").value<QQuickItem*>()->childItems()) {
    if (delegate->objectName() == "directoryEntryDelegate" && delegate->property("index").toInt() == row) {
      return {.theme = delegate->findChild<QQuickItem*>("themeFileIcon"),
              .fallback = delegate->findChild<QQuickItem*>("fallbackFileIcon")};
    }
  }
  return {};
}
bool imageReady(QQuickItem* icon) {
  const auto images = icon->findChildren<QQuickItem*>();
  return !images.isEmpty() && images.first()->property("status").toInt() == 1;  // Image.Ready
}
}  // namespace

TEST(Files, IconColumnUsesThemeIconsAndFallsBackToBundledGlyphs) {
  QTemporaryDir themeRoot(files_test::fixturePattern("icon-theme"));
  QTemporaryDir dir(files_test::fixturePattern("icon-window"));
  ASSERT_TRUE(themeRoot.isValid());
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(writeFolderOnlyTheme(themeRoot));
  ASSERT_TRUE(QDir(dir.path()).mkdir("a-folder"));
  ASSERT_FALSE(files_test::writeFile(dir, "b-notes.txt").isEmpty());
  ASSERT_TRUE(QFile::link(dir.filePath("missing"), dir.filePath("c-dangling")));
  // Test-only theme override; production never touches the theme (REQ-F-018).
  const auto previousPaths = QIcon::themeSearchPaths();
  const auto previousFallbackPaths = QIcon::fallbackSearchPaths();
  const auto previousTheme = QIcon::themeName();
  const auto previousFallbackTheme = QIcon::fallbackThemeName();
  const auto restoreTheme = qScopeGuard([&] {
    QIcon::setThemeSearchPaths(previousPaths);
    QIcon::setFallbackSearchPaths(previousFallbackPaths);
    QIcon::setThemeName(previousTheme);
    QIcon::setFallbackThemeName(previousFallbackTheme);
  });
  QIcon::setThemeSearchPaths({themeRoot.path()});
  QIcon::setFallbackSearchPaths({});
  QIcon::setThemeName(QStringLiteral("folder-only-theme"));
  QIcon::setFallbackThemeName(QStringLiteral("folder-only-theme"));

  {
    DirectoryController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    engine.loadFromModule("HolonightFiles", "Main");
    ASSERT_EQ(engine.rootObjects().size(), 1);
    // Registered by the HolonightFiles plugin itself, not by this test (DESIGN.md §5.4).
    EXPECT_NE(engine.imageProvider(QStringLiteral("icon")), nullptr);
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    ASSERT_NE(window, nullptr);
    window->resize(1000, 500);
    window->releaseResources();  // earlier tests' cached image://icon/ results used another theme
    controller.open(dir.path());
    auto* list = window->findChild<QQuickItem*>("directoryListView");
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && list->property("count").toInt() == 3; }));

    // Sorted folders first: 0 a-folder, 1 b-notes.txt, 2 c-dangling.
    ASSERT_TRUE(QTest::qWaitFor([&] {
      const auto folder = rowIcons(list, 0);
      return folder.theme != nullptr && folder.theme->isVisible() && imageReady(folder.theme);
    }));
    EXPECT_FALSE(rowIcons(list, 0).fallback->isVisible());
    for (const int row : {1, 2}) {
      ASSERT_TRUE(QTest::qWaitFor([&] {
        const auto icons = rowIcons(list, row);
        return icons.fallback != nullptr && icons.fallback->isVisible() && imageReady(icons.fallback);
      })) << row;
      const auto icons = rowIcons(list, row);
      EXPECT_FALSE(icons.theme->isVisible()) << row;
      EXPECT_TRUE(icons.fallback->property("source").toString().endsWith("generic-file-fallback.svg")) << row;
      EXPECT_TRUE(icons.fallback->property("tinted").toBool()) << row;
    }
    EXPECT_EQ(rowIcons(list, 0).theme->property("source").toString(), "image://icon/folder/inode-directory");

    // Preview pane: the folder row's theme icon at up to 128 px; a file row falls back to its glyph.
    auto* previewTheme = window->findChild<QQuickItem*>("previewThemeIcon");
    auto* previewFallback = window->findChild<QQuickItem*>("previewFallbackIcon");
    ASSERT_NE(previewTheme, nullptr);
    ASSERT_NE(previewFallback, nullptr);
    ASSERT_TRUE(QTest::qWaitFor([&] { return previewTheme->isVisible() && imageReady(previewTheme); }));
    EXPECT_FALSE(previewFallback->isVisible());
    EXPECT_LE(previewTheme->width(), 128);
    EXPECT_GT(previewTheme->width(), 0);
    window->requestActivate();
    ASSERT_TRUE(QTest::qWaitForWindowActive(window));
    QTest::keyClick(window, Qt::Key_J);
    ASSERT_TRUE(QTest::qWaitFor([&] { return previewFallback->isVisible() && imageReady(previewFallback); }));
    EXPECT_FALSE(previewTheme->isVisible());
    EXPECT_TRUE(previewFallback->property("source").toString().endsWith("generic-file-fallback.svg"));
    QTest::keyClick(window, Qt::Key_K);
    ASSERT_TRUE(QTest::qWaitFor([&] { return previewTheme->isVisible(); }));
  }
  QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);

  // With no theme reachable at all, the folder row switches to the bundled folder glyph.
  QIcon::setThemeSearchPaths({dir.filePath("a-folder")});
  QIcon::setThemeName(QStringLiteral("nonexistent-test-theme"));
  QIcon::setFallbackThemeName(QStringLiteral("nonexistent-test-theme"));
  DirectoryController bareController;
  QQmlApplicationEngine bareEngine;
  bareEngine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&bareController)}});
  bareEngine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(bareEngine.rootObjects().size(), 1);
  EXPECT_NE(bareEngine.imageProvider(QStringLiteral("icon")), nullptr);
  auto* bareWindow = qobject_cast<QQuickWindow*>(bareEngine.rootObjects().first());
  ASSERT_NE(bareWindow, nullptr);
  // Qt Quick caches decoded images per URL process-wide; drop the now-unreferenced theme hits from
  // the first engine so the same image://icon/ URLs are requested again under the empty theme.
  bareWindow->releaseResources();
  bareController.open(dir.path());
  auto* bareList = bareWindow->findChild<QQuickItem*>("directoryListView");
  ASSERT_NE(bareList, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    const auto folder = rowIcons(bareList, 0);
    return folder.fallback != nullptr && folder.fallback->isVisible() && imageReady(folder.fallback);
  }));
  EXPECT_TRUE(rowIcons(bareList, 0).fallback->property("source").toString().endsWith("folder-fallback.svg"));
  // A missing packaged SVG must still leave a visible marker in the fixed icon cell (REQ-F-022).
  auto* bareRowPlaceholder =
      rowIcons(bareList, 0).fallback->parentItem()->findChild<QQuickItem*>("iconFailurePlaceholder");
  ASSERT_NE(bareRowPlaceholder, nullptr);
  rowIcons(bareList, 0).fallback->setProperty("source", QUrl("qrc:/missing-folder-icon.svg"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return bareRowPlaceholder->isVisible(); }));
  auto* barePreviewFallback = bareWindow->findChild<QQuickItem*>("previewFallbackIcon");
  ASSERT_NE(barePreviewFallback, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return barePreviewFallback->isVisible() && imageReady(barePreviewFallback); }));
  EXPECT_TRUE(barePreviewFallback->property("source").toString().endsWith("folder-fallback.svg"));
  auto* barePreviewPlaceholder = bareWindow->findChild<QQuickItem*>("previewIconFailurePlaceholder");
  ASSERT_NE(barePreviewPlaceholder, nullptr);
  barePreviewFallback->setProperty("source", QUrl("qrc:/missing-preview-icon.svg"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return barePreviewPlaceholder->isVisible(); }));
}

TEST(Files, WindowAndKeyboard) {
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  EXPECT_TRUE(window->isVisible());
  EXPECT_FALSE(window->flags().testFlag(Qt::FramelessWindowHint));
  window->resize(420, 280);
  QCoreApplication::processEvents();
  const QString capture = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
  if (!capture.isEmpty()) {
    QTest::qWait(200);
    EXPECT_TRUE(window->grabWindow().save(capture + QStringLiteral("-small.png")));
    window->resize(1000, 700);
    QTest::qWait(200);
    EXPECT_TRUE(window->grabWindow().save(capture + QStringLiteral("-large.png")));
  }
  window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(window));
  // Qt visibility changes synchronously; allow native configure events to arrive
  // before another transition or a restoration assertion.
  const auto settle = [] { QTest::qWait(250); };
  settle();
  const auto initial_geometry = window->geometry();
  const auto initial_visibility = window->visibility();
  for (const auto exit_key : {Qt::Key_F, Qt::Key_Escape}) {
    QTest::keyClick(window, Qt::Key_F);
    settle();
    ASSERT_EQ(window->visibility(), QWindow::FullScreen);
    QTest::keyClick(window, exit_key);
    settle();
    EXPECT_EQ(window->visibility(), initial_visibility);
    EXPECT_EQ(window->geometry(), initial_geometry);
  }
  QTest::keyClick(window, Qt::Key_Escape);
  settle();
  EXPECT_EQ(window->visibility(), initial_visibility);
  EXPECT_EQ(window->geometry(), initial_geometry);
  window->showMaximized();
  settle();
  ASSERT_EQ(window->visibility(), QWindow::Maximized);
  for (const auto exit_key : {Qt::Key_F, Qt::Key_Escape}) {
    QTest::keyClick(window, Qt::Key_F);
    settle();
    ASSERT_EQ(window->visibility(), QWindow::FullScreen);
    QTest::keyClick(window, exit_key);
    settle();
    EXPECT_EQ(window->visibility(), QWindow::Maximized);
  }
  QTest::keyClick(window, Qt::Key_Q);
  EXPECT_FALSE(window->isVisible());
}

TEST(Files, EmbeddedStyleSelection) {
  QQmlEngine engine;
  QQmlComponent component(&engine);
  component.setData("import QtQuick.Controls\nButton {}", QUrl());
  const std::unique_ptr<QObject> button(component.create());
  ASSERT_NE(button, nullptr) << component.errorString().toStdString();
  EXPECT_EQ(QQuickStyle::name(), QStringLiteral("Holonight"));
  EXPECT_TRUE(button->property("foregroundColor").isValid());
}

int main(int argc, char* argv[]) {
  qunsetenv("QT_QUICK_CONTROLS_STYLE");
  qunsetenv("QT_QUICK_CONTROLS_FALLBACK_STYLE");
  qunsetenv("QT_QUICK_CONTROLS_CONF");
  qputenv("XDG_CACHE_HOME", QByteArray(FILES_FIXTURE_DIR) + "/cache");
  const QGuiApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include "smoke.moc"

TEST(Files, QuickLookConsumesSpaceBeforeDelegateActivationAndRestoresFocus) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-keys"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeFile(dir, "a.txt");
  files_test::writeFile(dir, "b.txt");
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(window));
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && !controller.preview()->busy(); }));
  auto* list = window->findChild<QQuickItem*>("directoryListView");
  auto* popup = window->findChild<QObject*>("quickLookOverlay");
  ASSERT_NE(list, nullptr);
  ASSERT_NE(popup, nullptr);
  FileUrlReceiver receiver;
  QDesktopServices::setUrlHandler("file", &receiver, "receive");
  const auto cleanup = qScopeGuard([] { QDesktopServices::unsetUrlHandler("file"); });
  QTest::keyClick(window, Qt::Key_J);
  QTest::keyClick(window, Qt::Key_K);
  QTest::keyClick(window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->property("opened").toBool(); }));
  EXPECT_TRUE(receiver.received.isEmpty());
  QTest::keyClick(window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }));
  EXPECT_TRUE(list->hasActiveFocus());
  auto* delegate = qvariant_cast<QQuickItem*>(list->property("currentItem"));
  ASSERT_NE(delegate, nullptr);
  delegate->forceActiveFocus();
  QTest::keyPress(window, Qt::Key_Space);
  QKeyEvent repeat(QEvent::KeyPress, Qt::Key_Space, Qt::NoModifier, " ", true);
  QCoreApplication::sendEvent(window, &repeat);
  QTest::keyRelease(window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return popup->property("opened").toBool(); }));
  EXPECT_TRUE(receiver.received.isEmpty());
  const auto capture = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
  if (!capture.isEmpty()) {
    QTest::qWait(100);
    EXPECT_TRUE(window->grabWindow().save(capture + "-quicklook.png"));
  }
  auto* text = window->findChild<QQuickItem*>("quickLookText");
  ASSERT_NE(text, nullptr);
  text->forceActiveFocus();
  QTest::keyClick(window, Qt::Key_J);
  EXPECT_EQ(controller.cursorRow(), 1);
  QTest::keyClick(window, Qt::Key_K);
  EXPECT_EQ(controller.cursorRow(), 0);
  window->showFullScreen();
  QTest::qWait(100);
  QTest::keyClick(window, Qt::Key_Escape);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !popup->property("visible").toBool(); }));
  EXPECT_EQ(window->visibility(), QWindow::FullScreen);
  EXPECT_TRUE(list->hasActiveFocus());
  QTest::keyClick(window, Qt::Key_Escape);
  EXPECT_NE(window->visibility(), QWindow::FullScreen);
  for (const auto key : {Qt::Key_Return, Qt::Key_Enter, Qt::Key_L}) {
    receiver.received = QUrl{};
    QTest::keyClick(window, key);
    EXPECT_EQ(receiver.received, QUrl::fromLocalFile(dir.filePath("a.txt")));
  }
  EXPECT_TRUE(receiver.received.isValid());
}

TEST(Files, NativeInspectionAcceptance) {
  if (!qEnvironmentVariableIsSet("FILES_NATIVE_ACCEPTANCE")) {
    GTEST_SKIP() << "Opt-in native acceptance run";
  }
  QTemporaryDir dir(files_test::fixturePattern("native-inspection"));
  ASSERT_TRUE(dir.isValid());
  constexpr int count = 101;
  for (int i = 0; i < count; ++i) {
    QImage image(1600, 1200, QImage::Format_RGB32);
    image.fill(QColor(i, 80, 150));
    const auto path = dir.filePath(QStringLiteral("image-%1.bmp").arg(i, 3, 10, QLatin1Char('0')));
    ASSERT_TRUE(image.save(path));
    ASSERT_GT(QFileInfo(path).size(), 5'000'000);
  }
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  QElapsedTimer elapsed;
  elapsed.start();
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(window));
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->hasImage(); }, 3000));
  const auto initialMs = elapsed.elapsed();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && !controller.preview()->busy(); }));
  QTest::keyClick(window, Qt::Key_J);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.preview()->busy(); }));
  elapsed.restart();
  QTest::keyClick(window, Qt::Key_K);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->hasImage(); }));
  const auto cachedMs = elapsed.elapsed();
  QTest::keyClick(window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.quickLookOpen(); }));
  QTest::qWait(250);
  elapsed.restart();
  QTest::keyClick(window, Qt::Key_J);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->hasImage(); }));
  const auto quickLookMs = elapsed.elapsed();
  QTest::keyClick(window, Qt::Key_Space);
  QTest::keyClick(window, Qt::Key_K);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.preview()->busy(); }));
  int stale = 0;
  QObject::connect(controller.preview(), &PreviewService::changed, &controller, [&] {
    if (controller.preview()->hasImage() &&
        controller.preview()->image().pixelColor(0, 0).red() != controller.cursorRow()) {
      ++stale;
    }
  });
  std::atomic_int frames = 0;
  QObject::connect(window, &QQuickWindow::frameSwapped, window, [&] { ++frames; }, Qt::DirectConnection);
  QTimer render;
  QObject::connect(&render, &QTimer::timeout, window, &QQuickWindow::update);
  render.start(16);
  elapsed.restart();
  for (int i = 1; i < count; ++i) {
    QTest::keyClick(window, Qt::Key_J);
    QTest::qWait(90);
  }
  const auto stressMs = elapsed.elapsed();
  render.stop();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.preview()->busy(); }));
  QFile status("/proc/self/status");
  ASSERT_TRUE(status.open(QIODevice::ReadOnly));
  qint64 peakKiB = 0;
  const auto statusLines = status.readAll().split('\n');
  for (const auto& line : statusLines) {
    if (line.startsWith("VmHWM:")) {
      peakKiB = line.simplified().split(' ').at(1).toLongLong();
    }
  }
  const double fps = frames.load() * 1000.0 / static_cast<double>(stressMs);
  QFile evidence(qEnvironmentVariable("FILES_NATIVE_ACCEPTANCE"));
  ASSERT_TRUE(evidence.open(QIODevice::WriteOnly));
  QTextStream out(&evidence);
  out << "platform=" << QGuiApplication::platformName() << "\ninitial_ms=" << initialMs << "\ncached_ms=" << cachedMs
      << "\nquicklook_ms=" << quickLookMs << "\nimages=" << count
      << "\nmovements_per_second=" << 100000.0 / static_cast<double>(stressMs) << "\nstale_results=" << stale
      << "\npeak_kib=" << peakKiB << "\nfps=" << fps << '\n';
  EXPECT_LT(initialMs, 500);
  EXPECT_LT(cachedMs, 100);
  EXPECT_LT(quickLookMs, 200);
  EXPECT_EQ(stale, 0);
  EXPECT_GT(peakKiB, 0);
  EXPECT_LT(peakKiB, 500 * 1024);
  EXPECT_GE(fps, 30);
  EXPECT_LT(stressMs, 10000);
}

TEST(Files, InspectionImageSplitterAndPixelSizing) {
  QTemporaryDir dir(files_test::fixturePattern("image-splitter"));
  QImage source(800, 600, QImage::Format_RGB32);
  for (int row = 0; row < source.height(); ++row) {
    for (int column = 0; column < source.width(); ++column) {
      source.setPixelColor(column, row, (((column / 8) + (row / 8)) % 2) == 0 ? Qt::darkCyan : Qt::white);
    }
  }
  ASSERT_TRUE(source.save(dir.filePath("image.bmp")));
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(window));
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->hasImage() && !controller.preview()->busy(); }));
  auto* pane = window->findChild<QQuickItem*>("previewPane");
  auto* listing = window->findChild<QQuickItem*>("directoryListing");
  ASSERT_NE(pane, nullptr);
  ASSERT_NE(listing, nullptr);
  const auto originalWidth = pane->width();
  const auto left = listing->mapToScene(QPointF(listing->width(), listing->height() / 2));
  const auto right = pane->mapToScene(QPointF(0, pane->height() / 2));
  const auto handle = ((left + right) / 2).toPoint();
  QTest::mousePress(window, Qt::LeftButton, Qt::NoModifier, handle);
  QTest::mouseMove(window, handle - QPoint(100, 0), 100);
  QTest::mouseRelease(window, Qt::LeftButton, Qt::NoModifier, handle - QPoint(100, 0));
  ASSERT_TRUE(QTest::qWaitFor([&] { return pane->width() > originalWidth + 50; }));
  QTest::qWait(300);
  const auto imageSize = controller.preview()->image().size();
  EXPECT_LE(qAbs((imageSize.width() * 3) - (imageSize.height() * 4)), 4);
  EXPECT_GE(imageSize.height(), qRound(230 * window->devicePixelRatio()));
  const auto capture = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
  if (!capture.isEmpty()) {
    EXPECT_TRUE(window->grabWindow().save(capture + "-image-pane.png"));
  }
  QTest::keyClick(window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.quickLookOpen(); }));
  auto* area = window->findChild<QQuickItem*>("quickLookImageArea");
  ASSERT_NE(area, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    const auto needed = source.size().scaled(
        (area->size() * window->devicePixelRatio()).toSize().boundedTo(source.size()), Qt::KeepAspectRatio);
    return needed.isValid() && controller.preview()->image().width() >= needed.width() &&
           controller.preview()->image().height() >= needed.height();
  }));
  if (!capture.isEmpty()) {
    EXPECT_TRUE(window->grabWindow().save(capture + "-image-popup.png"));
  }
  QTest::keyClick(window, Qt::Key_Escape);
  EXPECT_NEAR(pane->width(), originalWidth + 100, 1);
}

TEST(Files, ModalEditingWindowKeyboardAndHighlighting) {
  QTemporaryDir dir(files_test::fixturePattern("modal-window"));
  QTemporaryDir destination(files_test::fixturePattern("modal-window-destination"));
  files_test::writeFile(dir, "<b>nN&.txt");
  files_test::writeFile(dir, "beta.txt");
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(window));
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  auto* list = window->findChild<QQuickItem*>("directoryListView");
  ASSERT_NE(list, nullptr);
  auto capture = [&](const QString& state) {
    const auto prefix = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
    if (!prefix.isEmpty()) {
      QTest::qWait(60);
      EXPECT_TRUE(window->grabWindow().save(prefix + "-modal-" + state + ".png"));
    }
  };
  for (const char key : {'i', 'a', 'o'}) {
    for (const auto modifier : {Qt::NoModifier, Qt::ShiftModifier}) {
      SCOPED_TRACE(QString("key=%1 shift=%2 focus=%3")
                       .arg(QChar(key))
                       .arg(modifier == Qt::ShiftModifier)
                       .arg(window->activeFocusItem() ? window->activeFocusItem()->objectName() : QString())
                       .toStdString());
      QTest::keyClick(window, modifier == Qt::ShiftModifier ? static_cast<char>(key - 'a' + 'A') : key, modifier);
      ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
      auto* editor = window->activeFocusItem();
      ASSERT_NE(editor, nullptr);
      EXPECT_EQ(editor->objectName(), "inlineNameEditor");
      const int expectedCursor = key == 'a' ? static_cast<int>(controller.vim()->insertText().size()) : 0;
      EXPECT_TRUE(QTest::qWaitFor([&] { return editor->property("cursorPosition").toInt() == expectedCursor; }));
      QTest::keyClick(window, Qt::Key_F);
      QTest::keyClick(window, Qt::Key_Q);
      EXPECT_NE(window->visibility(), QWindow::FullScreen);
      EXPECT_TRUE(window->isVisible());
      capture("insert");
      QTest::keyClick(window, Qt::Key_Escape);
      EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
      EXPECT_TRUE(list->hasActiveFocus());
      ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
    }
  }
  QTest::keyClick(window, Qt::Key_O);
  auto* editor = window->activeFocusItem();
  ASSERT_NE(editor, nullptr);
  QElapsedTimer timer;
  qint64 maxNs = 0;
  for (int edit = 0; edit < 20; ++edit) {
    timer.start();
    editor->setProperty("text", edit % 2 == 0 ? "../invalid" : "created.txt");
    ASSERT_TRUE(QTest::qWaitFor([&] { return editor->property("hasError").toBool() == (edit % 2 == 0); }));
    maxNs = qMax(maxNs, timer.nsecsElapsed());
    EXPECT_LT(timer.elapsed(), 200);
  }
  std::cout << "Modal validation feedback: 20 edits, maximum ms " << static_cast<double>(maxNs) / 1e6 << '\n';
  QTest::keyClick(window, Qt::Key_Return);
  EXPECT_TRUE(QFile::exists(dir.filePath("created.txt")));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  QTest::keyClick(window, Qt::Key_Slash);
  ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Search);
  EXPECT_EQ(window->activeFocusItem()->objectName(), "searchField");
  QTest::keyClick(window, Qt::Key_N);
  EXPECT_EQ(window->activeFocusItem()->objectName(), "searchField");
  QTest::keyClick(window, 'N', Qt::ShiftModifier);
  EXPECT_EQ(controller.vim()->searchQuery(), "nN");
  EXPECT_EQ(controller.vim()->searchMatchPositions(), (QList<int>{3, 4}));
  capture("search");
  bool literal = false;
  bool highlighted = false;
  QList<QQuickItem*> items{window->contentItem()};
  for (int item = 0; item < items.size(); ++item) {
    items.append(items[item]->childItems());
  }
  for (auto* label : items) {
    if (label->objectName() != "filenameRun") {
      continue;
    }
    const auto text = label->property("rawText").toString();
    literal |= text == "<b>";
    highlighted |= text == "nN" && label->property("font").value<QFont>().bold();
    EXPECT_EQ(label->property("text").toString(), text);
  }
  EXPECT_TRUE(literal);
  EXPECT_TRUE(highlighted);
  QTest::keyClick(window, Qt::Key_F);
  QTest::keyClick(window, Qt::Key_Q);
  EXPECT_NE(window->visibility(), QWindow::FullScreen);
  QTest::keyClick(window, Qt::Key_Escape);
  EXPECT_TRUE(list->hasActiveFocus());
  QTest::keyClick(window, Qt::Key_Slash);
  QTest::keyClick(window, Qt::Key_N);
  QTest::keyClick(window, Qt::Key_Return);
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  QTest::keyClick(window, Qt::Key_V);
  capture("visual");
  QTest::keyClick(window, Qt::Key_F);
  QTest::keyClick(window, Qt::Key_Q);
  EXPECT_NE(window->visibility(), QWindow::FullScreen);
  QTest::keyClick(window, Qt::Key_Escape);
  for (const auto key : {Qt::Key_I, Qt::Key_O, Qt::Key_V, Qt::Key_Slash}) {
    QTest::keyClick(window, key);
    controller.open(destination.path());
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
    EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
    EXPECT_TRUE(list->hasActiveFocus());
    controller.open(dir.path());
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  }
  window->showFullScreen();
  QTest::qWait(60);
  QTest::keyClick(window, Qt::Key_Escape);
  EXPECT_NE(window->visibility(), QWindow::FullScreen);
  capture("normal");
}

TEST(Files, ModeStatusBarShowsProgressAndConflictPromptAndCtrlCCancels) {
  QTemporaryDir src(files_test::fixturePattern("fileops-window-src"));
  QTemporaryDir dst(files_test::fixturePattern("fileops-window-dst"));
  ASSERT_TRUE(src.isValid() && dst.isValid());
  files_test::writeFile(src, "a.txt");
  files_test::writeFile(src, "b.txt", "NEW");
  files_test::writeFile(src, "c.txt");
  files_test::writeFile(dst, "b.txt", "OLD");
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(window));
  controller.open(src.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));

  // v + j + j selects all three entries, "y" copies the whole selection to the register.
  QTest::keyClick(window, Qt::Key_V);
  QTest::keyClick(window, Qt::Key_J);
  QTest::keyClick(window, Qt::Key_J);
  QTest::keyClick(window, Qt::Key_Y);
  controller.open(dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));

  QTest::keyClick(window, Qt::Key_P);
  auto* conflictLabel = window->findChild<QObject*>("conflictPromptLabel");
  ASSERT_NE(conflictLabel, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return conflictLabel->property("visible").toBool(); }));
  EXPECT_TRUE(conflictLabel->property("rawText").toString().contains("b.txt"));
  EXPECT_TRUE(controller.tasks()->busy());

  const auto capture = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
  if (!capture.isEmpty()) {
    QTest::qWait(60);
    EXPECT_TRUE(window->grabWindow().save(capture + "-fileops-conflict.png"));
  }

  // Ctrl+C fires the window-level Shortcut regardless of the open prompt, cancelling the whole
  // task rather than resolving it (REQ-F-030/REQ-C-009).
  QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
  EXPECT_FALSE(controller.tasks()->hasPrompt());
  // "a.txt" sorts and processes before the "b.txt" collision; "c.txt" never gets reached.
  EXPECT_TRUE(QFile::exists(dst.filePath("a.txt")));
  EXPECT_FALSE(QFile::exists(dst.filePath("c.txt")));
  QFile bFile(dst.filePath("b.txt"));
  ASSERT_TRUE(bFile.open(QIODevice::ReadOnly));
  EXPECT_EQ(bFile.readAll(), QByteArray("OLD"));  // conflict was never resolved, dest untouched
}

TEST(Files, ModeStatusBarShowsTrashConfirmation) {
  QTemporaryDir home(files_test::fixturePattern("fileops-window-trash-home"));
  QTemporaryDir src(files_test::fixturePattern("fileops-window-trash-src"));
  ASSERT_TRUE(home.isValid() && src.isValid());
  const files_test::ScopedXdgDataHome guard(home.path());
  files_test::writeFile(src, "gone.txt");
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(window));
  controller.open(src.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));

  QTest::keyClick(window, 'D', Qt::ShiftModifier);
  auto* trashLabel = window->findChild<QObject*>("trashConfirmLabel");
  ASSERT_NE(trashLabel, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return trashLabel->property("visible").toBool(); }));
  EXPECT_TRUE(trashLabel->property("rawText").toString().contains("Trash 1 item"));
  const auto capture = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
  if (!capture.isEmpty()) {
    QTest::qWait(60);
    EXPECT_TRUE(window->grabWindow().save(capture + "-fileops-trash-confirm.png"));
  }
  QTest::keyClick(window, Qt::Key_Y);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
  EXPECT_FALSE(QFile::exists(src.filePath("gone.txt")));
}

TEST(Files, PromptsCaptureKeysAndCtrlCInEveryModeWithoutChangingEditorState) {
  for (const auto* mode : {"normal", "visual", "search", "insert", "quicklook"}) {
    SCOPED_TRACE(mode);
    QTemporaryDir src(files_test::fixturePattern("prompt-modes-src"));
    QTemporaryDir dst(files_test::fixturePattern("prompt-modes-dst"));
    ASSERT_TRUE(src.isValid() && dst.isValid());
    const auto source = files_test::writeFile(src, "file.txt", "new");
    files_test::writeFile(dst, "file.txt", "old");
    DirectoryController controller;
    QQmlApplicationEngine engine;
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    engine.loadFromModule("HolonightFiles", "Main");
    ASSERT_EQ(engine.rootObjects().size(), 1);
    auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
    ASSERT_NE(window, nullptr);
    window->requestActivate();
    ASSERT_TRUE(QTest::qWaitForWindowActive(window));
    controller.open(src.path());
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
    const QString modeName = QString::fromLatin1(mode);
    if (modeName == "visual") {
      QTest::keyClick(window, Qt::Key_V);
    }
    if (modeName == "search") {
      QTest::keyClick(window, Qt::Key_Slash);
    }
    if (modeName == "insert") {
      QTest::keyClick(window, Qt::Key_I);
    }
    if (modeName == "quicklook") {
      ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->hasEntry(); }));
      QTest::keyClick(window, Qt::Key_Space);
      ASSERT_TRUE(controller.quickLookOpen());
    }
    const auto originalMode = controller.vim()->currentMode();
    QQuickItem* editor = nullptr;
    if (modeName == "search" || modeName == "insert") {
      const auto name = modeName == "search" ? QStringLiteral("searchField") : QStringLiteral("inlineNameEditor");
      ASSERT_TRUE(QTest::qWaitFor(
          [&] { return window->activeFocusItem() && window->activeFocusItem()->objectName() == name; }));
      editor = window->activeFocusItem();
      editor->setProperty("text", "draft-name");
      ASSERT_TRUE(QMetaObject::invokeMethod(editor, "select", Q_ARG(int, 1), Q_ARG(int, 5)));
    }
    const auto cursor = (editor != nullptr) ? editor->property("cursorPosition") : QVariant();
    const auto selectionStart = (editor != nullptr) ? editor->property("selectionStart") : QVariant();
    const auto selectionEnd = (editor != nullptr) ? editor->property("selectionEnd") : QVariant();
    controller.tasks()->enqueueCopy({source}, dst.path());
    ASSERT_TRUE(QTest::qWaitFor([&] { return controller.tasks()->hasPrompt(); }));
    EXPECT_FALSE(controller.quickLookOpen());
    QTest::keyClick(window, Qt::Key_Q, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_F, Qt::ShiftModifier);
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_TRUE(window->isVisible());
    EXPECT_TRUE(controller.tasks()->hasPrompt());
    EXPECT_EQ(controller.vim()->currentMode(), originalMode);
    QTest::keyClick(window, Qt::Key_S);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
    EXPECT_FALSE(controller.tasks()->hasPrompt());
    if (editor != nullptr) {
      EXPECT_EQ(window->activeFocusItem(), editor);
      EXPECT_EQ(editor->property("text").toString(), "draft-name");
      EXPECT_EQ(editor->property("cursorPosition"), cursor);
      EXPECT_EQ(editor->property("selectionStart"), selectionStart);
      EXPECT_EQ(editor->property("selectionEnd"), selectionEnd);
    }
    controller.tasks()->requestTrashConfirmation({source});
    ASSERT_TRUE(controller.tasks()->hasPrompt());
    QTest::keyClick(window, Qt::Key_Escape);
    EXPECT_FALSE(controller.tasks()->hasPrompt());
    EXPECT_EQ(controller.vim()->currentMode(), originalMode);
    EXPECT_TRUE(QFile::exists(source));
    controller.tasks()->enqueueCopy({source}, dst.path());
    ASSERT_TRUE(QTest::qWaitFor([&] { return controller.tasks()->hasPrompt(); }));
    QTest::keyClick(window, Qt::Key_C, Qt::ControlModifier);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
    EXPECT_FALSE(controller.tasks()->hasPrompt());
    if (editor != nullptr) {
      EXPECT_EQ(window->activeFocusItem(), editor);
      EXPECT_EQ(editor->property("text").toString(), "draft-name");
      EXPECT_EQ(editor->property("cursorPosition"), cursor);
      EXPECT_EQ(editor->property("selectionStart"), selectionStart);
      EXPECT_EQ(editor->property("selectionEnd"), selectionEnd);
    }
  }
}

namespace {

// Presses j until the preview shows `name` and has settled; fixture names sort in visiting order.
bool stepPreviewTo(DirectoryController& controller, const QString& name) {
  for (int step = 0; step < 32; ++step) {
    if (controller.preview()->name() == name) {
      return QTest::qWaitFor([&] { return !controller.preview()->busy(); });
    }
    controller.handleKey(QStringLiteral("j"));
    QTest::qWait(1);
  }
  return false;
}

struct BindingLoopCounter {
  std::atomic_int warnings{0};
  QtMessageHandler previous = nullptr;
};

BindingLoopCounter& bindingLoopCounter() {
  static BindingLoopCounter counter;
  return counter;
}

void countBindingLoops(QtMsgType type, const QMessageLogContext& context, const QString& message) {
  auto& counter = bindingLoopCounter();
  if (message.contains(QStringLiteral("Binding loop detected"))) {
    counter.warnings.fetch_add(1);
  }
  if (counter.previous != nullptr) {
    counter.previous(type, context, message);
  }
}

}  // namespace

// info-sidebar-redesign REQ-F-005/REQ-F-030: one formatter, so the listing and the sidebar agree.
TEST(Files, PreviewSidebarSizeMatchesListingAndDirectoriesShowDir) {
  QTemporaryDir dir(files_test::fixturePattern("sidebar-size"));
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  ASSERT_FALSE(files_test::writeFile(dir, "large.bin", QByteArray(5 * 1024 * 1024, 'x')).isEmpty());
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->resize(1200, 600);
  controller.open(dir.path());
  auto* list = window->findChild<QQuickItem*>("directoryListView");
  auto* sidebarSize = window->findChild<QQuickItem*>("previewSizeValue");
  ASSERT_NE(list, nullptr);
  ASSERT_NE(sidebarSize, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && controller.preview()->hasEntry(); }));

  ASSERT_TRUE(stepPreviewTo(controller, "folder"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return sidebarSize->isVisible(); }));
  EXPECT_EQ(sidebarSize->property("text").toString(), QStringLiteral("Dir"));

  ASSERT_TRUE(stepPreviewTo(controller, "large.bin"));
  ASSERT_TRUE(QTest::qWaitFor([&] {
    auto* row = list->property("currentItem").value<QQuickItem*>();
    return row != nullptr && row->findChild<QQuickItem*>("sizeColumnField") != nullptr &&
           row->findChild<QQuickItem*>("sizeColumnField")->property("text").toString() != "";
  }));
  auto* listingSize = list->property("currentItem").value<QQuickItem*>()->findChild<QQuickItem*>("sizeColumnField");
  const auto sidebarText = sidebarSize->property("text").toString();
  EXPECT_EQ(sidebarText, listingSize->property("text").toString());
  EXPECT_EQ(sidebarText, QStringLiteral("5.0 MB"));
  EXPECT_FALSE(sidebarText.contains('('));
}

// info-sidebar-redesign REQ-F-001/002/008/012/019/022, REQ-NF-001/002/005.
TEST(Files, PreviewSidebarRowsHideWrapAndStayFreeOfBindingLoops) {
  QTemporaryDir dir(files_test::fixturePattern("sidebar-rows"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeJpegWithExifBlob(dir, "01-portrait.jpg", files_test::buildSampleExifBlob(), QSize(60, 120));
  files_test::writeJpegWithExifBlob(dir, "02-landscape.jpg", files_test::buildSampleExifBlobWithoutLens(),
                                    QSize(120, 60));
  files_test::writeJpegWithExifBlob(dir, "03-long-lens.jpg",
                                    files_test::buildExifBlob({.lensModel = files_test::kLongLensModel}));
  files_test::writeJpegWithoutExif(dir, "04-no-exif.jpg");
  files_test::writeSmallText(dir, "05-notes.txt");
  files_test::writeCorruptJpeg(dir, "06-corrupt.jpg");
  const auto longName = QStringLiteral("07-") + QString(120, 'a') + QStringLiteral(".txt");
  ASSERT_FALSE(files_test::writeFile(dir, longName).isEmpty());

  bindingLoopCounter().warnings.store(0);
  bindingLoopCounter().previous = qInstallMessageHandler(countBindingLoops);
  const auto restoreHandler = qScopeGuard([] { qInstallMessageHandler(bindingLoopCounter().previous); });

  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->resize(1000, 700);
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && controller.preview()->hasEntry(); }));

  auto* pane = window->findChild<QQuickItem*>("previewPane");
  auto* imageArea = window->findChild<QQuickItem*>("previewImageArea");
  auto* metadataTable = window->findChild<QQuickItem*>("previewMetadataTable");
  auto* exifTable = window->findChild<QQuickItem*>("previewExifTable");
  auto* exifHeader = window->findChild<QQuickItem*>("previewExifHeader");
  auto* lensValue = window->findChild<QQuickItem*>("previewLensValue");
  auto* apertureValue = window->findChild<QQuickItem*>("previewApertureValue");
  auto* cameraValue = window->findChild<QQuickItem*>("previewCameraValue");
  auto* sizeValue = window->findChild<QQuickItem*>("previewSizeValue");
  auto* dimensionsValue = window->findChild<QQuickItem*>("previewDimensionsValue");
  auto* errorNotice = window->findChild<QQuickItem*>("previewErrorNotice");
  auto* fileName = window->findChild<QQuickItem*>("previewFileName");
  for (auto* item : {pane, imageArea, metadataTable, exifTable, exifHeader, lensValue, apertureValue, cameraValue,
                     sizeValue, dimensionsValue, errorNotice, fileName}) {
    ASSERT_NE(item, nullptr);
  }
  // Frame height follows the source aspect ratio, capped at 240 (REQ-F-001, REQ-NF-001).
  const auto frameSettles = [&](double aspect) {
    return QTest::qWaitFor([&] {
      return controller.preview()->hasImage() &&
             qAbs(imageArea->height() - qMin(240.0, qRound(imageArea->width() * aspect) * 1.0)) <= 1;
    });
  };

  ASSERT_TRUE(stepPreviewTo(controller, "01-portrait.jpg"));
  ASSERT_TRUE(frameSettles(2.0));
  EXPECT_LE(imageArea->height(), 240);
  EXPECT_TRUE(exifTable->isVisible());
  EXPECT_TRUE(lensValue->isVisible());
  EXPECT_EQ(dimensionsValue->property("text").toString(), QStringLiteral("60 \u00d7 120"));
  EXPECT_EQ(cameraValue->property("text").toString(), QStringLiteral("Holonight TestCam 1000"));

  ASSERT_TRUE(stepPreviewTo(controller, "02-landscape.jpg"));
  ASSERT_TRUE(frameSettles(0.5));
  EXPECT_TRUE(exifTable->isVisible());
  EXPECT_TRUE(cameraValue->isVisible());
  EXPECT_FALSE(lensValue->isVisible());
  EXPECT_FALSE(apertureValue->isVisible());

  // At the 220 px minimum a long lens model wraps instead of eliding or overflowing (REQ-NF-002).
  auto* container = window->findChild<QQuickItem*>("previewContainer");
  ASSERT_NE(container, nullptr);
  QQmlProperty(container, QStringLiteral("SplitView.preferredWidth"), qmlContext(container)).write(220);
  ASSERT_TRUE(QTest::qWaitFor([&] { return pane->width() <= 221; }));
  ASSERT_TRUE(stepPreviewTo(controller, "03-long-lens.jpg"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return lensValue->isVisible() && lensValue->property("lineCount").toInt() > 1; }));
  EXPECT_FALSE(lensValue->property("truncated").toBool());
  const auto paneRight = pane->mapToScene(QPointF(pane->width(), 0)).x();
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return lensValue->mapToScene(QPointF(lensValue->width(), 0)).x() <= paneRight + 1; }));
  EXPECT_LE(lensValue->property("contentWidth").toReal(), lensValue->width() + 1);

  ASSERT_TRUE(stepPreviewTo(controller, "04-no-exif.jpg"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->hasImage(); }));
  EXPECT_FALSE(exifTable->isVisible());
  EXPECT_FALSE(exifHeader->isVisible());
  ASSERT_TRUE(QTest::qWaitFor([&] { return dimensionsValue->isVisible(); }));
  QTest::qWait(20);  // let the GridLayout polish at this pane width
  const auto imageMetadataHeight = metadataTable->implicitHeight();

  // A hidden row reserves no space: same pane width, one row fewer (REQ-F-008).
  ASSERT_TRUE(stepPreviewTo(controller, "05-notes.txt"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !dimensionsValue->isVisible(); }));
  QTest::qWait(20);
  EXPECT_TRUE(sizeValue->isVisible());
  EXPECT_LT(metadataTable->implicitHeight(), imageMetadataHeight);

  ASSERT_TRUE(stepPreviewTo(controller, "06-corrupt.jpg"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return errorNotice->isVisible(); }));
  EXPECT_TRUE(sizeValue->isVisible());

  ASSERT_TRUE(stepPreviewTo(controller, longName));
  ASSERT_TRUE(QTest::qWaitFor([&] { return fileName->property("truncated").toBool(); }));
  EXPECT_EQ(fileName->property("elide").toInt(), int(Qt::ElideMiddle));

  EXPECT_EQ(bindingLoopCounter().warnings.load(), 0);
}

// REQ-F-004/011/019: both tables keep the mockup's common left edges as rows hide.
TEST(Files, PreviewSidebarTablesShareLeftAlignedColumns) {
  QTemporaryDir dir(files_test::fixturePattern("sidebar-alignment"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeJpegWithExifBlob(dir, "01-full.jpg", files_test::buildSampleExifBlob());
  files_test::writeJpegWithExifBlob(dir, "02-partial.jpg", files_test::buildSampleExifBlobWithoutLens());
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->resize(1000, 900);
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && controller.preview()->exifPresent(); }));
  auto* pane = window->findChild<QQuickItem*>("previewPane");
  auto* container = window->findChild<QQuickItem*>("previewContainer");
  auto* metadata = window->findChild<QQuickItem*>("previewMetadataTable");
  auto* exif = window->findChild<QQuickItem*>("previewExifTable");
  for (auto* item : {pane, container, metadata, exif}) {
    ASSERT_NE(item, nullptr);
  }
  const auto defaultWidth = container->width();
  const auto columnWidth = pane->property("labelColumnWidth").toReal();
  ASSERT_GT(columnWidth, 0);
  for (const auto width : {defaultWidth, 220.0}) {
    controller.handleKey(QStringLiteral("g"));
    controller.handleKey(QStringLiteral("g"));
    ASSERT_TRUE(
        QQmlProperty(container, QStringLiteral("SplitView.preferredWidth"), qmlContext(container)).write(width));
    ASSERT_TRUE(QTest::qWaitFor([&] { return qAbs(container->width() - width) <= 1; }));
    for (const auto* filename : {"01-full.jpg", "02-partial.jpg"}) {
      SCOPED_TRACE(QStringLiteral("%1 at %2 px").arg(filename).arg(width).toStdString());
      ASSERT_TRUE(stepPreviewTo(controller, filename));
      // Wait for both layouts to polish after selection and width changes.
      ASSERT_TRUE(
          QTest::qWaitFor([&] { return qAbs(metadata->width() - exif->width()) <= 1 && metadata->width() > 0; }));
      QTest::qWait(50);
      const auto labelLeft = metadata->mapToScene(QPointF()).x();
      const auto spacing = metadata->property("columnSpacing").toReal();
      const auto valueLeft = labelLeft + columnWidth + spacing;
      EXPECT_DOUBLE_EQ(exif->property("columnSpacing").toReal(), spacing);
      EXPECT_DOUBLE_EQ(pane->property("labelColumnWidth").toReal(), columnWidth);
      int visibleRows = 0;
      for (auto* table : {metadata, exif}) {
        const auto cells = table->childItems();
        ASSERT_EQ(cells.size() % 2, 0);
        for (qsizetype row = 0; row < cells.size(); row += 2) {
          auto* label = cells[row];
          auto* value = cells[row + 1];
          ASSERT_EQ(label->isVisible(), value->isVisible());
          if (!label->isVisible()) {
            continue;
          }
          ++visibleRows;
          EXPECT_EQ(label->property("horizontalAlignment").toInt(), int(Qt::AlignLeft));
          EXPECT_EQ(value->property("horizontalAlignment").toInt(), int(Qt::AlignLeft));
          EXPECT_NEAR(label->mapToScene(QPointF()).x(), labelLeft, 1);
          EXPECT_NEAR(value->mapToScene(QPointF()).x(), valueLeft, 1);
          EXPECT_NEAR(label->width(), columnWidth, 1);
          EXPECT_GE(label->width(), label->implicitWidth());
          EXPECT_NEAR(label->mapToScene(QPointF()).y(), value->mapToScene(QPointF()).y(), 1);
          EXPECT_LE(value->mapToItem(table, QPointF(value->width(), 0)).x(), table->width() + 1);
          EXPECT_FALSE(value->property("truncated").toBool());
        }
      }
      EXPECT_EQ(visibleRows, QString::fromLatin1(filename).contains("full") ? 9 : 7);
      const auto capture = qEnvironmentVariable("FILES_CAPTURE_PREFIX");
      if (!capture.isEmpty()) {
        EXPECT_TRUE(window->grabWindow().save(capture +
                                              QStringLiteral("-alignment-%1-%2.png").arg(filename).arg(qRound(width))));
      }
    }
  }
}

TEST(Files, PreviewSidebarScrollsToWrappedExifInShortWindows) {
  QTemporaryDir dir(files_test::fixturePattern("sidebar-scroll"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeJpegWithExifBlob(
      dir, "01-photo.jpg", files_test::buildExifBlob({.lensModel = files_test::kLongLensModel}), QSize(600, 900));
  files_test::writeSmallText(dir, "02-notes.txt");
  DirectoryController controller;
  QQmlApplicationEngine engine;
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  window->resize(1000, 500);
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && controller.preview()->exifPresent(); }));
  auto* container = window->findChild<QQuickItem*>("previewContainer");
  auto* scroll = window->findChild<QQuickItem*>("previewScrollArea");
  auto* lastRow = window->findChild<QQuickItem*>("previewFocalLengthValue");
  auto* fileName = window->findChild<QQuickItem*>("previewFileName");
  for (auto* item : {container, scroll, lastRow, fileName}) {
    ASSERT_NE(item, nullptr);
  }
  ASSERT_TRUE(QQmlProperty(container, QStringLiteral("SplitView.preferredWidth"), qmlContext(container)).write(220));
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return container->width() <= 221 && scroll->property("contentHeight").toReal() > scroll->height(); }));
  EXPECT_TRUE(scroll->clip());
  EXPECT_GT(lastRow->mapToItem(scroll, QPointF(0, lastRow->height())).y(), scroll->height());

  // Exercise real wheel delivery, not just a programmatic contentY assignment.
  const auto position = scroll->mapToScene(QPointF(scroll->width() / 2, scroll->height() / 2));
  for (int step = 0; step < 30 && !scroll->property("atYEnd").toBool(); ++step) {
    QWheelEvent event(position, window->mapToGlobal(position.toPoint()), QPoint(), QPoint(0, -120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    // Flickable uses event timestamps to distinguish successive wheel steps.
    event.setTimestamp(static_cast<ulong>((step + 1) * 200));
    QCoreApplication::sendEvent(window, &event);
    QTest::qWait(150);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !scroll->property("moving").toBool(); }));
  }
  ASSERT_TRUE(QTest::qWaitFor([&] { return scroll->property("atYEnd").toBool(); }));
  EXPECT_GE(lastRow->mapToItem(scroll, QPointF()).y(), 0);
  EXPECT_LE(lastRow->mapToItem(scroll, QPointF(0, lastRow->height())).y(), scroll->height() + 1);
  EXPECT_EQ(scroll->property("contentX").toReal(), 0);

  ASSERT_TRUE(stepPreviewTo(controller, "02-notes.txt"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return scroll->property("contentY").toReal() == 0; }));
  EXPECT_GE(fileName->mapToItem(scroll, QPointF()).y(), 0);
  EXPECT_LE(fileName->mapToItem(scroll, QPointF(0, fileName->height())).y(), scroll->height());
}

namespace {

// quick-look-redesign: a rendered main window at a fixed size with the overlay located.
struct QuickLookHarness {
  DirectoryController controller;
  QQmlApplicationEngine engine;
  QQuickWindow* window = nullptr;
  QObject* popup = nullptr;
  QQuickItem* overlay = nullptr;
};

void loadQuickLookHarness(QuickLookHarness& harness, const QString& path, QSize size = QSize(1280, 800)) {
  harness.engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&harness.controller)}});
  harness.engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(harness.engine.rootObjects().size(), 1);
  harness.window = qobject_cast<QQuickWindow*>(harness.engine.rootObjects().first());
  ASSERT_NE(harness.window, nullptr);
  harness.window->resize(size);
  harness.window->requestActivate();
  ASSERT_TRUE(QTest::qWaitForWindowActive(harness.window));
  harness.popup = harness.window->findChild<QObject*>("quickLookOverlay");
  ASSERT_NE(harness.popup, nullptr);
  harness.overlay = harness.popup->property("parent").value<QQuickItem*>();
  ASSERT_NE(harness.overlay, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.overlay->size() == QSizeF(size); }));
  harness.controller.open(path);
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return !harness.controller.scanning() && harness.controller.preview()->hasEntry(); }));
}

void openQuickLook(QuickLookHarness& harness) {
  QTest::keyClick(harness.window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.popup->property("opened").toBool(); }));
}

QColor paletteColor(QQmlEngine& engine, const char* name) {
  auto* palette = engine.singletonInstance<QObject*>("Holonight.Core", "HoloniightPalette");
  return palette != nullptr ? palette->property(name).value<QColor>() : QColor();
}

QSizeF popupSize(const QuickLookHarness& harness) {
  return {harness.popup->property("width").toReal(), harness.popup->property("height").toReal()};
}

QSizeF quickLookBounds(const QuickLookHarness& harness) { return harness.overlay->size() * 0.92; }

// SPEC.md "preview bounds": bounds minus card padding minus the caption reserve.
QSizeF quickLookPreviewBounds(const QuickLookHarness& harness) {
  const auto padding = harness.popup->property("cardPadding").toReal();
  const auto gap = harness.popup->property("frameCaptionGap").toReal();
  const auto reserve = harness.popup->property("captionReserve").toReal();
  const auto bounds = quickLookBounds(harness);
  return {bounds.width() - (2 * padding), bounds.height() - (2 * padding) - gap - reserve};
}

QString quickLookText(const QuickLookHarness& harness, const char* objectName) {
  auto* item = harness.window->findChild<QQuickItem*>(objectName);
  return item != nullptr ? item->property("text").toString() : QString();
}

QColor quickLookColor(const QuickLookHarness& harness, const char* objectName) {
  auto* item = harness.window->findChild<QQuickItem*>(objectName);
  return item != nullptr ? item->property("color").value<QColor>() : QColor();
}

// Worker-side gate for PreviewServiceTestAccess::beforeDispatch: jobs wait while `held` is set.
struct DispatchGate {
  std::atomic_bool held{false};
  std::atomic_int waiting{0};
};

void installDispatchGate(PreviewService& service, DispatchGate& gate) {
  PreviewServiceTestAccess::beforeDispatch(service, [&gate] {
    gate.waiting.fetch_add(1);
    while (gate.held.load()) {
      QThread::msleep(1);
    }
    gate.waiting.fetch_sub(1);
  });
}

void expectCardWithinBounds(const QuickLookHarness& harness) {
  const auto bounds = quickLookBounds(harness);
  const auto size = popupSize(harness);
  EXPECT_GT(size.width(), 0);
  EXPECT_GT(size.height(), 0);
  EXPECT_LE(size.width(), bounds.width() + 1);
  EXPECT_LE(size.height(), bounds.height() + 1);
  const auto centerX = harness.popup->property("x").toReal() + (size.width() / 2);
  const auto centerY = harness.popup->property("y").toReal() + (size.height() / 2);
  EXPECT_NEAR(centerX, harness.overlay->width() / 2, 1);
  EXPECT_NEAR(centerY, harness.overlay->height() / 2, 1);
}

}  // namespace

// quick-look-redesign REQ-F-001/002/005.
TEST(Files, QuickLookCardStaysWithinBoundsForEveryKind) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-card"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  ASSERT_FALSE(files_test::writeFile(dir, "01-image.jpg", files_test::renderJpegBytes({600, 400})).isEmpty());
  files_test::writeSmallText(dir, "02-notes.txt");
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_TRUE(stepPreviewTo(harness.controller, "folder"));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  auto* card = harness.window->findChild<QQuickItem*>("quickLookCard");
  ASSERT_NE(card, nullptr);
  const auto textDisabled = paletteColor(harness.engine, "textDisabled");
  const auto textMuted = paletteColor(harness.engine, "textMuted");
  ASSERT_TRUE(textDisabled.isValid());
  EXPECT_NE(textDisabled, textMuted);
  for (const auto* name : {"folder", "01-image.jpg", "02-notes.txt"}) {
    SCOPED_TRACE(name);
    ASSERT_TRUE(stepPreviewTo(harness.controller, name));
    QTest::qWait(20);
    expectCardWithinBounds(harness);
    EXPECT_GT(card->property("radius").toReal(), 0);
    EXPECT_NEAR(card->width(), popupSize(harness).width(), 1);
    EXPECT_NEAR(card->height(), popupSize(harness).height(), 1);
    EXPECT_TRUE(harness.window->findChild<QQuickItem*>("quickLookHint")->isVisible());
    EXPECT_EQ(quickLookText(harness, "quickLookHint"), QStringLiteral("Press Space or Esc to close"));
    EXPECT_EQ(quickLookColor(harness, "quickLookHint"), textDisabled);
    EXPECT_EQ(quickLookColor(harness, "quickLookMetadata"), textMuted);
  }
  EXPECT_EQ(harness.popup->property("closePolicy").toInt(), 0);
}

// quick-look-redesign REQ-F-003, REQ-C-004.
TEST(Files, QuickLookBackdropCoversWindowWithScrim) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-backdrop"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeSmallText(dir, "notes.txt");
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  QQuickItem* backdrop = nullptr;
  for (auto* child : harness.overlay->childItems()) {
    if (child->objectName() == QStringLiteral("quickLookBackdrop")) {
      backdrop = child;
    }
  }
  ASSERT_NE(backdrop, nullptr);
  EXPECT_TRUE(backdrop->isVisible());
  EXPECT_EQ(backdrop->size(), harness.overlay->size());
  EXPECT_EQ(backdrop->property("color").value<QColor>(), paletteColor(harness.engine, "scrim"));
  QTest::mouseClick(harness.window, Qt::LeftButton, Qt::NoModifier, QPoint(4, 4));
  QTest::qWait(50);
  EXPECT_TRUE(harness.controller.quickLookOpen());
  EXPECT_TRUE(harness.popup->property("visible").toBool());
}

// quick-look-redesign REQ-F-004.
TEST(Files, QuickLookNameElidesLongFilenamesAndStaysCentered) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-name"));
  ASSERT_TRUE(dir.isValid());
  const auto longName = QString(196, 'a') + QStringLiteral(".txt");
  ASSERT_FALSE(files_test::writeFile(dir, longName).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "b.txt").isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  auto* label = harness.window->findChild<QQuickItem*>("quickLookName");
  auto* card = harness.window->findChild<QQuickItem*>("quickLookCard");
  ASSERT_NE(label, nullptr);
  ASSERT_NE(card, nullptr);
  const auto expectCentered = [&] {
    const auto contentWidth = harness.popup->property("availableWidth").toReal();
    EXPECT_LE(label->width(), contentWidth + 1);
    const auto center = label->mapToItem(card, QPointF(label->width() / 2, 0)).x();
    EXPECT_NEAR(center, card->width() / 2, 1);
  };
  ASSERT_TRUE(stepPreviewTo(harness.controller, longName));
  ASSERT_TRUE(QTest::qWaitFor([&] { return label->property("truncated").toBool(); }));
  expectCentered();
  ASSERT_TRUE(stepPreviewTo(harness.controller, "b.txt"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return label->property("text").toString() == QStringLiteral("b.txt"); }));
  QTest::qWait(20);
  EXPECT_FALSE(label->property("truncated").toBool());
  expectCentered();
}

// quick-look-redesign REQ-F-006/007/008.
TEST(Files, QuickLookImageFrameAspectFitsPreviewBounds) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-image"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeFile(dir, "01-landscape.jpg", files_test::renderJpegBytes({6000, 4000})).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "02-tall.jpg", files_test::renderJpegBytes({1000, 5000})).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "03-wide.jpg", files_test::renderJpegBytes({5000, 1000})).isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }, 10000));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  auto* area = harness.window->findChild<QQuickItem*>("quickLookImageArea");
  ASSERT_NE(area, nullptr);
  const auto preview = quickLookPreviewBounds(harness);
  const auto waitForImage = [&](const QString& name) {
    return stepPreviewTo(harness.controller, name) &&
           QTest::qWaitFor([&] { return harness.controller.preview()->hasImage() && area->isVisible(); });
  };

  ASSERT_TRUE(waitForImage("01-landscape.jpg"));
  EXPECT_LE(qAbs((area->width() * 4000) - (area->height() * 6000)), 6000);
  const auto widthMatches = qAbs(area->width() - preview.width()) <= 1;
  const auto heightMatches = qAbs(area->height() - preview.height()) <= 1;
  EXPECT_NE(widthMatches, heightMatches);
  EXPECT_LE(area->width(), preview.width() + 1);
  EXPECT_LE(area->height(), preview.height() + 1);
  expectCardWithinBounds(harness);
  const auto items = area->childItems();
  ASSERT_FALSE(items.isEmpty());
  EXPECT_GT(items.first()->property("radius").toReal(), 0);
  EXPECT_EQ(items.first()->size(), area->size());
  EXPECT_EQ(quickLookText(harness, "quickLookMetadata"),
            QStringLiteral("6000 × 4000 · ") + quickLookText(harness, "previewSizeValue"));

  ASSERT_TRUE(waitForImage("02-tall.jpg"));
  EXPECT_NEAR(area->height(), preview.height(), 1);
  EXPECT_LT(area->width(), preview.width());
  EXPECT_LE(qAbs((area->width() * 5000) - (area->height() * 1000)), 5000);

  ASSERT_TRUE(waitForImage("03-wide.jpg"));
  EXPECT_NEAR(area->width(), preview.width(), 1);
  EXPECT_LT(area->height(), preview.height());
  EXPECT_LE(qAbs((area->width() * 1000) - (area->height() * 5000)), 5000);
  expectCardWithinBounds(harness);
}

// quick-look-redesign REQ-F-008: size only until the source dimensions are known.
TEST(Files, QuickLookImageMetadataLineShowsDimensionsThenSizeOnly) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-image-metadata"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeFile(dir, "a.txt").isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "b.jpg", files_test::renderJpegBytes({600, 400})).isEmpty());
  DispatchGate gate;
  QuickLookHarness harness;
  const auto release = qScopeGuard([&gate] { gate.held.store(false); });
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  installDispatchGate(*harness.controller.preview(), gate);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  gate.held.store(true);
  harness.controller.handleKey(QStringLiteral("j"));
  ASSERT_EQ(harness.controller.preview()->name(), QStringLiteral("b.jpg"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return gate.waiting.load() > 0; }));
  const auto sizeText = quickLookText(harness, "previewSizeValue");
  ASSERT_FALSE(sizeText.isEmpty());
  EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), sizeText);
  EXPECT_FALSE(quickLookText(harness, "quickLookMetadata").contains(QChar(0x00d7)));
  gate.held.store(false);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }));
  EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), QStringLiteral("600 × 400 · ") + sizeText);
}

// quick-look-redesign REQ-F-009/010.
TEST(Files, QuickLookTextFrameFillsPreviewBoundsWithMonospaceView) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-text"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeLargeText(dir, "01-large.txt");
  files_test::writeSmallText(dir, "02-small.txt");
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_TRUE(stepPreviewTo(harness.controller, "01-large.txt"));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  auto* text = harness.window->findChild<QQuickItem*>("quickLookText");
  ASSERT_NE(text, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return text->isVisible(); }));
  // TextEdit -> Flickable contentItem -> Flickable -> rounded surface -> preview frame.
  auto* flickable = text->parentItem() != nullptr ? text->parentItem()->parentItem() : nullptr;
  ASSERT_NE(flickable, nullptr);
  ASSERT_TRUE(flickable->inherits("QQuickFlickable"));
  auto* frame = flickable->parentItem() != nullptr ? flickable->parentItem()->parentItem() : nullptr;
  ASSERT_NE(frame, nullptr);
  const auto preview = quickLookPreviewBounds(harness);
  EXPECT_NEAR(frame->width(), preview.width(), 1);
  EXPECT_NEAR(frame->height(), preview.height(), 1);
  EXPECT_TRUE(text->property("readOnly").toBool());
  EXPECT_NE(text->property("wrapMode").toInt(), 0);
  auto* theme = harness.engine.singletonInstance<QObject*>("Holonight.Core", "HolonightTheme");
  ASSERT_NE(theme, nullptr);
  EXPECT_EQ(text->property("font").value<QFont>().family(), theme->property("monospaceFont").toString());
  ASSERT_TRUE(QTest::qWaitFor([&] { return flickable->property("contentHeight").toReal() > flickable->height(); }));
  EXPECT_EQ(flickable->property("contentWidth").toReal(), flickable->width());
  expectCardWithinBounds(harness);

  ASSERT_TRUE(harness.controller.preview()->textTruncated());
  const auto largeSize = quickLookText(harness, "previewSizeValue");
  EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), largeSize + QStringLiteral(" · truncated"));
  ASSERT_TRUE(stepPreviewTo(harness.controller, "02-small.txt"));
  ASSERT_FALSE(harness.controller.preview()->textTruncated());
  EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), quickLookText(harness, "previewSizeValue"));
}

// quick-look-redesign REQ-F-011/012.
TEST(Files, QuickLookCompactCardShowsDirIconAndStaysSmall) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-compact"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_TRUE(stepPreviewTo(harness.controller, "folder"));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  auto* icon = harness.window->findChild<QQuickItem*>("quickLookIcon");
  ASSERT_NE(icon, nullptr);
  EXPECT_TRUE(icon->isVisible());
  EXPECT_FALSE(icon->property("source").toUrl().isEmpty());
  const auto bounds = quickLookBounds(harness);
  const auto size = popupSize(harness);
  EXPECT_LT(size.width(), bounds.width() / 2);
  EXPECT_LT(size.height(), bounds.height() / 2);
  expectCardWithinBounds(harness);
  EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), QStringLiteral("Dir"));
  EXPECT_EQ(quickLookColor(harness, "quickLookMetadata"), paletteColor(harness.engine, "textMuted"));
  harness.window->resize(1920, 1080);
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.overlay->width() == 1920; }));
  EXPECT_EQ(popupSize(harness), size);
  expectCardWithinBounds(harness);
}

// quick-look-redesign REQ-F-012: errors in the error color, other types by description.
TEST(Files, QuickLookCompactCardShowsErrorAndMimeDescription) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-errors"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeRandomBinary(dir, "01-random.bin");
  files_test::writeBrokenSymlink(dir, "02-broken");
  files_test::writeCorruptJpeg(dir, "03-corrupt.jpg");
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_TRUE(stepPreviewTo(harness.controller, "01-random.bin"));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  const auto* preview = harness.controller.preview();
  const auto metadata = quickLookText(harness, "quickLookMetadata");
  EXPECT_FALSE(metadata.isEmpty());
  EXPECT_TRUE(metadata == preview->mimeTypeDescription() || metadata == preview->mimeType()) << metadata.toStdString();
  EXPECT_LT(popupSize(harness).width(), quickLookBounds(harness).width() / 2);
  const auto error = paletteColor(harness.engine, "error");
  for (const auto* name : {"02-broken", "03-corrupt.jpg"}) {
    SCOPED_TRACE(name);
    ASSERT_TRUE(stepPreviewTo(harness.controller, name));
    ASSERT_NE(preview->previewErrorKind(), PreviewService::PreviewErrorKind::None);
    ASSERT_FALSE(preview->previewErrorMessage().isEmpty());
    EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), preview->previewErrorMessage());
    EXPECT_EQ(quickLookColor(harness, "quickLookMetadata"), error);
    EXPECT_TRUE(harness.window->findChild<QQuickItem*>("quickLookIcon")->isVisible());
    expectCardWithinBounds(harness);
  }
}

// quick-look-redesign REQ-F-013/014/015.
TEST(Files, QuickLookRetainsSettledGeometryWhilePending) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-pending"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeFile(dir, "01-landscape.jpg", files_test::renderJpegBytes({600, 400})).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "02-portrait.jpg", files_test::renderJpegBytes({400, 600})).isEmpty());
  DispatchGate gate;
  QuickLookHarness harness;
  const auto release = qScopeGuard([&gate] { gate.held.store(false); });
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  installDispatchGate(*harness.controller.preview(), gate);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return !harness.controller.preview()->busy() && harness.controller.preview()->hasImage(); }));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  QTest::qWait(400);  // Let the Quick Look sized re-decode finish.
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }));
  const auto settled = popupSize(harness);
  auto* busy = harness.window->findChild<QQuickItem*>("quickLookBusy");
  auto* area = harness.window->findChild<QQuickItem*>("quickLookImageArea");
  ASSERT_NE(busy, nullptr);
  ASSERT_NE(area, nullptr);
  EXPECT_FALSE(busy->isVisible());

  QSignalSpy widthChanges(harness.popup, SIGNAL(widthChanged()));
  QSignalSpy heightChanges(harness.popup, SIGNAL(heightChanged()));
  gate.held.store(true);
  harness.controller.handleKey(QStringLiteral("j"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return gate.waiting.load() > 0; }));
  QTest::qWait(50);
  EXPECT_EQ(popupSize(harness), settled);
  EXPECT_EQ(widthChanges.count(), 0);
  EXPECT_EQ(heightChanges.count(), 0);
  EXPECT_EQ(quickLookText(harness, "quickLookName"), QStringLiteral("02-portrait.jpg"));
  const auto sizeText = quickLookText(harness, "previewSizeValue");
  EXPECT_FALSE(sizeText.isEmpty());
  EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), sizeText);
  EXPECT_TRUE(busy->isVisible());
  EXPECT_TRUE(busy->property("running").toBool());
  EXPECT_FALSE(area->isVisible());

  gate.held.store(false);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return !harness.controller.preview()->busy() && harness.controller.preview()->hasImage(); }));
  QTest::qWait(400);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }));
  EXPECT_LE(widthChanges.count(), 1);
  EXPECT_LE(heightChanges.count(), 1);
  const auto portrait = popupSize(harness);
  EXPECT_LT(portrait.width(), settled.width());
  EXPECT_GT(portrait.height(), settled.height() - 1);
  EXPECT_FALSE(busy->isVisible());
  EXPECT_TRUE(area->isVisible());
  EXPECT_LE(qAbs((area->width() * 600) - (area->height() * 400)), 600);
}

// quick-look-redesign REQ-F-016.
TEST(Files, QuickLookReopenOnDifferentKindUsesNewGeometry) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-reopen"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  ASSERT_FALSE(files_test::writeFile(dir, "image.jpg", files_test::renderJpegBytes({600, 400})).isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_TRUE(stepPreviewTo(harness.controller, "folder"));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  const auto compact = popupSize(harness);
  QTest::keyClick(harness.window, Qt::Key_Escape);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.popup->property("visible").toBool(); }));
  ASSERT_TRUE(stepPreviewTo(harness.controller, "image.jpg"));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  auto* area = harness.window->findChild<QQuickItem*>("quickLookImageArea");
  ASSERT_NE(area, nullptr);
  const auto preview = quickLookPreviewBounds(harness);
  const auto expected = QSizeF(600, 400).scaled(preview, Qt::KeepAspectRatio);
  EXPECT_NEAR(area->width(), expected.width(), 1);
  EXPECT_NEAR(area->height(), expected.height(), 1);
  EXPECT_GT(popupSize(harness).width(), compact.width());
  EXPECT_GT(popupSize(harness).height(), compact.height());
  expectCardWithinBounds(harness);
}

// quick-look-redesign REQ-F-017/018.
TEST(Files, QuickLookRequestedSizeStableAcrossNavigationButNotResize) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-request"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeFile(dir, "01.jpg", files_test::renderJpegBytes({600, 400})).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "02.jpg", files_test::renderJpegBytes({400, 600})).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "03.jpg", files_test::renderJpegBytes({1000, 200})).isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  const auto& preview = *harness.controller.preview();
  const auto requested = PreviewServiceTestAccess::quickLookRequestedSize(preview);
  const auto calls = harness.popup->property("requestedSizeCallCount").toInt();
  EXPECT_TRUE(requested.isValid() && !requested.isEmpty());
  for (const auto* name : {"02.jpg", "03.jpg"}) {
    ASSERT_TRUE(stepPreviewTo(harness.controller, name));
    QTest::qWait(50);
    EXPECT_EQ(PreviewServiceTestAccess::quickLookRequestedSize(preview), requested);
    EXPECT_EQ(harness.popup->property("requestedSizeCallCount").toInt(), calls);
  }
  harness.window->resize(1000, 700);
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.overlay->width() == 1000; }));
  EXPECT_GT(harness.popup->property("requestedSizeCallCount").toInt(), calls);
  EXPECT_NE(PreviewServiceTestAccess::quickLookRequestedSize(preview), requested);
}

// quick-look-redesign REQ-NF-001.
TEST(Files, QuickLookNavigationAndResizeProduceNoBindingLoops) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-loops"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("folder"));
  ASSERT_FALSE(files_test::writeFile(dir, "01.jpg", files_test::renderJpegBytes({600, 400})).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "02.jpg", files_test::renderJpegBytes({200, 900})).isEmpty());
  files_test::writeSmallText(dir, "03.txt");
  files_test::writeCorruptJpeg(dir, "04.jpg");
  files_test::writeBrokenSymlink(dir, "05-broken");

  bindingLoopCounter().warnings.store(0);
  bindingLoopCounter().previous = qInstallMessageHandler(countBindingLoops);
  const auto restoreHandler = qScopeGuard([] { qInstallMessageHandler(bindingLoopCounter().previous); });

  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  const QList<QSize> sizes{{1280, 800}, {640, 420}, {1600, 1000}};
  for (const auto size : sizes) {
    harness.window->resize(size);
    ASSERT_TRUE(QTest::qWaitFor([&] { return harness.overlay->size() == QSizeF(size); }));
    for (const auto* name : {"folder", "01.jpg", "02.jpg", "03.txt", "04.jpg", "05-broken"}) {
      ASSERT_TRUE(stepPreviewTo(harness.controller, name));
      expectCardWithinBounds(harness);
    }
    for (int step = 0; step < 5; ++step) {
      QTest::keyClick(harness.window, Qt::Key_K);
    }
    ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }));
  }
  EXPECT_EQ(bindingLoopCounter().warnings.load(), 0);
}

// quick-look-redesign regression: navigating onto files inside Quick Look emits navigated(), which
// used to pull focus onto the listing behind the modal popup.
TEST(Files, QuickLookKeepsFocusWhileNavigatingAndCloses) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-close"));
  ASSERT_TRUE(dir.isValid());
  for (int i = 0; i < 40; ++i) {
    const QSize size = (i % 2) == 0 ? QSize(300, 200) : QSize(120, 240);
    ASSERT_FALSE(files_test::writeFile(dir, QStringLiteral("%1.jpg").arg(i, 2, 10, QLatin1Char('0')),
                                       files_test::renderJpegBytes(size))
                     .isEmpty());
  }
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  auto* content = harness.window->findChild<QQuickItem*>("quickLookContent");
  ASSERT_NE(content, nullptr);
  for (const auto closeKey : {Qt::Key_Escape, Qt::Key_Space}) {
    SCOPED_TRACE(closeKey);
    ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
    const auto startRow = harness.controller.cursorRow();
    for (int step = 0; step < 30; ++step) {
      QTest::keyClick(harness.window, closeKey == Qt::Key_Escape ? Qt::Key_J : Qt::Key_K);
      QTest::qWait(5);
      ASSERT_EQ(harness.window->activeFocusItem(), content) << "step " << step;
    }
    EXPECT_NE(harness.controller.cursorRow(), startRow);
    QTest::keyClick(harness.window, closeKey);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.popup->property("visible").toBool(); }));
    EXPECT_FALSE(harness.controller.quickLookOpen());
    EXPECT_TRUE(harness.window->findChild<QQuickItem*>("directoryListView")->hasActiveFocus());
  }
}

// REQ-F-001/005/013: pending geometry follows resized bounds, including its caption.
TEST(Files, QuickLookPendingResizeKeepsCaptionInsideCard) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-pending-resize"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeFile(dir, "01.jpg", files_test::renderJpegBytes({600, 400})).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "02.jpg", files_test::renderJpegBytes({400, 600})).isEmpty());
  DispatchGate gate;
  QuickLookHarness harness;
  const auto release = qScopeGuard([&gate] { gate.held.store(false); });
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  QTest::qWait(400);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }));
  installDispatchGate(*harness.controller.preview(), gate);
  gate.held.store(true);
  harness.controller.handleKey(QStringLiteral("j"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return gate.waiting.load() > 0; }));
  const auto settled = popupSize(harness);
  harness.window->resize(640, 420);
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.overlay->size() == QSizeF(640, 420); }));
  auto* hint = harness.window->findChild<QQuickItem*>("quickLookHint");
  auto* card = harness.window->findChild<QQuickItem*>("quickLookCard");
  ASSERT_NE(hint, nullptr);
  ASSERT_NE(card, nullptr);
  const auto bottom = hint->mapToItem(card, QPointF(0, hint->height())).y();
  EXPECT_LE(bottom, card->height());
  auto* area = harness.window->findChild<QQuickItem*>("quickLookImageArea");
  ASSERT_NE(area, nullptr);
  auto* frame = area->parentItem();
  ASSERT_NE(frame, nullptr);
  EXPECT_GE(frame->mapToItem(card, QPointF()).x(), 0);
  EXPECT_LE(frame->mapToItem(card, QPointF(frame->width(), frame->height())).x(), card->width());
  expectCardWithinBounds(harness);
  EXPECT_LE(hint->mapToScene(QPointF(0, hint->height())).y(), harness.window->height());
  harness.window->resize(1280, 800);
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.overlay->size() == QSizeF(1280, 800); }));
  EXPECT_EQ(popupSize(harness), settled);
  EXPECT_LE(hint->mapToItem(card, QPointF(0, hint->height())).y(), card->height());
  EXPECT_TRUE(harness.controller.preview()->busy());
  EXPECT_FALSE(harness.controller.preview()->hasImage());
}
