#include "directory_controller.h"
#include "directory_fixtures.h"
#include "engine_setup.h"
#include "icon_fallbacks.h"
#include "image_policy.h"
#include "preview_fixtures.h"
#include "preview_service_test_access.h"
#include "quick_look_presentation_model_test_access.h"
#include "size_format.h"

#include <QAccessible>
#include <QCoreApplication>
#include <QDesktopServices>
#include <QGuiApplication>
#include <QIcon>
#include <QImage>
#include <QQmlApplicationEngine>
#include <QQmlComponent>
#include <QQmlContext>
#include <QQmlEngine>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickStyle>
#include <QQuickWindow>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTest>
#include <QThread>
#include <QWheelEvent>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <gtest/gtest.h>
#include <iostream>
#include <memory>

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
  initializeFilesEngine(engine);
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
  initializeFilesEngine(engine);
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
  auto* gutter = row->findChild<QQuickItem*>("lineNumberGutterField");
  auto* gutterHeader = window->findChild<QQuickItem*>("lineNumberGutterHeader");
  auto* listing = list->parentItem();
  ASSERT_NE(gutter, nullptr);
  ASSERT_NE(gutterHeader, nullptr);
  ASSERT_NE(icon, nullptr);
  ASSERT_NE(nameHeader, nullptr);
  ASSERT_NE(name, nullptr);
  ASSERT_NE(size, nullptr);
  ASSERT_NE(modified, nullptr);
  ASSERT_NE(sizeHeader, nullptr);
  ASSERT_NE(modifiedHeader, nullptr);
  ASSERT_NE(breadcrumb, nullptr);
  // line-number-gutter keeps the showSize/showModified formulas unchanged (its non-goal 8), so the
  // gutter narrows Name below them; 740 px (not 700) is the narrowest width that still hides Size
  // while leaving Name its 120 px.
  for (const int width : {1000, 850, 740, 1000}) {
    window->resize(width, 400);
    ASSERT_TRUE(QTest::qWaitFor([&] {
      return name->width() >= 120 && size->isVisible() == (width != 740) && modified->isVisible() == (width == 1000);
    }));
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
    // line-number-gutter REQ-F-009/010: a blank header spacer as wide as the gutter keeps every column aligned.
    const auto gutterWidth = listing->property("lineNumberGutterWidth").toReal();
    EXPECT_EQ(gutterHeader->width(), gutterWidth) << width;
    EXPECT_EQ(gutter->width(), gutterWidth) << width;
    EXPECT_NEAR(gutter->mapToScene(QPointF()).x(), list->mapToScene(QPointF()).x(), 1) << width;
    EXPECT_NEAR(gutterHeader->mapToScene(QPointF()).x(), gutter->mapToScene(QPointF()).x(), 1) << width;
    EXPECT_EQ(sizeHeader->isVisible(), size->isVisible()) << width;
    EXPECT_EQ(modifiedHeader->isVisible(), modified->isVisible()) << width;
    if (size->isVisible()) {
      EXPECT_NEAR(sizeHeader->mapToScene(QPointF()).x(), size->mapToScene(QPointF()).x(), 1) << width;
    }
    if (modified->isVisible()) {
      EXPECT_NEAR(modifiedHeader->mapToScene(QPointF()).x(), modified->mapToScene(QPointF()).x(), 1) << width;
    }
    // line-number-gutter REQ-F-015 (replacing main-view-icons REQ-NF-002's icon-column anchor): the row
    // now starts with the gutter, so the breadcrumb aligns with the list view's left edge instead —
    // a fixed offset that doesn't move when the gutter widens.
    EXPECT_NEAR(breadcrumb->mapToScene(QPointF()).x(), list->mapToScene(QPointF()).x(), 1) << width;
  }
}

TEST(Files, WindowAnchorOnlySeparatorsOccupyAndPaint) {
  QTemporaryDir dir(files_test::fixturePattern("separator-geometry"));
  ASSERT_FALSE(files_test::writeFile(dir, "example.txt").isEmpty());
  DirectoryController controller;
  QQmlApplicationEngine engine;
  initializeFilesEngine(engine);
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  QCoreApplication::processEvents();
  const QImage rendered = window->grabWindow();
  ASSERT_FALSE(rendered.isNull());
  const qreal dpr = window->effectiveDevicePixelRatio();

  const auto expectSeparator = [window, &rendered, dpr](const char* name) {
    auto* separator = window->findChild<QQuickItem*>(name);
    ASSERT_NE(separator, nullptr) << name;
    auto* line = separator->findChild<QQuickItem*>("separatorLine");
    ASSERT_NE(line, nullptr) << name;
    EXPECT_GT(separator->width(), 0) << name;
    EXPECT_GT(separator->height(), 0) << name;
    EXPECT_TRUE(separator->isVisible()) << name;
    EXPECT_TRUE(line->isVisible()) << name;
    EXPECT_GT(line->width(), 0) << name;
    EXPECT_GT(line->height(), 0) << name;
    const QPointF center = line->mapToScene(QPointF(line->width() / 2, line->height() / 2));
    const QPoint pixel(qFloor(center.x() * dpr), qFloor(center.y() * dpr));
    ASSERT_TRUE(rendered.rect().contains(pixel)) << name;
    EXPECT_EQ(rendered.pixelColor(pixel), separator->property("color").value<QColor>()) << name;
  };
  for (const auto* name : {"headerDivider", "sidebarDivider", "columnHeaderDivider", "sizeColumnDivider",
                           "modifiedColumnDivider", "footerDivider"}) {
    expectSeparator(name);
  }
}

namespace {
struct LoadedWindow {
  std::unique_ptr<QQmlApplicationEngine> engine = [] {
    auto engine = std::make_unique<QQmlApplicationEngine>();
    initializeFilesEngine(*engine);
    return engine;
  }();
  QQuickWindow* window = nullptr;
  QQuickItem* list = nullptr;
  QQuickItem* listing = nullptr;
};
LoadedWindow loadActiveWindow(DirectoryController& controller, const QString& path) {
  LoadedWindow loaded;
  loaded.engine->setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  loaded.engine->loadFromModule("HolonightFiles", "Main");
  if (loaded.engine->rootObjects().size() != 1) {
    return loaded;
  }
  loaded.window = qobject_cast<QQuickWindow*>(loaded.engine->rootObjects().first());
  loaded.window->requestActivate();
  if (!QTest::qWaitForWindowActive(loaded.window)) {
    return loaded;
  }
  controller.open(path);
  if (QTest::qWaitFor([&] { return !controller.scanning(); })) {
    loaded.list = loaded.window->findChild<QQuickItem*>("directoryListView");
    loaded.listing = loaded.list != nullptr ? loaded.list->parentItem() : nullptr;
  }
  return loaded;
}

// Visible gutter labels keyed by row; delegates hang off the ListView's contentItem only visually.
QMap<int, QQuickItem*> gutterLabels(QQuickItem* list) {
  QMap<int, QQuickItem*> labels;
  for (auto* delegate : list->property("contentItem").value<QQuickItem*>()->childItems()) {
    if (delegate->objectName() == "directoryEntryDelegate" && delegate->isVisible()) {
      if (auto* label = delegate->findChild<QQuickItem*>("lineNumberGutterField")) {
        labels.insert(delegate->property("index").toInt(), label);
      }
    }
  }
  return labels;
}

QVariant evaluateInContext(QQuickItem* item, const QString& expression) {
  QQmlExpression evaluated(qmlContext(item), item, expression);
  return evaluated.evaluate();
}

// line-number-gutter REQ-F-002: absolute 1-based number on the cursor row, relative distance elsewhere.
QString expectedGutterText(int row, int cursor) {
  return QString::number(row == cursor ? row + 1 : std::abs(row - cursor));
}

// Every visible label must match its current proxy index and cursorRow, in text and palette colour.
::testing::AssertionResult gutterMatchesCursor(QQuickItem* list, const DirectoryController& controller) {
  const auto labels = gutterLabels(list);
  if (labels.isEmpty()) {
    return ::testing::AssertionFailure() << "no visible gutter labels";
  }
  const int cursor = controller.cursorRow();
  for (auto it = labels.cbegin(); it != labels.cend(); ++it) {
    const auto text = it.value()->property("text").toString();
    const auto expectedColor = evaluateInContext(
        it.value(), it.key() == cursor ? "HoloniightPalette.accentViolet" : "HoloniightPalette.textMuted");
    if (text != expectedGutterText(it.key(), cursor) || it.value()->property("color") != expectedColor) {
      return ::testing::AssertionFailure()
             << "row " << it.key() << " shows " << text.toStdString() << " for cursor " << cursor;
    }
  }
  return ::testing::AssertionSuccess();
}
}  // namespace

