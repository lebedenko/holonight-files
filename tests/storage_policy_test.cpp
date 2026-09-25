#include "storage_policy.h"

#include <QFile>
#include <QRegularExpression>

#include <gtest/gtest.h>

using HoloNight::System::StorageDrive;
using HoloNight::System::StorageOperation;
using StoragePolicy::DeviceClass;

namespace {
StorageDrive drive(const QString& bus, bool mediaRemovable, bool canEject, bool canPowerOff) {
  StorageDrive record;
  record.connectionBus = bus;
  record.mediaRemovable = mediaRemovable;
  record.canEject = canEject;
  record.canPowerOff = canPowerOff;
  return record;
}
}  // namespace

TEST(StoragePolicy, OpticalWinsOverAnyBus) {
  for (const auto* bus : {"", "usb"}) {
    auto record = drive(QString::fromLatin1(bus), true, true, false);
    record.optical = true;
    EXPECT_EQ(StoragePolicy::classify(record), DeviceClass::Optical) << bus;
  }
}

TEST(StoragePolicy, AnyNonEmptyBusIsExternal) {
  for (const auto* bus : {"usb", "ieee1394", "sdio", "a-bus-this-source-never-names"}) {
    EXPECT_EQ(StoragePolicy::classify(drive(QString::fromLatin1(bus), false, false, false)), DeviceClass::External)
        << bus;
  }
}

TEST(StoragePolicy, EmptyBusIsInternalWhateverTheCapabilities) {
  auto record = drive({}, true, true, true);
  record.removable = true;
  EXPECT_EQ(StoragePolicy::classify(record), DeviceClass::Internal);
}

TEST(StoragePolicy, RemovalVerbPrefersByWhatLeavesTheMachine) {
  EXPECT_EQ(StoragePolicy::removalVerb(drive("usb", true, true, true), true), StorageOperation::Eject);
  EXPECT_EQ(StoragePolicy::removalVerb(drive("usb", false, true, true), true), StorageOperation::PowerOff);
}

TEST(StoragePolicy, RemovalVerbFallsBackToEjectOnlyForSelfRemovableDrives) {
  EXPECT_EQ(StoragePolicy::removalVerb(drive("usb", false, true, false), true), StorageOperation::Eject);
  EXPECT_EQ(StoragePolicy::removalVerb(drive("usb", true, false, true), true), StorageOperation::Unmount);
  EXPECT_EQ(StoragePolicy::removalVerb(drive("usb", true, false, true), false), std::nullopt);
}

TEST(StoragePolicy, RemovalVerbFallsBackToUnmountThenNothing) {
  // An externally attached drive the provider cannot power off (eSATA dock, Thunderbolt NVMe).
  EXPECT_EQ(StoragePolicy::removalVerb(drive("usb", false, false, false), true), StorageOperation::Unmount);
  EXPECT_EQ(StoragePolicy::removalVerb(drive("usb", false, false, false), false), std::nullopt);
}

TEST(StoragePolicy, InternalDrivesOnlyEverUnmount) {
  EXPECT_EQ(StoragePolicy::removalVerb(drive({}, false, true, true), true), StorageOperation::Unmount);
  EXPECT_EQ(StoragePolicy::removalVerb(drive({}, true, true, true), true), StorageOperation::Unmount);
  EXPECT_EQ(StoragePolicy::removalVerb(drive({}, false, true, true), false), std::nullopt);
}

TEST(StoragePolicy, MultiSlotReaderNeverYieldsPowerOff) {
  for (int slot = 0; slot < 4; ++slot) {
    auto record = drive("usb", true, true, true);
    record.siblingId = "/sys/devices/reader";
    record.mediaPresent = slot % 2 == 0;
    for (const bool canEject : {true, false}) {
      record.canEject = canEject;
      for (const bool canUnmount : {true, false}) {
        EXPECT_NE(StoragePolicy::removalVerb(record, canUnmount), StorageOperation::PowerOff) << slot;
      }
    }
  }
}

TEST(StoragePolicy, IconNamePerClassification) {
  auto optical = drive("usb", true, true, false);
  optical.optical = true;
  EXPECT_EQ(StoragePolicy::iconName(optical), "media-optical-symbolic");
  EXPECT_EQ(StoragePolicy::iconName(drive("usb", true, true, true)), "media-flash-symbolic");
  EXPECT_EQ(StoragePolicy::iconName(drive("usb", false, true, true)), "drive-removable-media-symbolic");
  EXPECT_EQ(StoragePolicy::iconName(drive({}, false, false, false)), "drive-harddisk-symbolic");
}

// REQ-C-001: no hardware-specific identifier and no bus literal may steer classification.
TEST(StoragePolicy, SourceReadsNoHardwareSpecificIdentifiers) {
  QFile file(QStringLiteral(FILES_SOURCE_DIR "/apps/files/storage/storage_policy.h"));
  ASSERT_TRUE(file.open(QIODevice::ReadOnly));
  const auto source = QString::fromUtf8(file.readAll());
  const QRegularExpression forbidden(
      R"re(\b(vendor|model|serial|siblingId|device|mountPoints)\b|"(usb|ieee1394|sdio)")re");
  const auto match = forbidden.match(source);
  EXPECT_FALSE(match.hasMatch()) << match.captured().toStdString();
}
