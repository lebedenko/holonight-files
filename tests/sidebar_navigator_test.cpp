#include "sidebar_navigator.h"

#include "devices_model.h"
#include "directory_fixtures.h"
#include "places_model.h"
#include "settings_fixtures.h"
#include "storage_fixtures.h"

#include <QSignalSpy>

#include <gtest/gtest.h>
#include <memory>

using files_test::fixturePattern;

namespace {
// Home plus two user dirs, and as many device rows as asked for.
struct Sidebar {
  QTemporaryDir home{fixturePattern("navigator-home")};
  QTemporaryDir config{fixturePattern("navigator-config")};
  files_test::FakeStorage backend;
  HoloNight::System::StorageController controller{&backend};
  std::unique_ptr<PlacesModel> places;
  DevicesModel devices{&controller, nullptr, std::make_shared<files_test::FakeCapacityProbe>()};
  std::unique_ptr<SidebarNavigator> navigator;
  explicit Sidebar(int deviceRows) {
    files_test::writeFile(config, "user-dirs.dirs",
                          "XDG_DOCUMENTS_DIR=\"" + home.filePath("docs").toUtf8() + "\"\nXDG_MUSIC_DIR=\"" +
                              home.filePath("music").toUtf8() + "\"\n");
    places =
        std::make_unique<PlacesModel>(home.path(), config.filePath("user-dirs.dirs"), config.filePath("places.toml"),
                                      std::make_shared<files_test::FakePlaceAvailabilityChecker>(),
                                      std::make_shared<files_test::RecordingWarningSink>(), nullptr);
    setDevices(deviceRows);
    navigator = std::make_unique<SidebarNavigator>(places.get(), &devices);
  }
  void setDevices(int rows) {
    backend.drives = {files_test::storageDrive("stick")};
    backend.volumes.clear();
    for (int row = 0; row < rows; ++row) {
      backend.volumes.append(files_test::storageVolume(QStringLiteral("v%1").arg(row), "stick"));
    }
    backend.publish();
  }
  [[nodiscard]] int lastPlace() const { return places->rowCount() - 1; }
  // REQ-F-049: exactly one (section, index) pair is set.
  [[nodiscard]] bool oneCursor() const {
    const auto section = navigator->section();
    const auto count = section == SidebarNavigator::Places ? places->rowCount() : devices.rowCount();
    return section != SidebarNavigator::None && navigator->index() >= 0 && navigator->index() < count;
  }
};
}  // namespace

TEST(SidebarNavigator, DownFromTheLastPlaceCrossesIntoDevices) {
  Sidebar sidebar(2);
  ASSERT_EQ(sidebar.places->rowCount(), 3);
  sidebar.navigator->setCursor(SidebarNavigator::Places, sidebar.lastPlace());
  QSignalSpy reveals(sidebar.navigator.get(), &SidebarNavigator::revealRequested);
  EXPECT_TRUE(sidebar.navigator->moveDown());
  EXPECT_EQ(sidebar.navigator->section(), SidebarNavigator::Devices);
  EXPECT_EQ(sidebar.navigator->index(), 0);
  EXPECT_TRUE(sidebar.oneCursor());
  ASSERT_EQ(reveals.size(), 1);
  EXPECT_EQ(reveals.first().at(0).toInt(), SidebarNavigator::Devices);
  EXPECT_EQ(reveals.first().at(1).toInt(), 0);
}

TEST(SidebarNavigator, UpFromTheFirstDeviceCrossesIntoPlaces) {
  Sidebar sidebar(2);
  sidebar.navigator->setCursor(SidebarNavigator::Devices, 0);
  EXPECT_TRUE(sidebar.navigator->moveUp());
  EXPECT_EQ(sidebar.navigator->section(), SidebarNavigator::Places);
  EXPECT_EQ(sidebar.navigator->index(), sidebar.lastPlace());
  EXPECT_TRUE(sidebar.oneCursor());
}

TEST(SidebarNavigator, BoundariesWithoutADestinationRefuseAndStayPut) {
  Sidebar sidebar(2);
  QSignalSpy reveals(sidebar.navigator.get(), &SidebarNavigator::revealRequested);
  sidebar.navigator->setCursor(SidebarNavigator::Devices, 1);
  EXPECT_FALSE(sidebar.navigator->moveDown());
  EXPECT_EQ(sidebar.navigator->section(), SidebarNavigator::Devices);
  EXPECT_EQ(sidebar.navigator->index(), 1);
  sidebar.navigator->setCursor(SidebarNavigator::Places, 0);
  EXPECT_FALSE(sidebar.navigator->moveUp());
  EXPECT_EQ(sidebar.navigator->section(), SidebarNavigator::Places);
  EXPECT_EQ(sidebar.navigator->index(), 0);
  EXPECT_TRUE(reveals.isEmpty());

  Sidebar empty(0);
  ASSERT_EQ(empty.devices.rowCount(), 0);
  empty.navigator->setCursor(SidebarNavigator::Places, empty.lastPlace());
  EXPECT_FALSE(empty.navigator->moveDown());
  EXPECT_EQ(empty.navigator->section(), SidebarNavigator::Places);
  EXPECT_EQ(empty.navigator->index(), empty.lastPlace());
}

TEST(SidebarNavigator, EveryStepKeepsExactlyOneCursor) {
  Sidebar sidebar(3);
  sidebar.navigator->setCursor(SidebarNavigator::Places, 0);
  QSignalSpy reveals(sidebar.navigator.get(), &SidebarNavigator::revealRequested);
  int moves = 0;
  while (sidebar.navigator->moveDown()) {
    ++moves;
    EXPECT_TRUE(sidebar.oneCursor());
  }
  while (sidebar.navigator->moveUp()) {
    ++moves;
    EXPECT_TRUE(sidebar.oneCursor());
  }
  EXPECT_EQ(moves, 2 * (sidebar.places->rowCount() + sidebar.devices.rowCount() - 1));
  EXPECT_EQ(reveals.size(), moves);
}

TEST(SidebarNavigator, CursorClampsWhenAModelShrinksBeneathIt) {
  Sidebar sidebar(3);
  sidebar.navigator->setCursor(SidebarNavigator::Devices, 2);
  sidebar.setDevices(1);
  EXPECT_EQ(sidebar.navigator->section(), SidebarNavigator::Devices);
  EXPECT_EQ(sidebar.navigator->index(), 0);
  sidebar.setDevices(0);
  EXPECT_EQ(sidebar.navigator->section(), SidebarNavigator::Places);
  EXPECT_EQ(sidebar.navigator->index(), sidebar.lastPlace());
  EXPECT_TRUE(sidebar.oneCursor());
}