TEST(Files, LineNumberGutterHybridNumberingFollowsCursorSortAndFilter) {
  QTemporaryDir dir(files_test::fixturePattern("gutter"));
  ASSERT_TRUE(dir.isValid());
  files_test::populateEntries(dir, 5);
  ASSERT_FALSE(files_test::writeFile(dir, ".hidden").isEmpty());
  DirectoryController controller;
  auto loaded = loadActiveWindow(controller, dir.path());
  ASSERT_NE(loaded.list, nullptr);
  auto* window = loaded.window;
  auto* list = loaded.list;
  ASSERT_TRUE(QTest::qWaitFor([&] { return gutterLabels(list).size() == 5; }));

  QTest::keyClick(window, Qt::Key_2);
  QTest::keyClick(window, Qt::Key_J);
  ASSERT_EQ(controller.cursorRow(), 2);
  auto labels = gutterLabels(list);
  QStringList texts;
  for (auto* label : labels) {
    texts.append(label->property("text").toString());
  }
  EXPECT_EQ(texts, (QStringList{"2", "1", "3", "1", "2"}));
  EXPECT_TRUE(gutterMatchesCursor(list, controller));
  for (auto* label : labels) {
    EXPECT_EQ(label->property("font").value<QFont>().family(),
              evaluateInContext(label, "HolonightTheme.monospaceFont").toString());
    EXPECT_EQ(label->property("horizontalAlignment").toInt(), Qt::AlignRight);
    EXPECT_EQ(label->width(), loaded.listing->property("lineNumberGutterWidth").toReal());
    // REQ-F-005: flush at the row's left edge; REQ-NF-004: a visual aid only.
    EXPECT_EQ(label->mapToItem(label->parentItem()->parentItem(), QPointF()).x(), 0);
    EXPECT_TRUE(QQmlProperty(label, QStringLiteral("Accessible.ignored"), qmlContext(label)).read().toBool());
  }

  // REQ-F-006 / REQ-NF-002: j/k rebinds the same label instances instead of recreating delegates.
  auto* rowZero = labels.value(0);
  const auto rowZeroText = rowZero->property("text").toString();
  QTest::keyClick(window, Qt::Key_J);
  ASSERT_EQ(controller.cursorRow(), 3);
  EXPECT_TRUE(gutterMatchesCursor(list, controller));
  EXPECT_EQ(gutterLabels(list).value(0), rowZero);
  EXPECT_NE(rowZero->property("text").toString(), rowZeroText);
  QTest::keyClick(window, Qt::Key_K);
  QTest::keyClick(window, Qt::Key_K);
  ASSERT_EQ(controller.cursorRow(), 1);
  EXPECT_TRUE(gutterMatchesCursor(list, controller));

  // VISUAL-selected rows keep their relative, muted numbers (non-goal 7).
  QTest::keyClick(window, Qt::Key_V);
  QTest::keyClick(window, Qt::Key_J);
  ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Visual);
  ASSERT_TRUE(controller.vim()->isRowSelected(1));
  EXPECT_TRUE(gutterMatchesCursor(list, controller));
  QTest::keyClick(window, Qt::Key_Escape);

  // REQ-F-007: hidden-files toggle and sort-order changes renumber every visible row.
  QTest::keyClick(window, Qt::Key_Period);
  ASSERT_TRUE(QTest::qWaitFor([&] { return gutterLabels(list).size() == 6; }));
  EXPECT_TRUE(gutterMatchesCursor(list, controller));
  QTest::keyClick(window, Qt::Key_S);
  ASSERT_TRUE(controller.listing()->sortDescending());
  EXPECT_TRUE(QTest::qWaitFor([&] { return gutterMatchesCursor(list, controller); }));
  QTest::keyClick(window, Qt::Key_S);
  QTest::keyClick(window, Qt::Key_Period);
  ASSERT_TRUE(QTest::qWaitFor([&] { return gutterLabels(list).size() == 5; }));
  EXPECT_TRUE(gutterMatchesCursor(list, controller));

  // REQ-F-008: a watcher refresh adding an entry above the cursor renumbers the rows.
  QTest::keyClick(window, 'G', Qt::ShiftModifier);
  ASSERT_EQ(controller.cursorRow(), 4);
  ASSERT_FALSE(files_test::writeFile(dir, "a-first.txt").isEmpty());
  ASSERT_TRUE(QTest::qWaitFor([&] { return gutterLabels(list).size() == 6; }));
  EXPECT_TRUE(QTest::qWaitFor([&] { return gutterMatchesCursor(list, controller); }));
}

TEST(Files, LineNumberGutterWidthGrowsWithDigitsButBreadcrumbStays) {
  QTemporaryDir dir(files_test::fixturePattern("gutter-width"));
  ASSERT_TRUE(dir.isValid());
  files_test::populateEntries(dir, 999);
  DirectoryController controller;
  auto loaded = loadActiveWindow(controller, dir.path());
  ASSERT_NE(loaded.list, nullptr);
  auto* listing = loaded.listing;

  // REQ-C-004: string-length digits with a three-digit minimum.
  const QList<std::pair<int, int>> digits{{0, 3},   {1, 3},    {9, 3},    {10, 3},    {99, 3},   {100, 3},
                                          {999, 3}, {1000, 4}, {9999, 4}, {10000, 5}, {99999, 5}};
  for (const auto& [rows, expected] : digits) {
    int actual = 0;
    ASSERT_TRUE(
        QMetaObject::invokeMethod(listing, "lineNumberGutterDigitsFor", Q_RETURN_ARG(int, actual), Q_ARG(int, rows)));
    EXPECT_EQ(actual, expected) << rows;
  }

  // REQ-F-004 / REQ-NF-001: measured off a hidden Code-role label of nines, plus 8 px either side.
  ASSERT_TRUE(QTest::qWaitFor([&] { return loaded.list->property("count").toInt() == 999; }));
  QQuickItem* metric = nullptr;
  for (auto* child : listing->childItems()) {
    if (!child->isVisible() && child->property("rawText").toString().startsWith("999")) {
      metric = child;
    }
  }
  ASSERT_NE(metric, nullptr);
  EXPECT_EQ(metric->property("rawText").toString(), "999");
  const auto narrow = listing->property("lineNumberGutterWidth").toReal();
  EXPECT_EQ(narrow, std::ceil(metric->implicitWidth()) + 16);
  auto* breadcrumb = loaded.window->findChild<QQuickItem*>("breadcrumbLabel");
  ASSERT_NE(breadcrumb, nullptr);
  const auto breadcrumbX = breadcrumb->mapToScene(QPointF()).x();

  // One real crossing: the 1000th entry arrives through the watcher.
  ASSERT_FALSE(files_test::writeFile(dir, "entry-99999.txt").isEmpty());
  ASSERT_TRUE(QTest::qWaitFor([&] { return loaded.list->property("count").toInt() == 1000; }));
  EXPECT_EQ(metric->property("rawText").toString(), "9999");
  EXPECT_TRUE(QTest::qWaitFor([&] { return listing->property("lineNumberGutterWidth").toReal() > narrow; }));
  EXPECT_EQ(listing->property("lineNumberGutterWidth").toReal(), std::ceil(metric->implicitWidth()) + 16);
  auto* header = loaded.window->findChild<QQuickItem*>("lineNumberGutterHeader");
  ASSERT_NE(header, nullptr);
  EXPECT_TRUE(QTest::qWaitFor([&] { return header->width() > narrow; }));
  EXPECT_NEAR(breadcrumb->mapToScene(QPointF()).x(), breadcrumbX, 1);
}

