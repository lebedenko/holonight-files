#include "directory_controller.h"
#include "directory_fixtures.h"
#include "engine_setup.h"
#include "storage_fixtures.h"

#include <QDirIterator>
#include <QFile>
#include <QIcon>
#include <QKeyEvent>
#include <QLocale>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include <QQmlExpression>
#include <QQmlProperty>
#include <QQuickItem>
#include <QQuickWindow>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QTest>

#include <StorageBackend.h>
#include <array>
#include <gtest/gtest.h>
#include <tuple>

using files_test::findPlaceRow;

namespace {
// Without a seeded XDG_CONFIG_HOME the sidebar is built from whatever ~/.config/user-dirs.dirs the
// machine happens to have -- a desktop has a full set, a freshly created CI account has no file at
// all and so gets Home alone. Tests that move between rows or need the list to overflow must seed
// their own; declare one before the PlacesWindow it applies to, so the guards outlive the model.
struct SeededUserDirs {
  QTemporaryDir config_home{files_test::fixturePattern("places-window-config")};
  QTemporaryDir data_home{files_test::fixturePattern("places-window-data")};
  files_test::ScopedXdgConfigHome config_guard;
  files_test::ScopedXdgDataHome data_guard;
  explicit SeededUserDirs(int count) : config_guard(seed(count)), data_guard(data_home.path()) {}
  // All nine keys PlacesModel recognizes, in its display order.
  static constexpr std::array<const char*, 9> kKeys{"DESKTOP", "DOCUMENTS", "DOWNLOAD",  "PICTURES",   "MUSIC",
                                                    "VIDEOS",  "PROJECTS",  "TEMPLATES", "PUBLICSHARE"};
  // Writes the seeded user-dirs.dirs and returns the config home it lives in.
  [[nodiscard]] QString seed(int count) const {
    QByteArray contents;
    for (int i = 0; i < count; ++i) {
      const auto path = data_home.filePath(QString::fromLatin1(kKeys.at(i)).toLower());
      QDir().mkpath(path);  // real directories, so the availability check reports them available
      contents += "XDG_" + QByteArray(kKeys.at(i)) + "_DIR=\"" + path.toUtf8() + "\"\n";
    }
    files_test::writeFile(config_home, "user-dirs.dirs", contents);
    return config_home.path();
  }
};

struct PlacesWindow {
  PlacesWindow() : controller(&storage, nullptr, probe) { initializeFilesEngine(engine); }
  QTemporaryDir dir{files_test::fixturePattern("places-window")};
  files_test::FakeStorage storage_backend;
  HoloNight::System::StorageController storage{&storage_backend};
  std::shared_ptr<files_test::FakeCapacityProbe> probe = std::make_shared<files_test::FakeCapacityProbe>();
  DirectoryController controller;
  QQmlApplicationEngine engine;
  QQuickWindow* window = nullptr;
  QQuickItem* places = nullptr;
  QQuickItem* sidebar = nullptr;  // The one Flickable holding Places and Devices; owns keyboard focus.
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
    sidebar = window->findChild<QQuickItem*>("sidebarPanel");
    listing = window->findChild<QQuickItem*>("directoryListView");
    controller.open(dir.path());
    return places != nullptr && sidebar != nullptr && listing != nullptr &&
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
  const SeededUserDirs seeded(1);  // Home plus one row, so Down/Up have somewhere to go.
  PlacesWindow view;
  ASSERT_TRUE(view.start());
  ASSERT_EQ(view.controller.places()->rowCount(), 2);
  view.target();
  EXPECT_FALSE(view.button()->property("highlighted").toBool());
  EXPECT_FALSE(view.button()->property("selected").toBool());
  // Reach the sidebar using actual Tab traversal from the focused listing.
  for (int i = 0; i < 20 && !view.sidebar->hasActiveFocus(); ++i) {
    QTest::keyClick(view.window, Qt::Key_Tab);
  }
  ASSERT_TRUE(view.sidebar->hasActiveFocus());
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
  view.sidebar->forceActiveFocus();
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
  view.controller.handleKey("j");  // Move from ".." to "target/"
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
  view.controller.handleKey("j");  // Quick Look needs a previewable file: move from "target/" to file.txt
  ASSERT_TRUE(QTest::qWaitFor([&] { return view.controller.preview()->quickLookEligible(); }, 5000));
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
  const auto kSeeded = static_cast<int>(SeededUserDirs::kKeys.size());
  const SeededUserDirs seeded(kSeeded);  // 10 rows: enough to overflow 220px.
  PlacesWindow view;
  ASSERT_TRUE(view.start());
  ASSERT_EQ(view.controller.places()->rowCount(), kSeeded + 1);
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
  view.sidebar->forceActiveFocus();
  for (int i = 0; i < view.controller.places()->rowCount(); ++i) {
    QTest::keyClick(view.window, Qt::Key_Down);
  }
  ASSERT_TRUE(QTest::qWaitFor([&] {
    const auto top = view.row()->mapToItem(view.sidebar, QPointF()).y();
    return view.sidebar->property("contentY").toReal() > 0 && top >= 0 &&
           top + view.row()->height() <= view.sidebar->height() + 1;
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
  view.sidebar->forceActiveFocus();
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

// ---------------------------------------------------------------------------------------------
// device-actions: sidebar composition, device rows, capacity bar and keyboard (T-008..T-011)
// ---------------------------------------------------------------------------------------------

namespace {
using HoloNight::System::StorageDrive;
using HoloNight::System::StorageOperation;
using HoloNight::System::StorageVolume;

constexpr quint64 kTerabyte = 1000ULL * 1000 * 1000 * 1000;
constexpr quint64 kFree = 345ULL * 1000 * 1000 * 1000;

QQuickItem* itemAtIndex(QQuickItem* list, int index) {
  QQuickItem* found = nullptr;
  QMetaObject::invokeMethod(list, "itemAtIndex", Qt::DirectConnection, qReturnArg(found), Q_ARG(int, index));
  return found;
}
QVariant evaluateIn(QQuickItem* item, const QString& expression) {
  QQmlExpression evaluated(qmlContext(item), item, expression);
  return evaluated.evaluate();
}
// Delivers one key straight to item and reports whether its handlers accepted it.
bool sendKey(QQuickItem* item, int key, const QString& text, Qt::KeyboardModifiers modifiers = Qt::NoModifier) {
  QKeyEvent press(QEvent::KeyPress, key, modifiers, text);
  press.setAccepted(false);
  QCoreApplication::sendEvent(item, &press);
  return press.isAccepted();
}
double centreY(QQuickItem* item, QQuickItem* within) {
  return item->mapToItem(within, QPointF(0, item->height() / 2)).y();
}
StorageDrive internalDrive(const QString& driveId) {
  auto record = files_test::storageDrive(driveId, false);
  record.connectionBus.clear();
  record.canPowerOff = false;
  return record;
}
StorageDrive opticalDrive(const QString& driveId, bool media) {
  auto record = files_test::storageDrive(driveId, true, media);
  record.optical = true;
  record.mediaRemovable = true;
  record.canEject = true;
  record.canPowerOff = false;
  return record;
}
StorageVolume mountedVolume(const QString& volumeId, const QString& drive, const QString& mountPoint) {
  auto record = files_test::storageVolume(volumeId, drive);
  record.mountPoints = {mountPoint};
  return record;
}

struct DevicesWindow : PlacesWindow {
  QQuickItem* devices_list = nullptr;
  bool startWith(const QList<StorageDrive>& drives, const QList<StorageVolume>& volumes) {
    storage_backend.drives = drives;
    storage_backend.volumes = volumes;
    storage_backend.publish();
    if (!start()) {
      return false;
    }
    devices_list = window->findChild<QQuickItem*>("devicesListView");
    return devices_list != nullptr &&
           QTest::qWaitFor([&] { return devices_list->property("count").toInt() == controller.devices()->rowCount(); });
  }
  [[nodiscard]] QQuickItem* find(const char* name) const { return window->findChild<QQuickItem*>(name); }
  [[nodiscard]] QQuickItem* deviceRow(const QString& targetId) {
    return itemAtIndex(devices_list, controller.devices()->findRow(targetId));
  }
  [[nodiscard]] QQuickItem* part(const QString& targetId, const char* name) {
    auto* row = deviceRow(targetId);
    return row != nullptr ? row->findChild<QQuickItem*>(name) : nullptr;
  }
  void setSidebarWidth(int width) const {
    window->setProperty("sidebarWidth", width);
    QTest::qWait(30);
  }
  // Focus the sidebar with the cursor on targetId's device row.
  void focusDevice(const QString& targetId) {
    sidebar->forceActiveFocus();
    controller.sidebarNavigator()->setCursor(SidebarNavigator::Devices, controller.devices()->findRow(targetId));
  }
  [[nodiscard]] bool press(int key, const QString& text = {}, Qt::KeyboardModifiers modifiers = Qt::NoModifier) const {
    return sendKey(sidebar, key, text, modifiers);
  }
  void finishLastCall(const QString& errorName = {}) {
    auto result = storage_backend.calls.last();
    result.errorName = errorName;
    emit storage_backend.operationFinished(result);
    QCoreApplication::processEvents();
  }
};
}  // namespace

TEST(PlacesWindow, SidebarIsOneScrollAreaReachingDevices) {
  const SeededUserDirs seeded(static_cast<int>(SeededUserDirs::kKeys.size()));
  DevicesWindow view;
  ASSERT_TRUE(view.startWith({files_test::storageDrive("stick")},
                             {files_test::storageVolume("a", "stick"), files_test::storageVolume("b", "stick")}));
  view.window->setMinimumHeight(0);
  view.window->resize(800, 280);
  auto* container = view.find("sidebarContainer");
  ASSERT_NE(container, nullptr);
  int scrollBars = 0;
  for (auto* child : container->findChildren<QQuickItem*>()) {
    scrollBars += QString::fromUtf8(child->metaObject()->className()).contains("ScrollBar") ? 1 : 0;
  }
  EXPECT_EQ(scrollBars, 1);  // REQ-F-030
  auto* contentItem = view.sidebar->property("contentItem").value<QQuickItem*>();
  ASSERT_NE(contentItem, nullptr);
  EXPECT_TRUE(contentItem->isAncestorOf(view.places));
  EXPECT_TRUE(contentItem->isAncestorOf(view.devices_list));
  ASSERT_TRUE(
      QTest::qWaitFor([&] { return view.sidebar->property("contentHeight").toReal() > view.sidebar->height(); }));
  view.sidebar->setProperty("contentY", view.sidebar->property("contentHeight").toReal() - view.sidebar->height());
  auto* last = view.deviceRow("b");
  ASSERT_NE(last, nullptr);
  const auto top = last->mapToItem(view.sidebar, QPointF()).y();
  EXPECT_GE(top, 0);
  EXPECT_LE(top + last->height(), view.sidebar->height() + 1);
}

TEST(PlacesWindow, SeparatorSitsBetweenPlacesAndDevicesAtEveryWidth) {
  const SeededUserDirs seeded(2);
  DevicesWindow view;
  ASSERT_TRUE(view.startWith({files_test::storageDrive("stick")}, {files_test::storageVolume("usb", "stick")}));
  auto* separator = view.find("sidebarSectionSeparator");
  auto* heading = view.find("devicesHeading");
  ASSERT_TRUE(separator != nullptr && heading != nullptr);
  for (const int width : {220, 400}) {
    view.setSidebarWidth(width);
    auto* lastPlace = itemAtIndex(view.places, view.controller.places()->rowCount() - 1);
    ASSERT_NE(lastPlace, nullptr);
    const auto placesBottom = lastPlace->mapToItem(view.sidebar, QPointF(0, lastPlace->height())).y();
    const auto separatorY = separator->mapToItem(view.sidebar, QPointF()).y();
    EXPECT_TRUE(separator->isVisible());
    EXPECT_GT(separatorY, placesBottom) << width;                                     // REQ-F-031
    EXPECT_LT(separatorY, heading->mapToItem(view.sidebar, QPointF()).y()) << width;  // REQ-F-031
  }
}

TEST(PlacesWindow, EmptyDevicesLeaveNeitherSeparatorNorGap) {
  const SeededUserDirs seeded(2);
  DevicesWindow view;
  ASSERT_TRUE(view.startWith({}, {}));
  auto* separator = view.find("sidebarSectionSeparator");
  auto* devices = view.find("devicesPanel");
  auto* places = view.find("placesPanel");
  ASSERT_TRUE(separator != nullptr && devices != nullptr && places != nullptr);
  EXPECT_FALSE(separator->isVisible());  // REQ-F-032
  EXPECT_FALSE(devices->isVisible());
  EXPECT_NEAR(view.sidebar->property("contentHeight").toReal(), places->height(), 1.0);
}

TEST(PlacesWindow, SidebarSectionsSizeToTheirRows) {
  for (const auto& [places, deviceRows] : {std::pair{1, 1}, std::pair{9, 20}}) {  // 3 and 30 rows in all
    const SeededUserDirs seeded(places);
    DevicesWindow view;
    QList<StorageVolume> volumes;
    for (int row = 0; row < deviceRows; ++row) {
      volumes.append(files_test::storageVolume(QStringLiteral("v%1").arg(row, 2, 10, QLatin1Char('0')), "stick"));
    }
    ASSERT_TRUE(view.startWith({files_test::storageDrive("stick")}, volumes));
    auto* separator = view.find("sidebarSectionSeparator");
    auto* devices = view.find("devicesPanel");
    auto* placesPanel = view.find("placesPanel");
    ASSERT_TRUE(QTest::qWaitFor([&] { return view.devices_list->property("count").toInt() == deviceRows; }));
    const auto expected = placesPanel->height() + separator->height() + devices->height();
    EXPECT_NEAR(view.sidebar->property("contentHeight").toReal(), expected, 1.0) << places + 1 + deviceRows;
    EXPECT_NEAR(view.places->height(), view.places->property("contentHeight").toReal(), 1.0);  // REQ-F-033
    EXPECT_NEAR(view.devices_list->height(), view.devices_list->property("contentHeight").toReal(), 1.0);
  }
}

TEST(PlacesWindow, NoSidebarChildSizesAsAProportionOfTheSidebar) {
  const QRegularExpression proportional(R"re(height\s*\*\s*0?\.\d|height\s*/\s*\d)re");
  for (const auto* file :
       {"qml/Main.qml", "qml/places/SidebarPanel.qml", "qml/places/PlacesPanel.qml", "qml/places/DevicesPanel.qml"}) {
    QFile source(QStringLiteral(FILES_SOURCE_DIR "/apps/files/") + QString::fromLatin1(file));
    ASSERT_TRUE(source.open(QIODevice::ReadOnly)) << file;
    const auto text = QString::fromUtf8(source.readAll());
    const auto match = proportional.match(text);
    EXPECT_FALSE(match.hasMatch()) << file << ": " << match.captured().toStdString();
  }
}

TEST(PlacesWindow, CrossingIntoDevicesRevealsTheFocusedRow) {
  const SeededUserDirs seeded(static_cast<int>(SeededUserDirs::kKeys.size()));
  DevicesWindow view;
  ASSERT_TRUE(view.startWith({files_test::storageDrive("stick")}, {files_test::storageVolume("usb", "stick")}));
  view.window->setMinimumHeight(0);
  view.window->resize(800, 260);
  QTest::qWait(30);
  view.sidebar->forceActiveFocus();
  view.controller.sidebarNavigator()->setCursor(SidebarNavigator::Places, view.controller.places()->rowCount() - 1);
  EXPECT_TRUE(view.press(Qt::Key_Down));
  ASSERT_EQ(view.controller.sidebarNavigator()->section(), SidebarNavigator::Devices);
  auto* row = view.deviceRow("usb");
  ASSERT_NE(row, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    const auto top = row->mapToItem(view.sidebar, QPointF()).y();
    return top >= -0.5 && top + row->height() <= view.sidebar->height() + 1;  // REQ-F-053
  }));
}

TEST(PlacesWindow, DeviceRowsCentreIconAndRemovalControlAndAbsorbWidth) {
  DevicesWindow view;
  view.probe->set("/media/stick", kTerabyte, kFree);
  ASSERT_TRUE(view.startWith(
      {files_test::storageDrive("stick"), files_test::storageDrive("other")},
      {mountedVolume("measured", "stick", "/media/stick"), files_test::storageVolume("plain", "other")}));
  ASSERT_TRUE(QTest::qWaitFor([&] { return view.part("measured", "capacityBar")->opacity() == 1.0; }));
  EXPECT_EQ(view.part("plain", "capacityBar")->opacity(), 0.0);
  EXPECT_EQ(view.part("plain", "capacityText")->property("rawText").toString(),
            view.controller.devices()->data(
                view.controller.devices()->index(view.controller.devices()->findRow("plain")), DevicesModel::State));
  for (const auto* target : {"measured", "plain"}) {
    auto* body = view.part(target, "deviceRowBody");
    ASSERT_NE(body, nullptr) << target;
    // Spacing alone separates the columns (REQ-F-034).
    for (auto* child : body->findChildren<QQuickItem*>()) {
      EXPECT_FALSE(QString::fromUtf8(child->metaObject()->className()).contains("Separator")) << target;
    }
    EXPECT_NEAR(centreY(view.part(target, "deviceIcon"), body), body->height() / 2, 1.0) << target;  // REQ-F-035
  }
  EXPECT_NEAR(centreY(view.part("measured", "deviceRemoveButton"), view.part("measured", "deviceRowBody")),
              view.part("measured", "deviceRowBody")->height() / 2, 1.0);  // REQ-F-040
  EXPECT_FALSE(view.part("plain", "deviceRemoveButton")->isVisible());     // Not mounted, nothing to remove.
  // Measured or not, mounted or not, every row is equally tall (REQ-F-043).
  EXPECT_NEAR(view.part("plain", "deviceRowBody")->height(), view.part("measured", "deviceRowBody")->height(), 0.5);
  view.setSidebarWidth(220);
  const auto iconWidth = view.part("measured", "deviceIcon")->width();
  const auto buttonWidth = view.part("measured", "deviceRemoveButton")->width();
  const auto contentWidth = view.part("measured", "deviceContent")->width();
  view.setSidebarWidth(400);
  EXPECT_EQ(view.part("measured", "deviceIcon")->width(), iconWidth);  // REQ-F-038
  EXPECT_EQ(view.part("measured", "deviceRemoveButton")->width(), buttonWidth);
  EXPECT_NEAR(view.part("measured", "deviceContent")->width() - contentWidth, 180.0, 1.0);
}

TEST(PlacesWindow, ExternalAndOpticalRowsShowOneSharedEjectControl) {
  DevicesWindow view;
  ASSERT_TRUE(
      view.startWith({files_test::storageDrive("stick"), opticalDrive("dvd", true)},
                     {mountedVolume("usb", "stick", "/media/usb"), mountedVolume("disc", "dvd", "/media/disc")}));
  QVariant source;
  for (const auto* target : {"usb", "disc"}) {
    auto* row = view.deviceRow(target);
    ASSERT_NE(row, nullptr) << target;
    int visibleButtons = 0;
    for (auto* child : row->findChildren<QQuickItem*>()) {
      const auto className = QString::fromUtf8(child->metaObject()->className());
      visibleButtons +=
          child->isVisible() && (className.contains("Button") || className.contains("HnIconButton")) ? 1 : 0;
    }
    EXPECT_EQ(visibleButtons, 1) << target;  // REQ-F-010: no separate Unmount while mounted
    const auto icon = evaluateIn(view.part(target, "deviceRemoveButton"), "icon.source");
    if (source.isValid()) {
      EXPECT_EQ(icon, source);  // REQ-F-014
    }
    source = icon;
  }
  EXPECT_TRUE(source.toString().startsWith("qrc:/qt/qml/HolonightFiles/icons/")) << source.toString().toStdString();
  // The shared glyph is disambiguated by its accessible name (REQ-NF-002).
  EXPECT_NE(evaluateIn(view.part("usb", "deviceRemoveButton"), "Accessible.name").toString(),
            evaluateIn(view.part("disc", "deviceRemoveButton"), "Accessible.name").toString());
}

TEST(PlacesWindow, DeviceRowsFallBackToABundledIcon) {
  DevicesWindow view;
  ASSERT_TRUE(view.startWith({files_test::storageDrive("stick")}, {files_test::storageVolume("usb", "stick")}));
  auto* row = view.deviceRow("usb");
  ASSERT_NE(row, nullptr);
  row->setProperty("iconName", "devices-deliberately-unavailable-icon");
  auto* fallback = view.part("usb", "deviceFallbackIcon");
  ASSERT_NE(fallback, nullptr);
  ASSERT_TRUE(QTest::qWaitFor([&] {
    const auto images = fallback->findChildren<QQuickItem*>();
    return fallback->isVisible() && !images.isEmpty() && images.first()->property("status").toInt() == 1;
  }));  // REQ-F-037
}

TEST(PlacesWindow, CapacityBarFillsUsedSpaceInThresholdColours) {
  DevicesWindow view;
  view.probe->set("/media/usb", kTerabyte, kFree);
  ASSERT_TRUE(view.startWith({files_test::storageDrive("stick")}, {mountedVolume("usb", "stick", "/media/usb")}));
  ASSERT_TRUE(QTest::qWaitFor([&] { return view.part("usb", "capacityBar")->opacity() == 1.0; }));
  auto* bar = view.part("usb", "capacityBar");
  auto* fill = bar->findChild<QQuickItem*>("capacityFill");
  ASSERT_NE(fill, nullptr);
  EXPECT_NEAR(fill->width(), bar->width() * (1.0 - (345.0 / 1000.0)), 1.0);  // REQ-F-046
  EXPECT_EQ(view.part("usb", "capacityText")->property("rawText").toString(),
            QLocale().formattedDataSize(static_cast<qint64>(kFree), 0, QLocale::DataSizeSIFormat) + " free of " +
                QLocale().formattedDataSize(static_cast<qint64>(kTerabyte), 0, QLocale::DataSizeSIFormat));
  const QList<std::pair<double, const char*>> thresholds{
      {0.899, "primary"}, {0.90, "accentViolet"}, {0.949, "accentViolet"}, {0.95, "warning"}, {0.999, "warning"}};
  for (const auto& [fraction, role] : thresholds) {
    bar->setProperty("fraction", fraction);
    const auto expected = evaluateIn(bar, QStringLiteral("HoloniightPalette.") + role).value<QColor>();
    EXPECT_EQ(fill->property("color").value<QColor>(), expected) << fraction;  // REQ-F-047, REQ-F-048
  }
}

TEST(PlacesWindow, PlacesQmlComposesNoStockProgressBar) {
  QDirIterator files(QStringLiteral(FILES_SOURCE_DIR "/apps/files/qml/places"), {"*.qml"}, QDir::Files);
  const QRegularExpression instantiation(R"re(\bProgressBar\s*\{)re");
  while (files.hasNext()) {
    QFile file(files.next());
    ASSERT_TRUE(file.open(QIODevice::ReadOnly));
    EXPECT_FALSE(instantiation.match(QString::fromUtf8(file.readAll())).hasMatch())
        << file.fileName().toStdString();  // REQ-C-005
  }
}

TEST(PlacesWindow, SidebarKeysCrossActivateAndRemove) {
  const SeededUserDirs seeded(1);
  DevicesWindow view;
  auto dock = files_test::storageDrive("dock", false);
  dock.canPowerOff = false;
  auto stuck = mountedVolume("stuck", "dock", "/media/stuck");
  stuck.canUnmount = false;  // No verb at all (REQ-F-022).
  ASSERT_TRUE(view.startWith(
      {files_test::storageDrive("stick"), files_test::storageDrive("stick2"), internalDrive("disk"),
       opticalDrive("dvd", true), dock},
      {files_test::storageVolume("usb", "stick"), mountedVolume("usb2", "stick2", "/media/usb2"),
       mountedVolume("internal", "disk", "/mnt/data"), mountedVolume("disc", "dvd", "/media/disc"), stuck}));
  ASSERT_EQ(view.controller.devices()->rowCount(), 5);
  auto* navigator = view.controller.sidebarNavigator();
  const auto lastPlace = view.controller.places()->rowCount() - 1;
  const auto lastDevice = view.controller.devices()->rowCount() - 1;

  // Crossing both ways, with Down/j and Up/k, and refusal at both ends (REQ-F-050..052, REQ-F-054).
  view.sidebar->forceActiveFocus();
  navigator->setCursor(SidebarNavigator::Places, lastPlace);
  EXPECT_TRUE(view.press(Qt::Key_J, "j"));
  EXPECT_EQ(navigator->section(), SidebarNavigator::Devices);
  EXPECT_TRUE(view.press(Qt::Key_K, "k"));
  EXPECT_EQ(navigator->section(), SidebarNavigator::Places);
  EXPECT_TRUE(view.press(Qt::Key_Down));
  EXPECT_TRUE(view.press(Qt::Key_Up));
  EXPECT_EQ(navigator->index(), lastPlace);
  navigator->setCursor(SidebarNavigator::Places, 0);
  EXPECT_FALSE(view.press(Qt::Key_Up));
  EXPECT_FALSE(view.press(Qt::Key_K, "k"));
  navigator->setCursor(SidebarNavigator::Devices, lastDevice);
  EXPECT_FALSE(view.press(Qt::Key_Down));
  EXPECT_FALSE(view.press(Qt::Key_J, "j"));
  EXPECT_EQ(navigator->index(), lastDevice);

  // Activation keys follow the cursor: a place navigates, a device mounts (REQ-F-054).
  itemAtIndex(view.places, lastPlace)->setProperty("path", view.dir.filePath("target"));
  for (const auto key : {Qt::Key_Return, Qt::Key_Enter, Qt::Key_Space}) {
    view.controller.open(view.dir.path());
    ASSERT_TRUE(QTest::qWaitFor([&] { return !view.controller.scanning(); }));
    view.sidebar->forceActiveFocus();
    navigator->setCursor(SidebarNavigator::Places, lastPlace);
    EXPECT_TRUE(view.press(key, key == Qt::Key_Space ? " " : "\r"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return !view.controller.scanning(); }));
    EXPECT_EQ(view.controller.currentPath(), view.dir.filePath("target")) << key;

    view.storage_backend.calls.clear();
    view.focusDevice("usb");
    EXPECT_TRUE(view.press(key, key == Qt::Key_Space ? " " : "\r"));
    ASSERT_TRUE(QTest::qWaitFor([&] { return !view.storage_backend.calls.isEmpty(); })) << key;
    EXPECT_EQ(view.storage_backend.calls.last().operation, StorageOperation::Mount);
    view.finishLastCall("org.freedesktop.UDisks2.Error.Failed");
  }

  // X runs the focused row's one verb (REQ-F-021).
  const QList<std::tuple<const char*, StorageOperation, const char*>> removals{
      {"usb2", StorageOperation::PowerOff, "stick2"},
      {"internal", StorageOperation::Unmount, "internal"},
      {"disc", StorageOperation::Eject, "dvd"}};
  for (const auto& [row, operation, target] : removals) {
    view.storage_backend.calls.clear();
    view.focusDevice(row);
    EXPECT_TRUE(view.press(Qt::Key_X, "x"));
    const auto reached = files_test::removalCall(view.storage_backend);
    EXPECT_EQ(reached.operation, operation) << row;
    EXPECT_EQ(reached.targetId, target) << row;
    if (reached.operation != StorageOperation::Unmount) {
      view.finishLastCall();
    }
    ASSERT_TRUE(QTest::qWaitFor([&] { return !view.storage.busy(target); })) << row;
  }

  // No control, no operation, no error (REQ-F-022); Shift+X stays unhandled (REQ-F-024).
  view.storage_backend.calls.clear();
  view.storage_backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] { return view.controller.devices()->findRow("stuck") >= 0; }));
  view.focusDevice("stuck");
  EXPECT_FALSE(view.part("stuck", "deviceRemoveButton")->isVisible());
  EXPECT_TRUE(view.press(Qt::Key_X, "x"));
  EXPECT_FALSE(view.press(Qt::Key_X, "X", Qt::ShiftModifier));
  QTest::qWait(20);
  EXPECT_TRUE(view.storage_backend.calls.isEmpty());
  EXPECT_TRUE(view.controller.devices()->errorMessage().isEmpty());
}

