#include <QCoreApplication>
#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QTest>
#include <QtQml/QQmlExtensionPlugin>

#include <gtest/gtest.h>
#include <memory>

Q_IMPORT_QML_PLUGIN(HolonightFilesPlugin)

TEST(Files, WindowAndKeyboard) {
  QQmlApplicationEngine engine;
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  EXPECT_TRUE(window->isVisible());
  EXPECT_FALSE(window->flags().testFlag(Qt::FramelessWindowHint));
  auto* empty = window->findChild<QObject*>(QStringLiteral("emptyState"));
  ASSERT_NE(empty, nullptr);
  EXPECT_EQ(empty->property("titleText").toString(), QStringLiteral("No folder open"));
  window->resize(420, 280);
  auto* empty_item = qobject_cast<QQuickItem*>(empty);
  ASSERT_NE(empty_item, nullptr);
  QCoreApplication::processEvents();
  EXPECT_GE(empty_item->x(), 0);
  EXPECT_GE(empty_item->y(), 0);
  EXPECT_LE(empty_item->x() + empty_item->width(), window->width());
  EXPECT_LE(empty_item->y() + empty_item->height(), window->height());
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
  const auto initial_visibility = window->visibility();
  QTest::keyClick(window, Qt::Key_F);
  EXPECT_TRUE(QTest::qWaitFor([window] { return window->visibility() == QWindow::FullScreen; }));
  QTest::keyClick(window, Qt::Key_F);
  EXPECT_TRUE(QTest::qWaitFor([window, initial_visibility] { return window->visibility() == initial_visibility; }));
  QTest::keyClick(window, Qt::Key_F);
  QTest::keyClick(window, Qt::Key_Escape);
  EXPECT_TRUE(QTest::qWaitFor([window, initial_visibility] { return window->visibility() == initial_visibility; }));
  QTest::keyClick(window, Qt::Key_Escape);
  EXPECT_TRUE(QTest::qWaitFor([window, initial_visibility] { return window->visibility() == initial_visibility; }));
  window->showMaximized();
  ASSERT_TRUE(QTest::qWaitFor([window] { return window->visibility() == QWindow::Maximized; }));
  QTest::keyClick(window, Qt::Key_F);
  QTest::keyClick(window, Qt::Key_Escape);
  EXPECT_EQ(window->visibility(), QWindow::Maximized);
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