TEST(Files, LineNumberGutterNumbersPlaceholderRowsAndClearsForInlineEditor) {
  QTemporaryDir dir(files_test::fixturePattern("gutter-placeholder"));
  ASSERT_TRUE(dir.isValid());
  files_test::populateEntries(dir, 4);
  DirectoryController controller;
  auto loaded = loadActiveWindow(controller, dir.path());
  ASSERT_NE(loaded.list, nullptr);
  auto* window = loaded.window;
  auto* list = loaded.list;
  ASSERT_TRUE(QTest::qWaitFor([&] { return gutterLabels(list).size() == 4; }));
  QTest::keyClick(window, Qt::Key_J);
  ASSERT_EQ(controller.cursorRow(), 1);

  // REQ-F-012: the o placeholder (below the cursor, which moves onto it) is numbered like any row.
  QTest::keyClick(window, Qt::Key_O);
  ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
  ASSERT_TRUE(QTest::qWaitFor([&] { return gutterLabels(list).size() == 5; }));
  const int placeholderRow = controller.vim()->editingRow();
  EXPECT_EQ(placeholderRow, 2);
  EXPECT_TRUE(gutterMatchesCursor(list, controller));
  auto* placeholderLabel = gutterLabels(list).value(placeholderRow);
  ASSERT_NE(placeholderLabel, nullptr);
  EXPECT_EQ(placeholderLabel->property("text").toString(), expectedGutterText(placeholderRow, controller.cursorRow()));

  // REQ-F-011: the inline editor starts at or after the gutter's right edge.
  auto* editor = window->activeFocusItem();
  ASSERT_NE(editor, nullptr);
  ASSERT_EQ(editor->objectName(), "inlineNameEditor");
  auto* row = editor->parentItem();
  EXPECT_GE(editor->mapToItem(row, QPointF()).x(),
            placeholderLabel->mapToItem(row, QPointF()).x() + placeholderLabel->width());

  // Cancelling removes the placeholder and renumbers the remaining rows.
  QTest::keyClick(window, Qt::Key_Escape);
  ASSERT_TRUE(QTest::qWaitFor([&] { return gutterLabels(list).size() == 4; }));
  EXPECT_TRUE(QTest::qWaitFor([&] { return gutterMatchesCursor(list, controller); }));

  // Committing re-sorts the new entry; every row still matches its proxy index.
  QTest::keyClick(window, 'O', Qt::ShiftModifier);
  ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
  editor = window->activeFocusItem();
  ASSERT_NE(editor, nullptr);
  editor->setProperty("text", "zz-last.txt");
  QTest::keyClick(window, Qt::Key_Return);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning() && gutterLabels(list).size() == 5; }));
  EXPECT_TRUE(QFile::exists(dir.filePath("zz-last.txt")));
  EXPECT_TRUE(QTest::qWaitFor([&] { return gutterMatchesCursor(list, controller); }));
}

TEST(Files, LineNumberGutterHeaderStaysForEmptyAndUnreadableDirectories) {
  QTemporaryDir dir(files_test::fixturePattern("gutter-empty"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_TRUE(QDir(dir.path()).mkdir("empty"));
  ASSERT_TRUE(QDir(dir.path()).mkdir("blocked"));
  files_test::writeFile(dir, "blocked/secret.txt");
  DirectoryController controller;
  auto loaded = loadActiveWindow(controller, dir.path());
  ASSERT_NE(loaded.list, nullptr);
  auto* list = loaded.list;
  auto* header = loaded.window->findChild<QQuickItem*>("lineNumberGutterHeader");
  ASSERT_NE(header, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !gutterLabels(list).isEmpty(); }));

  // REQ-F-013: an empty directory has no gutter numbers but keeps the header spacer.
  controller.open(dir.filePath("empty"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  EXPECT_TRUE(QTest::qWaitFor([&] { return gutterLabels(list).isEmpty(); }));
  EXPECT_EQ(list->property("count").toInt(), 0);
  EXPECT_TRUE(header->isVisible());
  EXPECT_EQ(header->width(), loaded.listing->property("lineNumberGutterWidth").toReal());

  // REQ-F-014: likewise for a permission-denied listing error.
  if (files_test::runningAsRoot()) {
    GTEST_SKIP() << "root bypasses directory permission bits";
  }
  const auto blocked = dir.filePath("blocked");
  ASSERT_EQ(::chmod(blocked.toLocal8Bit().constData(), 0), 0);
  const auto restore = qScopeGuard([&] { ::chmod(blocked.toLocal8Bit().constData(), 0755); });
  controller.open(blocked);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  ASSERT_FALSE(controller.directoryError().isEmpty());
  EXPECT_TRUE(QTest::qWaitFor([&] { return gutterLabels(list).isEmpty(); }));
  EXPECT_TRUE(header->isVisible());
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
    initializeFilesEngine(engine);
    engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
    engine.loadFromModule("HolonightFiles", "Main");
    ASSERT_EQ(engine.rootObjects().size(), 1);
    // Shared engine setup registers image://icon before loading application QML.
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
  initializeFilesEngine(bareEngine);
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
  auto* failedIcons = bareEngine.singletonInstance<IconFallbacks*>("HolonightFiles", "IconFallbacks");
  ASSERT_NE(failedIcons, nullptr);
  EXPECT_TRUE(failedIcons->isUnresolved("folder/inode-directory"));
  // Recreate delegates after the failure: the source binding skips the provider entirely.
  bareController.open(dir.filePath("a-folder"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !bareController.scanning(); }));
  bareController.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] {
    const auto icons = rowIcons(bareList, 0);
    return !bareController.scanning() && icons.fallback != nullptr && icons.fallback->isVisible();
  }));
  EXPECT_TRUE(rowIcons(bareList, 0).theme->property("source").toUrl().isEmpty());
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
  initializeFilesEngine(engine);
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
  initializeFilesEngine(engine);
  QQmlComponent component(&engine);
  component.setData("import QtQuick.Controls\nButton {}", QUrl());
  const std::unique_ptr<QObject> button(component.create());
  ASSERT_NE(button, nullptr) << component.errorString().toStdString();
  EXPECT_EQ(QQuickStyle::name(), QStringLiteral("Holonight"));
  EXPECT_TRUE(button->property("foregroundColor").isValid());
}

