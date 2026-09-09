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
  EXPECT_TRUE(
      window->findChild<QObject*>("statusLabel")->property("rawText").toString().contains(dir.filePath("missing")));
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
  const QGuiApplication app(argc, argv);
  ::testing::InitGoogleTest(&argc, argv);
  return RUN_ALL_TESTS();
}

#include "smoke.moc"