TEST(PlacesWindow, RemovalKeyIsSuppressedWhileAModeOrPromptOrQuickLookHoldsTheWindow) {
  DevicesWindow view;
  ASSERT_TRUE(view.startWith({files_test::storageDrive("stick")}, {mountedVolume("usb", "stick", "/media/usb")}));
  const auto tryRemove = [&] {
    view.focusDevice("usb");
    // Acceptance varies (a prompt's window filter swallows the key); no storage call is the contract.
    std::ignore = view.press(Qt::Key_X, "x");
    QTest::qWait(20);
    return view.storage_backend.calls.size();
  };
  view.controller.handleKey("j");  // Onto "target/", a real entry for VISUAL and the trash prompt.
  view.controller.handleKey("v");
  EXPECT_EQ(tryRemove(), 0);  // VISUAL (REQ-F-023)
  QTest::keyClick(view.window, Qt::Key_Escape);
  view.controller.handleKey("D");
  ASSERT_TRUE(view.controller.tasks()->hasPrompt());
  EXPECT_EQ(tryRemove(), 0);  // Task prompt
  view.controller.handleKey("n");
  ASSERT_FALSE(view.controller.tasks()->hasPrompt());
  view.controller.handleKey("j");  // file.txt, previewable
  ASSERT_TRUE(QTest::qWaitFor([&] { return view.controller.preview()->quickLookEligible(); }, 5000));
  view.controller.handleKey(" ");
  ASSERT_TRUE(view.controller.quickLookOpen());
  EXPECT_EQ(tryRemove(), 0);  // Quick Look
  view.controller.handleKey("Escape");
  ASSERT_FALSE(view.controller.quickLookOpen());
  EXPECT_EQ(tryRemove(), 1);  // And unsuppressed once all three have cleared.
}