int main(int argc, char* argv[]) {
  configurePreviewImageLimits();
  const auto testStyle = qgetenv("FILES_TEST_STYLE");
  if (testStyle.isEmpty()) {
    qunsetenv("QT_QUICK_CONTROLS_STYLE");
  } else {
    qputenv("QT_QUICK_CONTROLS_STYLE", testStyle);
  }
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
  initializeFilesEngine(engine);
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
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->quickLookEligible(); }, 5000));
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
  // Quick Look is pinned: j/k move the viewer's current line (clamped on a one-line file), never the cursor.
  QTest::keyClick(window, Qt::Key_J);
  EXPECT_EQ(controller.cursorRow(), 0);
  QTest::keyClick(window, Qt::Key_K);
  EXPECT_EQ(controller.cursorRow(), 0);
  EXPECT_TRUE(controller.quickLookOpen());
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
  initializeFilesEngine(engine);
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
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->quickLookEligible(); }, 5000));
  QTest::keyClick(window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.quickLookOpen(); }));
  QTest::qWait(250);
  // Quick Look is pinned, so j no longer steps to the next image while it is open: close, step, reopen.
  QTest::keyClick(window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.quickLookOpen(); }));
  elapsed.restart();
  QTest::keyClick(window, Qt::Key_J);
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return controller.preview()->hasImage() && controller.preview()->quickLookEligible(); }));
  QTest::keyClick(window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.quickLookOpen(); }));
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
  initializeFilesEngine(engine);
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
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->quickLookEligible(); }, 5000));
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
  initializeFilesEngine(engine);
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
  QTest::keyClick(window, Qt::Key_V);
  ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Visual);
  QTest::keyClick(window, Qt::Key_Escape);
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
  EXPECT_EQ(window->visibility(), QWindow::FullScreen);
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
  initializeFilesEngine(engine);
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
  initializeFilesEngine(engine);
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
    initializeFilesEngine(engine);
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
      ASSERT_TRUE(QTest::qWaitFor([&] { return controller.preview()->quickLookEligible(); }, 5000));
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
  initializeFilesEngine(engine);
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
  initializeFilesEngine(engine);
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
  initializeFilesEngine(engine);
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
  initializeFilesEngine(engine);
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
  QuickLookHarness() { initializeFilesEngine(engine); }
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
  // Quick Look only opens for images and text/plain, whose MIME the worker reports asynchronously.
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.controller.preview()->quickLookEligible(); }, 5000));
  QTest::keyClick(harness.window, Qt::Key_Space);
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.popup->property("opened").toBool(); }));
}

void closeQuickLook(QuickLookHarness& harness) {
  if (!harness.controller.quickLookOpen()) {
    return;
  }
  QTest::keyClick(harness.window, Qt::Key_Escape);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.popup->property("visible").toBool(); }));
}

// Quick Look is pinned to one file, so showing another means closing, moving the listing cursor and reopening.
void showQuickLookOn(QuickLookHarness& harness, const QString& name) {
  ASSERT_NO_FATAL_FAILURE(closeQuickLook(harness));
  harness.controller.handleKey(QStringLiteral("g"));  // stepPreviewTo only moves forward: start from the top
  harness.controller.handleKey(QStringLiteral("g"));
  ASSERT_TRUE(stepPreviewTo(harness.controller, name));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
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

// Delegates are visual children of a ListView's content item, not QObject children.
QList<QQuickItem*> descendantsNamed(QQuickItem* root, const QString& name) {
  QList<QQuickItem*> found;
  QList<QQuickItem*> pending{root};
  while (!pending.isEmpty()) {
    auto* item = pending.takeLast();
    for (auto* child : item->childItems()) {
      if (child->objectName() == name) {
        found.append(child);
      }
      pending.append(child);
    }
  }
  return found;
}

QQuickItem* quickLookViewer(const QuickLookHarness& harness) {
  return harness.window->findChild<QQuickItem*>("quickLookText");
}

QQuickItem* viewerContent(const QuickLookHarness& harness) {
  auto* viewer = quickLookViewer(harness);
  return viewer != nullptr ? viewer->property("contentItem").value<QQuickItem*>() : nullptr;
}

// The delegate whose highlight is visible, or nullptr.
QQuickItem* highlightedRow(const QuickLookHarness& harness) {
  auto* content = viewerContent(harness);
  if (content == nullptr) {
    return nullptr;
  }
  for (auto* row : descendantsNamed(content, "quickLookLine")) {
    for (auto* mark : descendantsNamed(row, "quickLookCurrentLine")) {
      if (mark->isVisible()) {
        return row;
      }
    }
  }
  return nullptr;
}

QString rowNumber(QQuickItem* row) {
  const auto numbers = descendantsNamed(row, "quickLookLineNumbers");
  return numbers.isEmpty() ? QString() : numbers.first()->property("text").toString();
}

// True when the row lies fully inside the viewer's visible area.
bool rowInViewport(const QuickLookHarness& harness, QQuickItem* row) {
  auto* viewer = quickLookViewer(harness);
  if (viewer == nullptr || row == nullptr) {
    return false;
  }
  const auto top = row->mapToItem(viewer, QPointF(0, 0)).y();
  return top >= -0.5 && top + row->height() <= viewer->height() + 0.5;
}

QColor quickLookColor(const QuickLookHarness& harness, const char* objectName) {
  auto* item = harness.window->findChild<QQuickItem*>(objectName);
  return item != nullptr ? item->property("color").value<QColor>() : QColor();
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
  ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, "01-image.jpg"));
  auto* card = harness.window->findChild<QQuickItem*>("quickLookCard");
  ASSERT_NE(card, nullptr);
  const auto textDisabled = paletteColor(harness.engine, "textDisabled");
  const auto textMuted = paletteColor(harness.engine, "textMuted");
  ASSERT_TRUE(textDisabled.isValid());
  EXPECT_NE(textDisabled, textMuted);
  for (const auto* name : {"01-image.jpg", "02-notes.txt"}) {
    SCOPED_TRACE(name);
    ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, QString::fromLatin1(name)));
    QTest::qWait(20);
    expectCardWithinBounds(harness);
    EXPECT_GT(card->property("radius").toReal(), 0);
    EXPECT_NEAR(card->width(), popupSize(harness).width(), 1);
    EXPECT_NEAR(card->height(), popupSize(harness).height(), 1);
    EXPECT_TRUE(harness.window->findChild<QQuickItem*>("quickLookHint")->isVisible());
    EXPECT_EQ(harness.window->findChild<QQuickItem*>("quickLookCloseKeys")->property("accessibleText").toString(),
              QStringLiteral("Space or Esc"));
    EXPECT_EQ(quickLookText(harness, "quickLookCloseLabel"), QStringLiteral("Close"));
    EXPECT_EQ(quickLookColor(harness, "quickLookCloseLabel"), textDisabled);
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
  ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, longName));
  ASSERT_TRUE(QTest::qWaitFor([&] { return label->property("truncated").toBool(); }));
  expectCentered();
  ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, "b.txt"));
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
    showQuickLookOn(harness, name);
    return !::testing::Test::HasFatalFailure() &&
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

// quick-look-redesign REQ-F-009/010.
TEST(Files, QuickLookTextFrameFillsPreviewBoundsWithMonospaceView) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-text"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeLargeText(dir, "01-large.txt");
  files_test::writeSmallText(dir, "02-small.txt");
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, "01-large.txt"));
  auto* text = harness.window->findChild<QQuickItem*>("quickLookText");
  ASSERT_NE(text, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return text->isVisible(); }));
  // ListView -> rounded surface -> preview frame.
  ASSERT_TRUE(text->inherits("QQuickListView"));
  auto* frame = text->parentItem() != nullptr ? text->parentItem()->parentItem() : nullptr;
  ASSERT_NE(frame, nullptr);
  const auto preview = quickLookPreviewBounds(harness);
  EXPECT_NEAR(frame->width(), preview.width(), 1);
  EXPECT_NEAR(frame->height(), preview.height(), 1);
  EXPECT_FALSE(text->property("interactive").isNull());
  EXPECT_EQ(text->property("flickableDirection").toInt(), 2);  // Flickable.VerticalFlick: no horizontal scrolling
  auto* theme = harness.engine.singletonInstance<QObject*>("Holonight.Core", "HolonightTheme");
  ASSERT_NE(theme, nullptr);
  auto* content = text->property("contentItem").value<QQuickItem*>();
  ASSERT_NE(content, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !descendantsNamed(content, "quickLookLine").isEmpty(); }));
  const auto rows = descendantsNamed(content, "quickLookLine");
  const auto numbers = descendantsNamed(content, "quickLookLineNumbers");
  const auto bodies = descendantsNamed(content, "quickLookLineText");
  ASSERT_FALSE(numbers.isEmpty());
  ASSERT_EQ(numbers.size(), bodies.size());
  for (auto* body : bodies) {  // monospace, one row per line, never wrapped
    EXPECT_EQ(body->property("font").value<QFont>().family(), theme->property("monospaceFont").toString());
    EXPECT_EQ(body->property("wrapMode").toInt(), 0);  // Text.NoWrap
  }
  // The gutter shows 1..N and exactly the current row (line 1 on open) carries the highlight.
  QStringList shown;
  int highlighted = 0;
  for (auto* number : numbers) {
    shown.append(number->property("text").toString());
  }
  for (auto* row : rows) {
    for (auto* mark : descendantsNamed(row, "quickLookCurrentLine")) {
      highlighted += mark->isVisible() ? 1 : 0;
    }
  }
  EXPECT_TRUE(shown.contains(QStringLiteral("1")));
  EXPECT_EQ(highlighted, 1);
  EXPECT_EQ(harness.controller.preview()->currentLineIndex(), 0);
  expectCardWithinBounds(harness);

  ASSERT_TRUE(harness.controller.preview()->textTruncated());
  const auto largeSize = quickLookText(harness, "previewSizeValue");
  EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), largeSize + QStringLiteral(" · truncated"));
  ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, "02-small.txt"));
  ASSERT_FALSE(harness.controller.preview()->textTruncated());
  EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), quickLookText(harness, "previewSizeValue"));
}

