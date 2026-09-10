#include "directory_controller.h"
#include "directory_fixtures.h"

#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>

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
  source.fill(Qt::darkCyan);
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
  QString formattedSize;
  ASSERT_TRUE(
      QMetaObject::invokeMethod(pane, "formatSize", Q_RETURN_ARG(QString, formattedSize), Q_ARG(double, 1440054)));
  EXPECT_TRUE(formattedSize.contains("1440054"));
  EXPECT_TRUE(formattedSize.contains("MiB"));
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
    const auto needed = source.size().scaled((area->size() * window->devicePixelRatio()).toSize(), Qt::KeepAspectRatio);
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
