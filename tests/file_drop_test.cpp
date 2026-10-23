#include "directory_controller.h"
#include "directory_fixtures.h"
#include "engine_setup.h"
#include "file_drop_area.h"
#include "file_transfer.h"

#include <QDragEnterEvent>
#include <QMimeData>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QTemporaryDir>
#include <QTest>
#include <QtVersionChecks>

#include <gtest/gtest.h>

namespace {
// Qt 6.12 accepts floating-point drag coordinates; keep Qt 6.11 constructors supported.
auto dragPosition(QPoint position) {
#if QT_VERSION >= QT_VERSION_CHECK(6, 12, 0)
  return QPointF(position);
#else
  return position;
#endif
}
class DropArea : public FileDropArea {
 public:
  using FileDropArea::dragEnterEvent;
  using FileDropArea::dragLeaveEvent;
  using FileDropArea::dragMoveEvent;
  using FileDropArea::dropEvent;
};
int rowNamed(DirectoryController& controller, const QString& name) {
  for (int row = 0; row < controller.listing()->rowCount(); ++row) {
    if (controller.listing()->index(row, 0).data(DirectoryModel::NameRole).toString() == name) {
      return row;
    }
  }
  return -1;
}
}  // namespace
TEST(FileDrop, ModifierPolicyRejectsUnsupportedRequests) {
  const auto both = Qt::CopyAction | Qt::MoveAction;
  EXPECT_EQ(FileTransfer::action(Qt::NoModifier, both), Qt::CopyAction);
  EXPECT_EQ(FileTransfer::action(Qt::ControlModifier, both), Qt::CopyAction);
  EXPECT_EQ(FileTransfer::action(Qt::ShiftModifier, both), Qt::MoveAction);
  EXPECT_EQ(FileTransfer::action(Qt::ShiftModifier, Qt::CopyAction), Qt::IgnoreAction);
  EXPECT_EQ(FileTransfer::action(Qt::NoModifier, Qt::MoveAction), Qt::IgnoreAction);
  EXPECT_EQ(FileTransfer::action(Qt::ControlModifier | Qt::ShiftModifier, both), Qt::IgnoreAction);
  EXPECT_EQ(FileTransfer::action(Qt::AltModifier, both), Qt::IgnoreAction);
}
TEST(FileDrop, StrictBatchValidationAndDeduplication) {
  const auto local = QUrl::fromLocalFile(QStringLiteral("/tmp/space % # é"));
  EXPECT_EQ(FileTransfer::localPaths({local, local}), QStringList{local.toLocalFile()});
  for (const auto& bad : {QUrl(), QUrl("https://example.org/a"), QUrl("file://server/tmp/a"), QUrl("relative"),
                          QUrl("file:///tmp/a#fragment")}) {
    EXPECT_TRUE(FileTransfer::localPaths({local, bad}).isEmpty());
  }
  EXPECT_TRUE(FileTransfer::parseUriList(local.toEncoded() + "\r\nfile:///bad%zz\r\n").isEmpty());
}
TEST(FileDrop, PayloadSelectionDestinationsAndGuards) {
  QTemporaryDir src(files_test::fixturePattern("drag-payload"));
  const auto name = QStringLiteral("space % # é.txt");
  files_test::writeFile(src, name);
  ASSERT_TRUE(QDir(src.path()).mkdir("child"));
  DirectoryController controller;
  controller.open(src.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  const int file = rowNamed(controller, name);
  const int child = rowNamed(controller, "child");
  ASSERT_GE(file, 0);
  EXPECT_TRUE(controller.dragUriList(0).isEmpty());
  EXPECT_EQ(QUrl(controller.dragUriList(file).trimmed()).toLocalFile(), src.filePath(name));
  EXPECT_EQ(controller.dropDestination(file), src.path());
  EXPECT_EQ(controller.dropDestination(-1), src.path());
  EXPECT_EQ(controller.dropDestination(child), src.filePath("child"));
  EXPECT_EQ(controller.dropDestination(0), QFileInfo(src.path()).absolutePath());
  controller.vim()->enterVisual(0);
  controller.vim()->extendVisual(file);
  const auto payload = controller.dragUriList(file);
  EXPECT_FALSE(payload.contains("/../"));
  EXPECT_EQ(payload.count("file:"), 2);
  controller.vim()->exitVisual();
  controller.setProperty("finderOpen", true);
  EXPECT_FALSE(controller.transfersEnabled());
  EXPECT_TRUE(controller.dragUriList(file).isEmpty());
  controller.setProperty("finderOpen", false);
  controller.handleKey("/");
  EXPECT_FALSE(controller.transfersEnabled());
  controller.cancelSearchEditing();
  EXPECT_TRUE(controller.transfersEnabled());
  controller.shutdown();
  EXPECT_FALSE(controller.transfersEnabled());
}
TEST(FileDrop, NativeEventsCopyMoveAndCleanFeedback) {
  QTemporaryDir src(files_test::fixturePattern("drop-source"));
  QTemporaryDir dst(files_test::fixturePattern("drop-destination"));
  files_test::writeFile(src, "one");
  files_test::writeFile(src, "two");
  ASSERT_TRUE(QDir(dst.path()).mkdir("child"));
  DirectoryController controller;
  controller.open(dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  DropArea area;
  area.setProperty("controller", QVariant::fromValue(&controller));
  QMimeData mime;
  mime.setUrls({QUrl::fromLocalFile(src.filePath("one"))});
  QDragEnterEvent enter(dragPosition(QPoint(20, 20)), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton,
                        Qt::NoModifier);
  area.dragEnterEvent(&enter);
  EXPECT_TRUE(area.hovering());
  EXPECT_EQ(area.action(), Qt::CopyAction);
  EXPECT_EQ(area.position(), QPointF(20, 20));
  QDragLeaveEvent leave;
  area.dragLeaveEvent(&leave);
  EXPECT_FALSE(area.hovering());
  area.dragEnterEvent(&enter);
  area.setProperty("targetRow", -1);
  QDropEvent copy({20, 20}, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
  area.dropEvent(&copy);
  EXPECT_TRUE(copy.isAccepted());
  EXPECT_FALSE(area.hovering());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
  EXPECT_TRUE(QFile::exists(dst.filePath("one")));
  EXPECT_TRUE(QFile::exists(src.filePath("one")));
  mime.setUrls({QUrl::fromLocalFile(src.filePath("two"))});
  area.setProperty("targetRow", rowNamed(controller, "child"));
  QDropEvent move({20, 20}, Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::ShiftModifier);
  area.dropEvent(&move);
  EXPECT_TRUE(move.isAccepted());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
  EXPECT_TRUE(QFile::exists(dst.filePath("child/two")));
  EXPECT_FALSE(QFile::exists(src.filePath("two")));
  area.dragEnterEvent(&enter);
  area.setEnabled(false);
  EXPECT_FALSE(area.hovering());
}

TEST(FileDrop, IconTilesKeepEightPixelGapsAndPaddingAcrossResizeAndScroll) {
  QTemporaryDir dir(files_test::fixturePattern("icon-spacing"));
  for (int i = 0; i < 11; ++i) {
    files_test::writeFile(dir, QStringLiteral("file%1.txt").arg(i, 2, 10, QLatin1Char('0')));
  }
  DirectoryController controller;
  controller.setIconView(true);
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  QQmlEngine engine;
  initializeFilesEngine(engine);
  QQmlComponent component(&engine);
  component.loadFromModule("HolonightFiles", "DirectoryListing");
  std::unique_ptr<QObject> object(
      component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}}));
  ASSERT_NE(object, nullptr) << component.errorString().toStdString();
  auto* root = qobject_cast<QQuickItem*>(object.get());
  ASSERT_NE(root, nullptr);
  QQuickWindow window;
  root->setParentItem(window.contentItem());
  window.setGeometry(0, 0, 700, 300);
  window.show();
  auto* grid = root->findChild<QQuickItem*>("directoryGridView");
  ASSERT_NE(grid, nullptr);
  auto tile = [&](int row) -> QQuickItem* {
    for (auto* cell : grid->property("contentItem").value<QQuickItem*>()->childItems()) {
      if (cell->property("index").toInt() == row) {
        if (auto* item = cell->findChild<QQuickItem*>("directoryIconDelegate")) {
          return item;
        }
      }
    }
    return nullptr;
  };
  for (int width : {700, 430, 120}) {
    SCOPED_TRACE(width);
    root->setSize({static_cast<qreal>(width), 300});
    controller.selectEntry(0);
    QMetaObject::invokeMethod(grid, "forceLayout");
    const int columns = grid->property("columns").toInt();
    ASSERT_TRUE(QTest::qWaitFor([&] { return tile(0) && tile(columns); }));
    auto* first = tile(0);
    const auto start = first->mapToItem(root, QPointF());
    EXPECT_NEAR(start.x(), 8, 0.01);
    EXPECT_NEAR(start.y(), 8, 0.01);
    EXPECT_EQ(first->height(), 128);
    EXPECT_TRUE(first->property("highlighted").toBool());
    auto* icon = first->findChild<QQuickItem*>("iconColumnField");
    ASSERT_NE(icon, nullptr);
    EXPECT_EQ(icon->size(), QSizeF(64, 64));
    auto* nextRow = tile(columns);
    EXPECT_NEAR(nextRow->mapToItem(root, QPointF()).y() - start.y() - first->height(), 8, 0.01);
    if (columns > 1) {
      ASSERT_NE(tile(1), nullptr);
      EXPECT_NEAR(tile(1)->mapToItem(root, QPointF()).x() - start.x() - first->width(), 8, 0.01);
    }
    ASSERT_NE(tile(columns - 1), nullptr);
    auto* lastColumn = tile(columns - 1);
    EXPECT_NEAR(width - lastColumn->mapToItem(root, QPointF(lastColumn->width(), 0)).x(), 8, 0.01);
    grid->forceActiveFocus();
    QTest::keyClick(&window, Qt::Key_Down);
    EXPECT_EQ(controller.cursorRow(), columns);
    controller.selectEntry(10);
    ASSERT_TRUE(QTest::qWaitFor([&] { return tile(10); }));
    QMetaObject::invokeMethod(grid, "positionViewAtEnd");
    QMetaObject::invokeMethod(grid, "forceLayout");
    auto* last = tile(10);
    EXPECT_NEAR(300 - last->mapToItem(root, QPointF(0, last->height())).y(), 8, 0.01);
    const auto center = last->mapToScene(QPointF(last->width() / 2, last->height() / 2)).toPoint();
    QTest::mouseClick(&window, Qt::LeftButton, Qt::NoModifier, center);
    EXPECT_EQ(controller.cursorRow(), 10);
    ASSERT_TRUE(controller.handleKey("a"));
    ASSERT_TRUE(QTest::qWaitFor([&] {
      auto* editor = tile(10)->findChild<QQuickItem*>("inlineNameEditor");
      return editor && editor->isVisible() && editor->hasActiveFocus();
    }));
    QTest::keyClick(&window, Qt::Key_Escape);
    ASSERT_TRUE(QTest::qWaitFor([&] { return grid->hasActiveFocus(); }));
  }
}