void pressKeyTimes(QuickLookHarness& harness, Qt::Key key, int times) {
  for (int i = 0; i < times; ++i) {
    QTest::keyClick(harness.window, key);
  }
}

// quick-look-text-viewer REQ-F-003/012: the highlight follows the current line and the viewport follows both ways.
TEST(Files, QuickLookViewerHighlightsAndScrollsToTheCurrentLine) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-scroll"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeNumberedLines(dir, "a.txt", 200).isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path(), QSize(900, 500)));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  auto* viewer = quickLookViewer(harness);
  ASSERT_NE(viewer, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return highlightedRow(harness) != nullptr; }));
  EXPECT_EQ(rowNumber(highlightedRow(harness)), QStringLiteral("1"));
  EXPECT_EQ(viewer->property("contentY").toReal(), 0);
  // Only a screenful of rows fits, so line 50 is well below the first viewport.
  ASSERT_LT(viewer->height(), 200 * descendantsNamed(viewerContent(harness), "quickLookLine").first()->height());

  pressKeyTimes(harness, Qt::Key_J, 49);
  EXPECT_EQ(harness.controller.preview()->currentLineIndex(), 49);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    auto* row = highlightedRow(harness);
    return row != nullptr && rowNumber(row) == QStringLiteral("50") && rowInViewport(harness, row);
  }));
  EXPECT_GT(viewer->property("contentY").toReal(), 0);

  pressKeyTimes(harness, Qt::Key_K, 45);
  EXPECT_EQ(harness.controller.preview()->currentLineIndex(), 4);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    auto* row = highlightedRow(harness);
    return row != nullptr && rowNumber(row) == QStringLiteral("5") && rowInViewport(harness, row);
  }));
  // Arrow keys behave like j/k.
  QTest::keyClick(harness.window, Qt::Key_Down);
  EXPECT_EQ(harness.controller.preview()->currentLineIndex(), 5);
  QTest::keyClick(harness.window, Qt::Key_Up);
  EXPECT_EQ(harness.controller.preview()->currentLineIndex(), 4);
  EXPECT_EQ(harness.controller.cursorRow(), 0);
}

// quick-look-text-viewer REQ-F-013. The wheel event is synthesized in-process and delivered straight to the window;
// no native pointer is moved (AGENTS.md). A real wheel/pointer check is manual.
TEST(Files, QuickLookWheelScrollsTheViewportWithoutChangingTheCurrentLine) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-wheel"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeNumberedLines(dir, "a.txt", 200).isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path(), QSize(900, 500)));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  auto* viewer = quickLookViewer(harness);
  ASSERT_NE(viewer, nullptr);
  pressKeyTimes(harness, Qt::Key_J, 30);
  ASSERT_EQ(harness.controller.preview()->currentLineIndex(), 30);
  ASSERT_TRUE(QTest::qWaitFor([&] { return rowInViewport(harness, highlightedRow(harness)); }));
  const auto local = viewer->mapToScene(QPointF(viewer->width() / 2, viewer->height() / 2));
  const auto wheel = [&](int delta) {
    QWheelEvent event(local, harness.window->mapToGlobal(local.toPoint()), QPoint(), QPoint(0, delta), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    static ulong timestamp = 1000;
    timestamp += 200;  // Flickable ignores wheel events whose timestamp does not advance
    event.setTimestamp(timestamp);
    QCoreApplication::sendEvent(harness.window, &event);
    QTest::qWait(30);
  };
  // Wheel scrolling can keep moving briefly (kinetic flick): sample once contentY has stopped changing.
  const auto settledContentY = [&] {
    qreal last = -1;
    [[maybe_unused]] const bool stopped = QTest::qWaitFor(
        [&] {
          const auto now = viewer->property("contentY").toReal();
          const bool still = qFuzzyCompare(now + 1, last + 1);
          last = now;
          QTest::qWait(60);
          return still;
        },
        3000);
    return viewer->property("contentY").toReal();
  };

  const auto before = settledContentY();
  for (int i = 0; i < 3; ++i) {
    wheel(-120);  // wheel down
  }
  const auto scrolledDown = settledContentY();
  EXPECT_GT(scrolledDown, before);
  EXPECT_EQ(harness.controller.preview()->currentLineIndex(), 30);
  ASSERT_NE(highlightedRow(harness), nullptr) << "the current row is still highlighted wherever it scrolled";

  for (int i = 0; i < 3; ++i) {
    wheel(120);  // wheel up
  }
  EXPECT_LT(settledContentY(), scrolledDown);
  EXPECT_EQ(harness.controller.preview()->currentLineIndex(), 30);
  EXPECT_EQ(harness.controller.cursorRow(), 0);
}

// quick-look-text-viewer REQ-F-004/017: one row per line, clipped at the right edge, no horizontal scrolling.
TEST(Files, QuickLookLongLineIsClippedWithoutHorizontalScrolling) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-clip"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeBytes(dir, "a.txt", QByteArray(400, 'x') + "\nshort\n").isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path(), QSize(700, 400)));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  auto* viewer = quickLookViewer(harness);
  ASSERT_NE(viewer, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] { return descendantsNamed(viewerContent(harness), "quickLookLine").size() == 2; }));
  auto rows = descendantsNamed(viewerContent(harness), "quickLookLine");
  std::ranges::sort(rows, [](const QQuickItem* upper, const QQuickItem* lower) { return upper->y() < lower->y(); });
  EXPECT_NEAR(rows[0]->height(), rows[1]->height(), 0.01);  // the long line still occupies exactly one row
  EXPECT_NEAR(rows[1]->y() - rows[0]->y(), rows[0]->height(), 0.01);
  auto* const body = descendantsNamed(rows[0], "quickLookLineText").first();
  EXPECT_LE(body->x() + body->width(), viewer->width() + 0.5);        // clipped to the viewport width
  EXPECT_GT(body->property("contentWidth").toReal(), body->width());  // the text really is wider than the viewport
  EXPECT_EQ(rowNumber(rows[0]), QStringLiteral("1"));                 // gutter stays visible
  EXPECT_LE(viewer->property("contentWidth").toReal(), viewer->width() + 0.5);
  const auto wheelPos = viewer->mapToScene(QPointF(viewer->width() / 2, viewer->height() / 2));
  QWheelEvent horizontal(wheelPos, harness.window->mapToGlobal(wheelPos.toPoint()), QPoint(), QPoint(-240, 0),
                         Qt::NoButton, Qt::NoModifier, Qt::NoScrollPhase, false);
  horizontal.setTimestamp(9000);
  QCoreApplication::sendEvent(harness.window, &horizontal);
  QTest::qWait(30);
  EXPECT_EQ(viewer->property("contentX").toReal(), 0);
}