TEST(PlacesWindow, ClickingTheRemovalControlDoesNotAlsoActivateTheRow) {
  DevicesWindow view;
  ASSERT_TRUE(view.startWith({files_test::storageDrive("stick")}, {mountedVolume("usb", "stick", "/media/usb")}));
  ASSERT_TRUE(view.controller.devices()->data(view.controller.devices()->index(0), DevicesModel::CanActivate).toBool());
  const auto location = view.controller.currentPath();
  auto* button = view.part("usb", "deviceRemoveButton");
  ASSERT_NE(button, nullptr);
  const auto point = button->mapToScene(QPointF(button->width() / 2, button->height() / 2)).toPoint();
  QTest::mouseClick(view.window, Qt::LeftButton, Qt::NoModifier, point);
  ASSERT_TRUE(QTest::qWaitFor([&] { return !view.storage_backend.calls.isEmpty(); }));
  QTest::qWait(20);
  ASSERT_EQ(view.storage_backend.calls.size(), 1);  // The power-off's first step: unmounting the volume.
  EXPECT_EQ(view.storage_backend.calls.first().operation, StorageOperation::Unmount);
  EXPECT_EQ(view.controller.currentPath(), location);  // Not opened as well.
  // The row body itself still activates, which opens the mount.
  view.finishLastCall("org.freedesktop.UDisks2.Error.Failed");
  auto* body = view.part("usb", "deviceRowBody");
  QTest::mouseClick(view.window, Qt::LeftButton, Qt::NoModifier,
                    body->mapToScene(QPointF(20, body->height() / 2)).toPoint());
  ASSERT_TRUE(QTest::qWaitFor([&] { return view.controller.currentPath() == "/media/usb"; }));
}

TEST(PlacesWindow, DeviceErrorsReachTheStatusBarNotTheSidebar) {
  DevicesWindow view;
  ASSERT_TRUE(view.startWith({files_test::storageDrive("stick")}, {mountedVolume("usb", "stick", "/media/usb")}));
  EXPECT_EQ(view.find("devicesError"), nullptr);
  view.focusDevice("usb");
  EXPECT_TRUE(view.press(Qt::Key_X, "x"));
  ASSERT_TRUE(QTest::qWaitFor([&] { return !view.storage_backend.calls.isEmpty(); }));
  view.finishLastCall("org.freedesktop.UDisks2.Error.NotAuthorizedDismissed");
  EXPECT_EQ(view.controller.statusMessage(), DevicesModel::tr("Authorization was canceled."));
  for (auto* child : view.sidebar->findChildren<QQuickItem*>()) {
    const auto rawText = child->property("rawText");
    if (rawText.isValid() && child->isVisible()) {
      EXPECT_NE(rawText.toString(), view.controller.statusMessage());
    }
  }
}