TEST(FileDrop, OffscreenListingRecomputesTargetsAndClampsEdgeScrolling) {
  for (bool icons : {false, true}) {
    QTemporaryDir dir(files_test::fixturePattern("drop-scroll"));
    for (int i = 0; i < 40; ++i) {
      ASSERT_TRUE(QDir(dir.path()).mkdir(QStringLiteral("folder%1").arg(i, 2, 10, QLatin1Char('0'))));
    }
    DirectoryController controller;
    controller.setIconView(icons);
    controller.open(dir.path());
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
    QQmlEngine engine;
    initializeFilesEngine(engine);
    QQmlComponent component(&engine);
    component.loadFromModule("HolonightFiles", "DirectoryListing");
    std::unique_ptr<QObject> object(
        component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}}));
    ASSERT_NE(object, nullptr) << component.errorString().toStdString();
    auto* root = qobject_cast<QQuickItem*>(object.get());
    ASSERT_NE(root, nullptr);
    QQuickWindow window;
    root->setParentItem(window.contentItem());
    root->setSize({700, 240});
    window.setGeometry(0, 0, 700, 240);
    window.show();
    auto* area = root->findChild<FileDropArea*>("fileDropArea");
    auto* list = root->findChild<QQuickItem*>(icons ? "directoryGridView" : "directoryListView");
    ASSERT_NE(area, nullptr);
    ASSERT_NE(list, nullptr);
    ASSERT_TRUE(QTest::qWaitFor([&] { return list->property("contentHeight").toReal() > list->height(); }));
    QMetaObject::invokeMethod(list, "forceLayout");
    QMimeData mime;
    mime.setUrls({QUrl::fromLocalFile(dir.filePath("folder00"))});
    if (icons) {
      auto checkTarget = [&](QPoint point, int expected) {
        QDragMoveEvent move(dragPosition(point), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
        QCoreApplication::sendEvent(area, &move);
        EXPECT_EQ(area->property("targetRow").toInt(), expected);
      };
      const QPoint secondCell(qRound(list->property("cellWidth").toReal() * 1.5) + 4, 60);
      QDragEnterEvent horizontal(dragPosition(secondCell), Qt::CopyAction, &mime, Qt::LeftButton, Qt::NoModifier);
      QCoreApplication::sendEvent(area, &horizontal);
      EXPECT_EQ(area->property("targetRow").toInt(), 1);
      EXPECT_EQ(controller.dropDestination(1), dir.filePath("folder01"));
      auto* outline = area->findChild<QQuickItem*>("iconDropOutline");
      ASSERT_NE(outline, nullptr);
      EXPECT_TRUE(outline->isVisible());
      EXPECT_NEAR(outline->x(), list->property("cellWidth").toReal() + 8, 0.01);
      EXPECT_NEAR(outline->y(), 8, 0.01);
      EXPECT_NEAR(outline->width(), list->property("cellWidth").toReal() - 8, 0.01);
      EXPECT_EQ(outline->height(), 128);
      list->setProperty("contentY", 10);
      EXPECT_NEAR(outline->y(), -2, 0.01);
      list->setProperty("contentY", 0);
      checkTarget({2, 60}, -1);
      checkTarget({6, 60}, -1);
      checkTarget({qRound(list->property("cellWidth").toReal()) + 4, 60}, -1);
      checkTarget({60, 140}, -1);
      root->setWidth(430);
      QMetaObject::invokeMethod(list, "forceLayout");
      checkTarget({qRound(list->property("cellWidth").toReal() * 1.5) + 4, 60}, 1);
    }
    const QPoint bottom(100, qRound(area->height()) - 16);
    QDragEnterEvent enter(dragPosition(bottom), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton, Qt::NoModifier);
    QCoreApplication::sendEvent(area, &enter);
    ASSERT_TRUE(area->hovering());
    const int first = area->property("targetRow").toInt();
    ASSERT_GE(first, 0);
    ASSERT_TRUE(QTest::qWaitFor([&] { return area->property("targetRow").toInt() > first; }));
    const qreal maximum =
        list->property("originY").toReal() + list->property("contentHeight").toReal() - list->height();
    list->setProperty("contentY", maximum);
    QTest::qWait(100);
    EXPECT_LE(list->property("contentY").toReal(), maximum);
    QDragMoveEvent top(dragPosition(QPoint(100, 1)), Qt::CopyAction | Qt::MoveAction, &mime, Qt::LeftButton,
                       Qt::NoModifier);
    QCoreApplication::sendEvent(area, &top);
    list->setProperty("contentY", list->property("originY"));
    QTest::qWait(100);
    EXPECT_GE(list->property("contentY").toReal(), list->property("originY").toReal());
    QDragLeaveEvent leave;
    QCoreApplication::sendEvent(area, &leave);
    EXPECT_FALSE(area->hovering());
    EXPECT_EQ(area->property("targetRow").toInt(), -1);
  }
}