// quick-look-text-viewer REQ-F-017: an empty file shows one highlighted empty line that cannot move.
TEST(Files, QuickLookEmptyFileShowsOneHighlightedEmptyLine) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-empty"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeEmptyText(dir).isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  ASSERT_TRUE(QTest::qWaitFor([&] { return highlightedRow(harness) != nullptr; }));
  auto* row = highlightedRow(harness);
  EXPECT_EQ(rowNumber(row), QStringLiteral("1"));
  EXPECT_TRUE(descendantsNamed(row, "quickLookLineText").first()->property("text").toString().isEmpty());
  EXPECT_EQ(descendantsNamed(viewerContent(harness), "quickLookLine").size(), 1);
  pressKeyTimes(harness, Qt::Key_J, 3);
  pressKeyTimes(harness, Qt::Key_K, 3);
  EXPECT_EQ(harness.controller.preview()->currentLineIndex(), 0);
  EXPECT_EQ(rowNumber(highlightedRow(harness)), QStringLiteral("1"));
}

// quick-look-text-viewer REQ-NF-003: 100+ current-line moves, auto-scroll and resizes raise no binding loop.
TEST(Files, QuickLookLineNavigationAndResizeProduceNoBindingLoops) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-line-loops"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeNumberedLines(dir, "a.txt", 300).isEmpty());
  bindingLoopCounter().warnings.store(0);
  bindingLoopCounter().previous = qInstallMessageHandler(countBindingLoops);
  const auto restoreHandler = qScopeGuard([] { qInstallMessageHandler(bindingLoopCounter().previous); });

  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
  const QList<QSize> sizes{{1280, 800}, {640, 420}, {1600, 1000}};
  int expected = 0;
  for (const auto size : sizes) {
    harness.window->resize(size);
    ASSERT_TRUE(QTest::qWaitFor([&] { return harness.overlay->size() == QSizeF(size); }));
    pressKeyTimes(harness, Qt::Key_J, 60);
    expected += 60;
    EXPECT_EQ(harness.controller.preview()->currentLineIndex(), expected);
    pressKeyTimes(harness, Qt::Key_K, 20);
    expected -= 20;
    EXPECT_EQ(harness.controller.preview()->currentLineIndex(), expected);
    expectCardWithinBounds(harness);
  }
  ASSERT_TRUE(QTest::qWaitFor([&] { return rowInViewport(harness, highlightedRow(harness)); }));
  EXPECT_EQ(bindingLoopCounter().warnings.load(), 0);
}

// quick-look-redesign REQ-F-012: errors in the error color. Only entries that pass the Quick Look gate can open
// (images and text/plain), so the compact error card is reached via a corrupt image and an unreadable text file.
TEST(Files, QuickLookCompactCardShowsErrorForUnreadableAndCorruptFiles) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-errors"));
  ASSERT_TRUE(dir.isValid());
  const auto denied = files_test::writeFile(dir, "01-denied.txt", "secret");
  ASSERT_TRUE(QFile::setPermissions(denied, QFileDevice::Permissions()));
  QFile probe(denied);
  const bool deniedIsReadable = probe.open(QIODevice::ReadOnly);
  probe.close();
  files_test::writeCorruptJpeg(dir, "02-corrupt.jpg");
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  const auto* preview = harness.controller.preview();
  const auto error = paletteColor(harness.engine, "error");
  QStringList names{QStringLiteral("02-corrupt.jpg")};
  if (!deniedIsReadable) {  // privileged runs bypass file permissions
    names.prepend(QStringLiteral("01-denied.txt"));
  }
  for (const auto& name : names) {
    SCOPED_TRACE(name.toStdString());
    ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, name));
    ASSERT_NE(preview->previewErrorKind(), PreviewService::PreviewErrorKind::None);
    ASSERT_FALSE(preview->previewErrorMessage().isEmpty());
    EXPECT_EQ(preview->currentLineIndex(), -1);
    EXPECT_EQ(quickLookText(harness, "quickLookMetadata"), preview->previewErrorMessage());
    EXPECT_EQ(quickLookColor(harness, "quickLookMetadata"), error);
    EXPECT_TRUE(harness.window->findChild<QQuickItem*>("quickLookIcon")->isVisible());
    EXPECT_LT(popupSize(harness).width(), quickLookBounds(harness).width() / 2);
    expectCardWithinBounds(harness);
    QTest::keyClick(harness.window, Qt::Key_J);  // no lines: consumed, nothing moves, still open
    EXPECT_TRUE(harness.controller.quickLookOpen());
    EXPECT_EQ(preview->currentLineIndex(), -1);
  }
}

// quick-look-redesign REQ-F-016.
TEST(Files, QuickLookReopenOnDifferentKindUsesNewGeometry) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-reopen"));
  ASSERT_TRUE(dir.isValid());
  files_test::writeCorruptJpeg(dir, "01-corrupt.jpg");  // compact error card
  ASSERT_FALSE(files_test::writeFile(dir, "02-image.jpg", files_test::renderJpegBytes({600, 400})).isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, "01-corrupt.jpg"));
  const auto compact = popupSize(harness);
  ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, "02-image.jpg"));
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
  const auto* presentation = harness.window->findChild<QuickLookPresentationModel*>("quickLookPresentation");
  ASSERT_NE(presentation, nullptr);
  const auto calls = QuickLookPresentationModelTestAccess::requestedSizeCallCount(*presentation);
  EXPECT_TRUE(requested.isValid() && !requested.isEmpty());
  for (const auto* name : {"02.jpg", "03.jpg"}) {
    ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, QString::fromLatin1(name)));
    QTest::qWait(50);
    EXPECT_EQ(PreviewServiceTestAccess::quickLookRequestedSize(preview), requested);
    EXPECT_EQ(QuickLookPresentationModelTestAccess::requestedSizeCallCount(*presentation), calls);
  }
  harness.window->resize(1000, 700);
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.overlay->width() == 1000; }));
  EXPECT_GT(QuickLookPresentationModelTestAccess::requestedSizeCallCount(*presentation), calls);
  EXPECT_NE(PreviewServiceTestAccess::quickLookRequestedSize(preview), requested);
}

// quick-look-redesign REQ-NF-001.
TEST(Files, QuickLookNavigationAndResizeProduceNoBindingLoops) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-loops"));
  ASSERT_TRUE(dir.isValid());
  ASSERT_FALSE(files_test::writeFile(dir, "01.jpg", files_test::renderJpegBytes({600, 400})).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "02.jpg", files_test::renderJpegBytes({200, 900})).isEmpty());
  files_test::writeSmallText(dir, "03.txt");
  files_test::writeCorruptJpeg(dir, "04.jpg");

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
    for (const auto* name : {"01.jpg", "02.jpg", "03.txt", "04.jpg"}) {
      ASSERT_NO_FATAL_FAILURE(showQuickLookOn(harness, QString::fromLatin1(name)));
      expectCardWithinBounds(harness);
    }
    for (int step = 0; step < 5; ++step) {
      QTest::keyClick(harness.window, Qt::Key_K);
    }
    ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.preview()->busy(); }));
  }
  EXPECT_EQ(bindingLoopCounter().warnings.load(), 0);
}

// quick-look-redesign regression: key handling inside Quick Look must not pull focus onto the listing behind
// the modal popup. j now moves the viewer's current line (the file stays pinned), never the cursor.
TEST(Files, QuickLookKeepsFocusWhileMovingTheCurrentLineAndCloses) {
  QTemporaryDir dir(files_test::fixturePattern("quicklook-close"));
  ASSERT_TRUE(dir.isValid());
  QByteArray lines;
  for (int i = 1; i <= 60; ++i) {
    lines += "line " + QByteArray::number(i) + '\n';
  }
  ASSERT_FALSE(files_test::writeFile(dir, "a.txt", lines).isEmpty());
  ASSERT_FALSE(files_test::writeFile(dir, "b.txt", lines).isEmpty());
  QuickLookHarness harness;
  ASSERT_NO_FATAL_FAILURE(loadQuickLookHarness(harness, dir.path()));
  auto* content = harness.window->findChild<QQuickItem*>("quickLookContent");
  ASSERT_NE(content, nullptr);
  for (const auto closeKey : {Qt::Key_Escape, Qt::Key_Space}) {
    SCOPED_TRACE(closeKey);
    ASSERT_NO_FATAL_FAILURE(openQuickLook(harness));
    const auto startRow = harness.controller.cursorRow();
    ASSERT_EQ(harness.controller.preview()->currentLineIndex(), 0);
    for (int step = 0; step < 30; ++step) {
      QTest::keyClick(harness.window, Qt::Key_J);
      QTest::qWait(5);
      ASSERT_EQ(harness.window->activeFocusItem(), content) << "step " << step;
    }
    EXPECT_EQ(harness.controller.cursorRow(), startRow);
    EXPECT_EQ(harness.controller.preview()->currentLineIndex(), 30);
    QTest::keyClick(harness.window, closeKey);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.popup->property("visible").toBool(); }));
    EXPECT_FALSE(harness.controller.quickLookOpen());
    EXPECT_TRUE(harness.window->findChild<QQuickItem*>("directoryListView")->hasActiveFocus());
  }
}

namespace {
struct ModeBadge {
  QQuickItem* item = nullptr;
  QQuickItem* label = nullptr;
  QQuickItem* background = nullptr;
};

ModeBadge findModeBadge(QQuickWindow* window) {
  ModeBadge badge;
  badge.item = window->findChild<QQuickItem*>("modeBadge");
  if (badge.item != nullptr) {
    badge.label = badge.item->findChild<QQuickItem*>("modeBadgeLabel");
    badge.background = badge.item->property("background").value<QQuickItem*>();
  }
  return badge;
}

// mode-status-badge REQ-F-002/F-003/F-009: label, live palette fill and accessible name agree.
::testing::AssertionResult badgeShows(const ModeBadge& badge, const QString& label, const QString& fillToken) {
  const auto text = badge.label->property("text").toString();
  const auto fill = badge.background->property("color");
  const auto expectedFill = evaluateInContext(badge.background, "HoloniightPalette." + fillToken);
  const auto name = QQmlProperty(badge.item, QStringLiteral("Accessible.name"), qmlContext(badge.item)).read();
  if (!badge.item->isVisible() || text != label || !expectedFill.isValid() || fill != expectedFill ||
      name.toString() != label + " mode") {
    return ::testing::AssertionFailure() << "badge shows " << text.toStdString() << " / "
                                         << fill.value<QColor>().name().toStdString() << " / "
                                         << name.toString().toStdString() << ", expected " << label.toStdString()
                                         << " / " << fillToken.toStdString();
  }
  return ::testing::AssertionSuccess();
}

struct BadgeHarness {
  QTemporaryDir dir{files_test::fixturePattern("mode-badge")};
  DirectoryController controller;
  LoadedWindow loaded;
  ModeBadge badge;

  BadgeHarness() {
    files_test::populateEntries(dir, 3);
    loaded = loadActiveWindow(controller, dir.path());
    if (loaded.window != nullptr) {
      badge = findModeBadge(loaded.window);
    }
  }
  [[nodiscard]] bool ready() const {
    return loaded.list != nullptr && badge.item != nullptr && badge.label != nullptr && badge.background != nullptr;
  }
};
}  // namespace

TEST(Files, ModeBadgeNormalMode) {
  BadgeHarness harness;
  ASSERT_TRUE(harness.ready());
  // REQ-F-001: first row child of the status bar, ahead of every mode-contextual label.
  auto* statusBar = harness.loaded.window->findChild<QQuickItem*>("modeStatusBar");
  ASSERT_NE(statusBar, nullptr);
  ASSERT_FALSE(statusBar->childItems().isEmpty());
  EXPECT_EQ(statusBar->childItems().first(), harness.badge.item);
  EXPECT_EQ(harness.badge.label->property("font").value<QFont>().family(),
            evaluateInContext(harness.badge.label, "HolonightTheme.monospaceFont").toString());
  EXPECT_TRUE(badgeShows(harness.badge, "NORMAL", "accentBlue"));
  EXPECT_EQ(harness.badge.label->property("color"),
            evaluateInContext(harness.badge.label, "HoloniightPalette.background"));
  EXPECT_EQ(QQmlProperty(harness.badge.item, QStringLiteral("Accessible.role"), qmlContext(harness.badge.item))
                .read()
                .toInt(),
            QAccessible::StaticText);
}

TEST(Files, ModeBadgeVisualMode) {
  BadgeHarness harness;
  ASSERT_TRUE(harness.ready());
  QTest::keyClick(harness.loaded.window, Qt::Key_V);
  ASSERT_EQ(harness.controller.vim()->currentMode(), VimModeController::Mode::Visual);
  EXPECT_TRUE(badgeShows(harness.badge, "VISUAL", "accentViolet"));
}

TEST(Files, ModeBadgeSearchMode) {
  BadgeHarness harness;
  ASSERT_TRUE(harness.ready());
  QTest::keyClick(harness.loaded.window, Qt::Key_Slash);
  ASSERT_EQ(harness.controller.vim()->currentMode(), VimModeController::Mode::Search);
  EXPECT_TRUE(badgeShows(harness.badge, "SEARCH", "accentYellow"));
}

TEST(Files, ModeBadgeInsertMode) {
  BadgeHarness harness;
  ASSERT_TRUE(harness.ready());
  QTest::keyClick(harness.loaded.window, Qt::Key_I);
  ASSERT_EQ(harness.controller.vim()->currentMode(), VimModeController::Mode::Insert);
  EXPECT_TRUE(badgeShows(harness.badge, "INSERT", "success"));
}

// REQ-F-005 / REQ-NF-001: every transition lands in one event-loop spin without moving the badge.
TEST(Files, ModeBadgeWidthConstant) {
  BadgeHarness harness;
  ASSERT_TRUE(harness.ready());
  auto* window = harness.loaded.window;
  const auto width = harness.badge.item->width();
  const auto sceneX = harness.badge.item->mapToScene(QPointF()).x();
  EXPECT_GT(width, 0);
  // REQ-F-007: vertically centred in the status bar, rounded with the Pill role.
  auto* statusBar = window->findChild<QQuickItem*>("modeStatusBar");
  ASSERT_NE(statusBar, nullptr);
  EXPECT_NEAR(harness.badge.item->mapToItem(statusBar, QPointF(0, harness.badge.item->height() / 2)).y(),
              statusBar->height() / 2, 1);
  EXPECT_EQ(harness.badge.background->property("radius"),
            evaluateInContext(harness.badge.background,
                              "HnAppearance.roundedRadius(HnSurfaceRole.Pill, width, height, HnAppearance.revision)"));

  const QList<std::tuple<Qt::Key, VimModeController::Mode, QString, QString>> steps{
      {Qt::Key_V, VimModeController::Mode::Visual, "VISUAL", "accentViolet"},
      {Qt::Key_Escape, VimModeController::Mode::Normal, "NORMAL", "accentBlue"},
      {Qt::Key_Slash, VimModeController::Mode::Search, "SEARCH", "accentYellow"},
      {Qt::Key_Escape, VimModeController::Mode::Normal, "NORMAL", "accentBlue"},
      {Qt::Key_I, VimModeController::Mode::Insert, "INSERT", "success"},
      {Qt::Key_Escape, VimModeController::Mode::Normal, "NORMAL", "accentBlue"},
  };
  for (const auto& [key, mode, label, fill] : steps) {
    SCOPED_TRACE(label.toStdString());
    QTest::keyClick(window, key);
    ASSERT_EQ(harness.controller.vim()->currentMode(), mode);
    QTest::qWait(0);
    EXPECT_TRUE(badgeShows(harness.badge, label, fill));
    EXPECT_EQ(harness.badge.item->width(), width);
    EXPECT_EQ(harness.badge.item->mapToScene(QPointF()).x(), sceneX);
  }
}