TEST(FileDrop, ConflictsCancellationStaleSourcesAndWorkerSafeguards) {
  QTemporaryDir src(files_test::fixturePattern("drop-conflict-source"));
  QTemporaryDir dst(files_test::fixturePattern("drop-conflict-destination"));
  files_test::writeFile(src, "same", "new");
  files_test::writeFile(dst, "same", "old");
  DirectoryController controller;
  controller.open(dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  const QList<QUrl> urls{QUrl::fromLocalFile(src.filePath("same"))};
  ASSERT_TRUE(controller.submitDrop(urls, -1, Qt::CopyAction));
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.tasks()->hasPrompt(); }));
  EXPECT_FALSE(controller.submitDrop(urls, -1, Qt::CopyAction));
  controller.tasks()->cancelCurrentTask();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
  EXPECT_TRUE(QFile::exists(src.filePath("same")));
  ASSERT_TRUE(controller.submitDrop({QUrl::fromLocalFile(src.filePath("missing"))}, -1, Qt::CopyAction));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
  EXPECT_EQ(controller.tasks()->lastSummary().failed, 1);
  ASSERT_TRUE(QDir(src.path()).mkdir("child"));
  controller.open(src.filePath("child"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  ASSERT_TRUE(controller.submitDrop({QUrl::fromLocalFile(src.path())}, -1, Qt::CopyAction));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
  EXPECT_EQ(controller.tasks()->lastSummary().failed, 1);
  EXPECT_TRUE(QDir(src.path()).exists());
}

TEST(FileDrop, QueuedDestinationIsIndependentOfLaterNavigation) {
  QTemporaryDir src(files_test::fixturePattern("drop-queue-source"));
  QTemporaryDir dst(files_test::fixturePattern("drop-queue-destination"));
  files_test::writeFile(src, "same", "new");
  files_test::writeFile(src, "queued", "queued");
  files_test::writeFile(dst, "same", "old");
  ASSERT_TRUE(QDir(dst.path()).mkdir("child"));
  DirectoryController controller;
  controller.open(dst.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  ASSERT_TRUE(controller.submitDrop({QUrl::fromLocalFile(src.filePath("same"))}, -1, Qt::CopyAction));
  // Queue synchronously before the worker can deliver its conflict prompt.
  ASSERT_TRUE(controller.submitDrop({QUrl::fromLocalFile(src.filePath("queued"))}, rowNamed(controller, "child"),
                                    Qt::CopyAction));
  controller.open(src.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.tasks()->hasPrompt(); }));
  controller.tasks()->resolveConflict(TaskManager::ConflictResolution::Skip, controller.tasks()->promptId());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.tasks()->busy(); }));
  EXPECT_TRUE(QFile::exists(dst.filePath("child/queued")));
  EXPECT_TRUE(QFile::exists(src.filePath("queued")));
}