TEST(Files, ModeBadgeVisibleDuringPrompt) {
  QTemporaryDir home(files_test::fixturePattern("mode-badge-trash-home"));
  ASSERT_TRUE(home.isValid());
  const files_test::ScopedXdgDataHome guard(home.path());
  BadgeHarness harness;
  ASSERT_TRUE(harness.ready());
  QTest::keyClick(harness.loaded.window, 'D', Qt::ShiftModifier);
  ASSERT_TRUE(QTest::qWaitFor([&] { return harness.controller.tasks()->hasPrompt(); }));
  EXPECT_TRUE(badgeShows(harness.badge, "NORMAL", "accentBlue"));
  QTest::keyClick(harness.loaded.window, Qt::Key_N);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !harness.controller.tasks()->hasPrompt(); }));
  EXPECT_TRUE(badgeShows(harness.badge, "NORMAL", "accentBlue"));
}

// REQ-F-008: the badge names the mode, so the contextual labels no longer repeat it.
TEST(Files, ModeBadgeRemovesPrefixes) {
  BadgeHarness harness;
  ASSERT_TRUE(harness.ready());
  auto* window = harness.loaded.window;
  auto* visualLabel = window->findChild<QQuickItem*>("visualStatusLabel");
  auto* insertLabel = window->findChild<QQuickItem*>("insertStatusLabel");
  ASSERT_NE(visualLabel, nullptr);
  ASSERT_NE(insertLabel, nullptr);

  QTest::keyClick(window, Qt::Key_V);
  QTest::keyClick(window, Qt::Key_J);
  ASSERT_EQ(harness.controller.vim()->selectedCount(), 2);
  EXPECT_EQ(visualLabel->property("rawText").toString(), "2 selected");
  QTest::keyClick(window, Qt::Key_Escape);

  QTest::keyClick(window, Qt::Key_I);
  ASSERT_TRUE(harness.controller.vim()->insertValid());
  EXPECT_FALSE(insertLabel->isVisible());
  EXPECT_TRUE(window->findChild<QQuickItem*>("insertGuidance")->isVisible());
  EXPECT_EQ(window->findChild<QQuickItem*>("insertConfirmKeys")->property("accessibleText").toString(), "Return");
  EXPECT_EQ(window->findChild<QQuickItem*>("insertCancelKeys")->property("accessibleText").toString(), "Esc");
  QTest::keyClick(window, Qt::Key_Slash);
  QTest::keyClick(window, Qt::Key_X);
  ASSERT_FALSE(harness.controller.vim()->insertValid());
  EXPECT_FALSE(harness.controller.vim()->insertErrorMessage().isEmpty());
  EXPECT_EQ(insertLabel->property("rawText").toString(), harness.controller.vim()->insertErrorMessage());
  EXPECT_TRUE(insertLabel->isVisible());
  EXPECT_FALSE(window->findChild<QQuickItem*>("insertGuidance")->isVisible());
  QTest::keyClick(window, Qt::Key_Escape);
}

// REQ-F-010: display-only — clicking the badge changes neither focus nor mode.
TEST(Files, ModeBadgeNoMouseOrFocus) {
  BadgeHarness harness;
  ASSERT_TRUE(harness.ready());
  auto* window = harness.loaded.window;
  auto* focused = window->activeFocusItem();
  ASSERT_NE(focused, nullptr);
  EXPECT_FALSE(harness.badge.item->property("activeFocusOnTab").toBool());
  EXPECT_EQ(harness.badge.item->property("focusPolicy").toInt(), Qt::NoFocus);
  const auto center =
      harness.badge.item->mapToScene(QPointF(harness.badge.item->width() / 2, harness.badge.item->height() / 2));
  QTest::mouseClick(window, Qt::LeftButton, Qt::NoModifier, center.toPoint());
  QTest::qWait(0);
  EXPECT_EQ(window->activeFocusItem(), focused);
  EXPECT_FALSE(harness.badge.item->hasActiveFocus());
  EXPECT_EQ(harness.controller.vim()->currentMode(), VimModeController::Mode::Normal);
  QTest::keyClick(window, Qt::Key_J);
  EXPECT_EQ(harness.controller.cursorRow(), 1);
}

TEST(Files, InlineEditorSynchronizesTextWithoutBindingLoops) {
  bindingLoopCounter().warnings.store(0);
  bindingLoopCounter().previous = qInstallMessageHandler(countBindingLoops);
  const auto restoreHandler = qScopeGuard([] { qInstallMessageHandler(bindingLoopCounter().previous); });
  QTemporaryDir dir(files_test::fixturePattern("inline-binding"));
  ASSERT_TRUE(dir.isValid());
  files_test::populateEntries(dir, 3);
  DirectoryController controller;
  auto loaded = loadActiveWindow(controller, dir.path());
  ASSERT_NE(loaded.list, nullptr);
  for (const auto key : {Qt::Key_I, Qt::Key_A, Qt::Key_O}) {
    SCOPED_TRACE(static_cast<int>(key));
    QTest::keyClick(loaded.window, key);
    ASSERT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
    auto* editor = loaded.window->activeFocusItem();
    ASSERT_NE(editor, nullptr);
    ASSERT_EQ(editor->objectName(), "inlineNameEditor");
    EXPECT_EQ(editor->property("text").toString(), controller.vim()->insertText());
    controller.updateInsertText("draft.txt");
    EXPECT_EQ(editor->property("text").toString(), "draft.txt");
    QTest::keyClick(loaded.window, Qt::Key_End);
    QTest::keyClick(loaded.window, Qt::Key_X);
    EXPECT_EQ(controller.vim()->insertText(), "draft.txtx");
    EXPECT_EQ(editor->property("text").toString(), "draft.txtx");
    EXPECT_TRUE(controller.vim()->insertValid());
    QTest::keyClick(loaded.window, Qt::Key_Home);
    QTest::keyClick(loaded.window, Qt::Key_Slash);
    EXPECT_FALSE(controller.vim()->insertValid());
    EXPECT_TRUE(editor->property("hasError").toBool());
    QTest::keyClick(loaded.window, Qt::Key_Escape);
    EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
    ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
    EXPECT_FALSE(QFile::exists(dir.filePath("draft.txtx")));
  }
  EXPECT_EQ(bindingLoopCounter().warnings.load(), 0);
}

TEST(Files, RuntimeStyleEditingShowsValidationAndCommits) {
  QTemporaryDir dir(files_test::fixturePattern("runtime-style-edit"));
  DirectoryController controller;
  QQmlApplicationEngine engine;
  initializeFilesEngine(engine);
  engine.setInitialProperties({{QStringLiteral("controller"), QVariant::fromValue(&controller)}});
  engine.loadFromModule("HolonightFiles", "Main");
  ASSERT_EQ(engine.rootObjects().size(), 1);
  controller.open(dir.path());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  controller.handleKey("o");
  auto* window = qobject_cast<QQuickWindow*>(engine.rootObjects().first());
  ASSERT_NE(window, nullptr);
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return window->activeFocusItem() && window->activeFocusItem()->objectName() == "inlineNameEditor"; }));
  auto* editor = window->activeFocusItem();
  ASSERT_NE(editor, nullptr);
  EXPECT_TRUE(editor->isVisible());
  controller.updateInsertText("invalid/name");
  controller.commitInsertEditing();
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Insert);
  auto* status = window->findChild<QQuickItem*>("insertStatusLabel");
  ASSERT_NE(status, nullptr);
  EXPECT_TRUE(status->isVisible());
  EXPECT_EQ(status->property("rawText").toString(), controller.vim()->insertErrorMessage());
  EXPECT_FALSE(status->property("rawText").toString().isEmpty());
  controller.updateInsertText("created.txt");
  controller.commitInsertEditing();
  EXPECT_TRUE(QFileInfo::exists(dir.filePath("created.txt")));
  EXPECT_EQ(controller.vim()->currentMode(), VimModeController::Mode::Normal);
}
