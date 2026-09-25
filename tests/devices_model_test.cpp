#include "devices_model.h"

#include "devices_model_test_access.h"
#include "directory_controller.h"
#include "engine_setup.h"
#include "storage_fixtures.h"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QFile>
#include <QLocale>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QScopeGuard>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <StorageBackend.h>
#include <gtest/gtest.h>
#include <memory>
#include <utility>

using namespace HoloNight::System;
using files_test::FakeCapacityProbe;
using files_test::FakeStorage;
namespace {
StorageDrive device(QString targetId = "drive", bool removable = true, bool media = true) {
  return files_test::storageDrive(std::move(targetId), removable, media);
}
StorageVolume volume(const QString& targetId = "volume", QString drive = "drive") {
  return files_test::storageVolume(targetId, std::move(drive));
}
StorageDrive internalDrive(QString targetId) {
  auto record = device(std::move(targetId), false);
  record.connectionBus.clear();
  record.canPowerOff = false;
  return record;
}
StorageDrive opticalDrive(QString targetId, bool media) {
  auto record = device(std::move(targetId), true, media);
  record.optical = true;
  record.mediaRemovable = true;
  record.canEject = true;
  record.canPowerOff = false;
  return record;
}
// One slot of a multi-LUN card reader: every slot shares the reader's SiblingId.
StorageDrive readerSlot(int slot, bool media) {
  auto record = device(QStringLiteral("slot%1").arg(slot), true, media);
  record.siblingId = "/sys/devices/pci0000:00/usb1/1-2/1-2:1.0";
  record.mediaRemovable = true;
  record.canEject = true;
  record.canPowerOff = true;
  return record;
}
// Every model under test measures through a fake probe (REQ-C-006).
struct Devices {
  FakeStorage backend;
  StorageController controller{&backend};
  std::shared_ptr<FakeCapacityProbe> probe = std::make_shared<FakeCapacityProbe>();
  DevicesModel model{&controller, nullptr, probe};
  [[nodiscard]] QVariant value(int row, DevicesModel::Role role) const { return model.data(model.index(row), role); }
  [[nodiscard]] QVariant value(const QString& targetId, DevicesModel::Role role) const {
    return value(model.findRow(targetId), role);
  }
};
}  // namespace
TEST(FilesStorage, FiltersInfrastructureWithoutHidingHintSystemData) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller, nullptr, std::make_shared<FakeCapacityProbe>());
  QAbstractItemModelTester tester(&model, QAbstractItemModelTester::FailureReportingMode::Fatal);
  backend.drives = {device()};
  auto data = volume();
  data.hintSystem = true;
  data.mountPoints = {"/mnt/backup"};
  auto root = volume("root");
  root.mountPoints = {"/"};
  auto home = volume("home");
  home.mountPoints = {"/home"};
  auto efi = volume("efi");
  efi.partitionType = "c12a7328-f81f-11d2-ba4b-00a0c93ec93b";
  auto swap = volume("swap");
  swap.usage = "other";
  swap.filesystemType = "swap";
  auto ignored = volume("ignored");
  ignored.hintIgnore = true;
  auto loop = volume("loop");
  loop.loop = true;
  auto container = volume("container");
  container.partitionContainer = true;
  backend.volumes = {data, root, home, efi, swap, ignored, loop, container};
  backend.publish();
  ASSERT_EQ(model.count(), 1);
  EXPECT_EQ(model.data(model.index(0), DevicesModel::TargetId).toString(), "volume");
}
TEST(FilesStorage, PreservesMultipleVolumesAndDoesNotDuplicateUnlockedBacking) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller, nullptr, std::make_shared<FakeCapacityProbe>());
  backend.drives = {device()};
  auto locked = volume("locked");
  locked.usage = "crypto";
  locked.locked = true;
  locked.canMount = false;
  backend.volumes = {volume(), locked};
  backend.publish();
  EXPECT_EQ(model.count(), 2);
  for (int row = 0; row < model.count(); ++row) {
    if (model.data(model.index(row), DevicesModel::TargetId).toString() == "locked") {
      EXPECT_FALSE(model.data(model.index(row), DevicesModel::CanActivate).toBool());
    }
  }
  locked.locked = false;
  auto clear = volume("clear");
  clear.cryptoBackingId = locked.id;
  backend.volumes = {volume(), locked, clear};
  backend.publish();
  EXPECT_EQ(model.count(), 2);
}
// A newly appearing volume changes the scope after invocation; the controller rejects it before
// unmounting anything (REQ-F-026, REQ-F-027). Cross-drive scope is blocked by the Files policy.
TEST(FilesStorage, PowerOffRejectsChangedVolumeScopeWithoutPartialUnmount) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller, nullptr, std::make_shared<FakeCapacityProbe>());
  auto first = device("first");
  first.siblingId = "physical";
  auto visible = volume("visible", "first");
  visible.mountPoints = {"/media/visible"};
  auto hidden = volume("hidden", "first");
  hidden.hintIgnore = true;
  hidden.mountPoints = {"/media/hidden"};
  backend.drives = {first};
  backend.volumes = {visible, hidden};
  backend.publish();
  ASSERT_EQ(model.data(model.index(model.findRow("visible")), DevicesModel::RemovalVerb).toInt(),
            DevicesModel::PowerOff);
  EXPECT_TRUE(controller.removalScope("first", true).contains("hidden"));
  model.remove("visible");
  backend.volumes.append(volume("new", "first"));
  backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !model.errorMessage().isEmpty(); }));
  EXPECT_EQ(model.errorMessage(), DevicesModel::tr("The affected devices changed. Review them before trying again."));
  EXPECT_TRUE(backend.calls.isEmpty());
  for (const auto& record : controller.volumes()->items()) {
    if (record.id != "new") {
      EXPECT_FALSE(record.mountPoints.isEmpty()) << record.id.toStdString();
    }
  }
}
TEST(FilesStorage, PowerOffAgainstAnUnchangedScopeReachesTheDrive) {
  Devices devices;
  auto first = device("first");
  first.siblingId = "physical";
  auto hidden = volume("hidden", "first");
  hidden.hintIgnore = true;
  devices.backend.drives = {first};
  auto visible = volume("visible", "first");
  visible.mountPoints = {"/media/visible"};
  devices.backend.volumes = {visible, hidden};
  devices.backend.publish();
  devices.model.remove("visible");
  const auto reached = files_test::removalCall(devices.backend);
  EXPECT_EQ(reached.operation, StorageOperation::PowerOff);
  EXPECT_EQ(reached.targetId, "first");
  EXPECT_TRUE(devices.model.errorMessage().isEmpty());
}

// Amended udisks2-storage R3 (REQ-F-016): an empty non-optical slot no longer shows.
TEST(FilesStorage, FiltersFixedDisksAndEmptyReadersAccordingToConsumerPolicy) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller, nullptr, std::make_shared<FakeCapacityProbe>());
  backend.drives = {device("empty", true, false), device("fixed", false), device("external")};
  auto mounted = volume("mounted", "fixed");
  mounted.mountPoints = {"/mnt/backup"};
  backend.volumes = {mounted, volume("unmounted", "fixed"), volume("usb", "external")};
  backend.publish();
  EXPECT_EQ(model.count(), 2);
  EXPECT_EQ(model.findRow("empty"), -1);
}
TEST(FilesStorage, MountOpensOnlyForCurrentActivationAndPreservesLocationOnFailure) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller, nullptr, std::make_shared<FakeCapacityProbe>());
  backend.drives = {device()};
  backend.volumes = {volume()};
  backend.publish();
  QSignalSpy opens(&model, &DevicesModel::openRequested);
  model.activate("volume");
  QCoreApplication::processEvents();
  ASSERT_EQ(backend.calls.size(), 1);
  model.navigationChanged("/somewhere/else");
  auto result = backend.calls.last();
  result.mountPath = "/media/Data";
  emit backend.operationFinished(result);
  EXPECT_TRUE(opens.isEmpty());
  model.activate("volume");
  QCoreApplication::processEvents();
  ASSERT_EQ(backend.calls.size(), 2);
  result = backend.calls.last();
  result.errorName = "org.freedesktop.UDisks2.Error.DeviceBusy";
  emit backend.operationFinished(result);
  EXPECT_TRUE(opens.isEmpty());
  model.activate("volume");
  QCoreApplication::processEvents();
  result = backend.calls.last();
  result.mountPath = "/media/Data";
  backend.volumes[0].mountPoints = {result.mountPath};
  backend.publish();
  emit backend.operationFinished(result);
  ASSERT_EQ(opens.size(), 1);
  EXPECT_EQ(opens.first().first().toString(), "/media/Data");
}
TEST(FilesStorage, ModalGuardSuppressesActionsAndInvalidatesPendingActivation) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller, nullptr, std::make_shared<FakeCapacityProbe>());
  backend.drives = {device()};
  backend.volumes = {volume()};
  backend.publish();
  QSignalSpy opens(&model, &DevicesModel::openRequested);
  model.activate("volume");
  QCoreApplication::processEvents();
  ASSERT_EQ(backend.calls.size(), 1);
  model.setInteractionEnabled(false);
  model.activate("volume");
  model.remove("volume");
  model.setInteractionEnabled(true);
  auto result = backend.calls.last();
  result.mountPath = "/media/Data";
  emit backend.operationFinished(result);
  EXPECT_TRUE(opens.isEmpty());
  EXPECT_EQ(backend.calls.size(), 1);
  // Removal is guarded the same way once nothing is in flight (REQ-F-023).
  backend.volumes[0].mountPoints = {"/media/Data"};  // Removal needs a mounted volume.
  backend.publish();
  ASSERT_NE(model.data(model.index(0), DevicesModel::RemovalVerb).toInt(), DevicesModel::NoVerb);
  model.setInteractionEnabled(false);
  model.remove("volume");
  QCoreApplication::processEvents();
  EXPECT_EQ(backend.calls.size(), 1);
  model.setInteractionEnabled(true);
  model.remove("volume");
  ASSERT_TRUE(QTest::qWaitFor([&] { return backend.calls.size() == 2; }));
}
TEST(FilesStorage, UnmountRecoversOnlyWhenCurrentLocationIsWithinAffectedMount) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller, nullptr, std::make_shared<FakeCapacityProbe>());
  backend.drives = {device()};
  auto record = volume();
  record.mountPoints = {"/media/Data"};
  backend.volumes = {record};
  backend.publish();
  model.navigationChanged("/media/Data/folder");
  QSignalSpy recovery(&model, &DevicesModel::recoveryRequested);
  backend.volumes[0].mountPoints.clear();
  backend.publish();
  EXPECT_EQ(recovery.size(), 1);
  backend.volumes = {record};
  backend.publish();
  model.navigationChanged("/media/Database/folder");
  backend.volumes.clear();
  backend.publish();
  EXPECT_EQ(recovery.size(), 1);
}

TEST(FilesStorage, DirectoryControllerMountNavigationAndRemovalRecoverThroughNormalNavigation) {
  FakeStorage backend;
  StorageController storage(&backend);
  DirectoryController controller(&storage, nullptr, std::make_shared<FakeCapacityProbe>());
  QTemporaryDir initial;
  QTemporaryDir mounted;
  ASSERT_TRUE(initial.isValid());
  ASSERT_TRUE(mounted.isValid());
  backend.drives = {device()};
  backend.volumes = {volume()};
  backend.publish();
  controller.open(initial.path());
  controller.devices()->activate("volume");
  ASSERT_TRUE(QTest::qWaitFor([&] { return backend.calls.size() == 1; }));
  auto result = backend.calls.last();
  result.mountPath = mounted.path();
  backend.volumes[0].mountPoints = {mounted.path()};
  backend.publish();
  emit backend.operationFinished(result);
  EXPECT_EQ(controller.currentPath(), mounted.path());
  backend.volumes.clear();
  backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] { return controller.currentPath() == QDir::homePath(); }));
  EXPECT_TRUE(controller.statusMessage().contains("unmounted"));
  EXPECT_FALSE(controller.quickLookOpen());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !controller.scanning(); }));
  EXPECT_EQ(controller.currentPath(), QDir::homePath());
}

TEST(FilesStorage, DevicesPanelLoadsWithPopulatedModel) {
  FakeStorage backend;
  StorageController storage(&backend);
  DirectoryController controller(&storage, nullptr, std::make_shared<FakeCapacityProbe>());
  backend.drives = {device()};
  backend.volumes = {volume()};
  backend.publish();
  QQmlEngine engine;
  initializeFilesEngine(engine);
  QQmlComponent component(&engine);
  component.loadFromModule("HolonightFiles", "DevicesPanel");
  ASSERT_TRUE(component.isReady()) << component.errorString().toStdString();
  std::unique_ptr<QObject> panel(
      component.createWithInitialProperties({{"controller", QVariant::fromValue(&controller)}}));
  ASSERT_NE(panel, nullptr) << component.errorString().toStdString();
}

TEST(FilesStorage, SuccessfulReplyAfterExternalUnmountDoesNotOpenUnderlyingDirectory) {
  FakeStorage backend;
  StorageController storage(&backend);
  DevicesModel model(&storage, nullptr, std::make_shared<FakeCapacityProbe>());
  backend.drives = {device()};
  backend.volumes = {volume()};
  backend.publish();
  QSignalSpy opens(&model, &DevicesModel::openRequested);
  model.activate("volume");
  QCoreApplication::processEvents();
  ASSERT_EQ(backend.calls.size(), 1);
  auto result = backend.calls.last();
  result.mountPath = "/media/Data";
  emit backend.operationFinished(result);
  EXPECT_TRUE(opens.isEmpty());
  EXPECT_FALSE(model.errorMessage().isEmpty());
}

TEST(FilesStorage, CanonicalLocationTracksSymlinkedMountAndRejectsStaleResolution) {
  FakeStorage backend;
  StorageController storage(&backend);
  DevicesModel model(&storage, nullptr, std::make_shared<FakeCapacityProbe>());
  backend.drives = {device()};
  auto mounted = volume();
  mounted.mountPoints = {"/media/Data"};
  backend.volumes = {mounted};
  backend.publish();
  QSignalSpy recovery(&model, &DevicesModel::recoveryRequested);
  model.navigationChanged("/home/test/link/folder");
  model.resolvedLocation("/superseded", "/media/Data/folder");
  backend.volumes.clear();
  backend.publish();
  EXPECT_TRUE(recovery.isEmpty());
  backend.volumes = {mounted};
  backend.publish();
  model.resolvedLocation("/home/test/link/folder", "/media/Data/folder");
  backend.volumes.clear();
  backend.publish();
  EXPECT_EQ(recovery.size(), 1);
}

// --- device-actions: removal (T-003) ---

TEST(FilesStorage, RemoveOnARowWithoutAVerbIsInert) {
  Devices devices;
  auto dock = device("dock", false);
  dock.canPowerOff = false;  // External, no drive-level capability...
  auto data = volume("data", "dock");
  data.mountPoints = {"/media/data"};
  data.canUnmount = false;  // ...and nothing to unmount either (REQ-F-008).
  devices.backend.drives = {dock};
  devices.backend.volumes = {data};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 1);
  EXPECT_EQ(devices.value("data", DevicesModel::RemovalVerb).toInt(), DevicesModel::NoVerb);
  EXPECT_TRUE(devices.value("data", DevicesModel::RemovalLabel).toString().isEmpty());
  devices.model.remove("data");
  QTest::qWait(20);
  EXPECT_TRUE(devices.backend.calls.isEmpty());
  EXPECT_TRUE(devices.model.errorMessage().isEmpty());
}

TEST(FilesStorage, RemoveOnABusyRowRecordsNoFurtherRequest) {
  Devices devices;
  devices.backend.drives = {internalDrive("disk")};
  auto data = volume("data", "disk");
  data.mountPoints = {"/mnt/data"};
  devices.backend.volumes = {data};
  devices.backend.publish();
  devices.model.remove("data");
  devices.model.remove("data");  // Before the row has refreshed to busy.
  ASSERT_TRUE(QTest::qWaitFor([&] { return devices.value("data", DevicesModel::Busy).toBool(); }));
  devices.model.remove("data");
  QTest::qWait(20);
  EXPECT_EQ(devices.backend.calls.size(), 1);
}

TEST(FilesStorage, RemoveDispatchesTheRowsSingleVerb) {
  Devices devices;
  auto disk = internalDrive("disk");
  disk.canEject = true;  // Advertised, and still never used for an internal drive (REQ-F-009).
  disk.canPowerOff = true;
  auto internal = volume("internal", "disk");
  internal.mountPoints = {"/mnt/data"};
  auto disc = volume("disc", "dvd");
  disc.mountPoints = {"/media/disc"};
  auto usb = volume("usb", "stick");
  usb.mountPoints = {"/media/usb"};
  devices.backend.drives = {disk, opticalDrive("dvd", true), device("stick")};
  devices.backend.volumes = {internal, disc, usb};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 3);
  EXPECT_EQ(devices.value("internal", DevicesModel::RemovalVerb).toInt(), DevicesModel::Unmount);
  EXPECT_EQ(devices.value("disc", DevicesModel::RemovalVerb).toInt(), DevicesModel::Eject);
  EXPECT_EQ(devices.value("usb", DevicesModel::RemovalVerb).toInt(), DevicesModel::PowerOff);
  const QList<std::pair<QString, StorageResult>> expected{
      {"internal", {.targetId = "internal", .operation = StorageOperation::Unmount}},
      {"disc", {.targetId = "dvd", .operation = StorageOperation::Eject}},
      {"usb", {.targetId = "stick", .operation = StorageOperation::PowerOff}}};
  for (const auto& [row, call] : expected) {
    devices.backend.calls.clear();
    devices.model.remove(row);
    const auto reached = files_test::removalCall(devices.backend);
    EXPECT_EQ(reached.targetId, call.targetId) << row.toStdString();
    EXPECT_EQ(reached.operation, call.operation) << row.toStdString();
    if (reached.operation != StorageOperation::Unmount) {
      emit devices.backend.operationFinished(reached);
    }
    ASSERT_TRUE(QTest::qWaitFor([&] { return !devices.controller.busy(call.targetId); }));
  }
}

TEST(FilesStorage, ModelExposesNoConfirmationAndNoMountEntryPoint) {
  const auto& meta = DevicesModel::staticMetaObject;
  EXPECT_EQ(meta.indexOfProperty("confirmationText"), -1);  // REQ-F-025
  for (const auto* name : {"mount", "unmount", "eject", "requestPowerOff", "confirmPowerOff", "cancelPowerOff"}) {
    for (int index = meta.methodOffset(); index < meta.methodCount(); ++index) {
      EXPECT_NE(meta.method(index).name(), QByteArray(name));  // REQ-F-012
    }
  }
  EXPECT_NE(meta.indexOfMethod("remove(QString)"), -1);
}

TEST(FilesStorage, EveryOperationErrorReachesTheErrorLabel) {
  Devices devices;
  auto usb = volume("usb", "stick");
  usb.mountPoints = {"/media/usb"};
  devices.backend.drives = {device("stick")};
  devices.backend.volumes = {usb};
  devices.backend.publish();
  const QList<std::pair<QString, QString>> branches{
      {"org.freedesktop.UDisks2.Error.NotAuthorizedDismissed", DevicesModel::tr("Authorization was canceled.")},
      {"org.freedesktop.UDisks2.Error.NotAuthorized",
       DevicesModel::tr("Permission to access this storage was denied.")},
      {"org.freedesktop.UDisks2.Error.DeviceBusy",
       DevicesModel::tr("The device is busy. Close files using it and try again.")},
      {"org.holonight.Storage.ScopeChanged",
       DevicesModel::tr("The affected devices changed. Review them before trying again.")},
      {"org.holonight.Storage.Unavailable", DevicesModel::tr("The storage device or service is no longer available.")},
      {"org.holonight.Storage.Disappeared", DevicesModel::tr("The storage device or service is no longer available.")},
      {"org.freedesktop.UDisks2.Error.Failed", DevicesModel::tr("Storage operation failed: %1").arg("boom")}};
  for (const auto& [errorName, message] : branches) {
    devices.backend.calls.clear();
    devices.model.remove("usb");
    ASSERT_TRUE(QTest::qWaitFor([&] { return !devices.backend.calls.isEmpty(); }));
    auto result = devices.backend.calls.first();
    result.errorName = errorName;
    result.errorMessage = errorName.endsWith("Failed") ? "boom" : QString();
    emit devices.backend.operationFinished(result);
    EXPECT_EQ(devices.model.errorMessage(), message) << errorName.toStdString();
    ASSERT_TRUE(QTest::qWaitFor([&] { return !devices.value("usb", DevicesModel::Busy).toBool(); }));
  }
}

// --- device-actions: visibility and grouping (T-004) ---

TEST(FilesStorage, MultiSlotReaderShowsOnlySlotsHoldingACard) {
  Devices devices;
  for (int slot = 0; slot < 4; ++slot) {
    devices.backend.drives.append(readerSlot(slot, false));
  }
  devices.backend.publish();
  EXPECT_EQ(devices.model.count(), 0);  // REQ-F-016
  devices.backend.drives[2].mediaPresent = true;
  auto card = volume("card", "slot2");
  card.mountPoints = {"/media/card"};
  devices.backend.volumes = {card};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 1);
  EXPECT_EQ(devices.value(0, DevicesModel::TargetId).toString(), "card");
  // Per-slot Eject, never the sibling-wide power-off (REQ-F-004, REQ-F-011).
  EXPECT_EQ(devices.value(0, DevicesModel::RemovalVerb).toInt(), DevicesModel::Eject);
  EXPECT_EQ(devices.value(0, DevicesModel::IconName).toString(), "media-flash-symbolic");
}

TEST(FilesStorage, ReaderWithoutEjectUnmountsItsCardWithoutPoweringOffSiblings) {
  Devices devices;
  auto first = readerSlot(0, true);
  first.canEject = false;
  devices.backend.drives = {first, readerSlot(1, false)};
  auto card = volume("card", first.id);
  card.mountPoints = {"/media/card"};
  devices.backend.volumes = {card};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 1);
  EXPECT_EQ(devices.value("card", DevicesModel::RemovalVerb).toInt(), DevicesModel::Unmount);
  devices.model.remove("card");
  const auto reached = files_test::removalCall(devices.backend);
  EXPECT_EQ(reached.operation, StorageOperation::Unmount);
  EXPECT_EQ(reached.targetId, "card");
}

TEST(FilesStorage, SharedPowerOffScopeFallsBackToRowLocalUnmount) {
  Devices devices;
  auto first = device("enclosure-a");
  auto second = device("enclosure-b");
  first.siblingId = second.siblingId = "shared-enclosure";
  auto data = volume("data", first.id);
  data.mountPoints = {"/media/data"};
  devices.backend.drives = {first, second};
  devices.backend.volumes = {data};
  devices.backend.publish();
  ASSERT_GE(devices.model.findRow("data"), 0);
  EXPECT_EQ(devices.value("data", DevicesModel::RemovalVerb).toInt(), DevicesModel::Unmount);
  devices.model.remove("data");
  const auto reached = files_test::removalCall(devices.backend);
  EXPECT_EQ(reached.operation, StorageOperation::Unmount);
  EXPECT_EQ(reached.targetId, "data");
}

TEST(FilesStorage, StalePowerOffRowCannotBypassSharedScopeGuard) {
  Devices devices;
  auto first = device("enclosure-a");
  first.siblingId = "shared-enclosure";
  auto data = volume("data", first.id);
  data.mountPoints = {"/media/data"};
  devices.backend.drives = {first};
  devices.backend.volumes = {data};
  devices.backend.publish();
  ASSERT_EQ(devices.value("data", DevicesModel::RemovalVerb).toInt(), DevicesModel::PowerOff);

  auto second = device("enclosure-b");
  second.siblingId = first.siblingId;
  devices.backend.drives.append(second);
  emit devices.backend.snapshotChanged(devices.backend.drives, devices.backend.volumes, true);
  ASSERT_TRUE(devices.controller.removalScope(first.id, true).contains(second.id));
  ASSERT_EQ(devices.value("data", DevicesModel::RemovalVerb).toInt(), DevicesModel::PowerOff);
  devices.model.remove("data");
  EXPECT_TRUE(devices.backend.calls.isEmpty());
  ASSERT_TRUE(QTest::qWaitFor(
      [&] { return devices.value("data", DevicesModel::RemovalVerb).toInt() == DevicesModel::Unmount; }));
}

TEST(FilesStorage, EmptyOpticalDriveKeepsAnEjectOnlyRow) {
  Devices devices;
  devices.backend.drives = {opticalDrive("dvd", false)};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 1);  // REQ-F-017
  EXPECT_EQ(devices.value(0, DevicesModel::TargetId).toString(), "dvd");
  EXPECT_EQ(devices.value(0, DevicesModel::RemovalVerb).toInt(), DevicesModel::Eject);
  EXPECT_FALSE(devices.value(0, DevicesModel::CanActivate).toBool());
  EXPECT_FALSE(devices.value(0, DevicesModel::CapacityValid).toBool());
  EXPECT_EQ(devices.value(0, DevicesModel::IconName).toString(), "media-optical-symbolic");
  devices.model.remove("dvd");
  ASSERT_TRUE(QTest::qWaitFor([&] { return !devices.backend.calls.isEmpty(); }));
  EXPECT_EQ(devices.backend.calls.first().operation, StorageOperation::Eject);
  EXPECT_EQ(devices.backend.calls.first().targetId, "dvd");
}

TEST(FilesStorage, EmptyOpticalDriveWithoutEjectOffersNoRemoval) {
  Devices devices;
  auto dvd = opticalDrive("dvd", false);
  dvd.canEject = false;
  dvd.canPowerOff = true;
  devices.backend.drives = {dvd};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 1);
  EXPECT_EQ(devices.value(0, DevicesModel::RemovalVerb).toInt(), DevicesModel::NoVerb);
  EXPECT_FALSE(devices.value(0, DevicesModel::CanActivate).toBool());
  devices.model.remove("dvd");
  EXPECT_TRUE(devices.backend.calls.isEmpty());
}

TEST(FilesStorage, UnmountedInternalPartitionIsHidden) {
  Devices devices;
  auto bay = internalDrive("bay");
  bay.removable = true;  // A hot-swap bay may report removable; it is still internal (REQ-F-015).
  devices.backend.drives = {internalDrive("disk"), bay};
  devices.backend.volumes = {volume("data", "disk"), volume("spare", "bay")};
  devices.backend.publish();
  EXPECT_EQ(devices.model.count(), 0);
}

TEST(FilesStorage, OnlyMultiRowDrivesCarryAGroupLabel) {
  Devices devices;
  auto disk = internalDrive("disk");
  disk.model = "Big Disk";
  auto first = volume("a-first", "disk");
  first.mountPoints = {"/mnt/first"};
  auto second = volume("b-second", "disk");
  second.mountPoints = {"/mnt/second"};
  devices.backend.drives = {disk, device("stick")};
  devices.backend.volumes = {first, second, volume("usb", "stick")};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 3);
  EXPECT_EQ(devices.value("a-first", DevicesModel::GroupLabel).toString(), "Big Disk");  // REQ-F-018
  EXPECT_EQ(devices.value("b-second", DevicesModel::GroupLabel).toString(), "Big Disk");
  EXPECT_TRUE(devices.value("a-first", DevicesModel::GroupStart).toBool());
  EXPECT_FALSE(devices.value("b-second", DevicesModel::GroupStart).toBool());
  EXPECT_TRUE(devices.value("usb", DevicesModel::GroupLabel).toString().isEmpty());  // REQ-F-019
  EXPECT_FALSE(devices.value("usb", DevicesModel::GroupStart).toBool());
}

TEST(FilesStorage, RowsOfOneDriveStayAdjacent) {
  Devices devices;
  devices.backend.drives = {device("a"), device("b")};
  devices.backend.volumes = {volume("1", "a"), volume("2", "b"), volume("3", "a"), volume("4", "b")};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 4);
  QStringList drives;
  for (int row = 0; row < devices.model.count(); ++row) {
    drives.append(devices.value(row, DevicesModel::DriveId).toString());
  }
  EXPECT_EQ(drives, (QStringList{"a", "a", "b", "b"}));  // REQ-F-020
}

TEST(FilesStorage, OverlappingDriveIdsKeepEachGroupContiguous) {
  Devices devices;
  devices.backend.drives = {device("ax"), device("a")};
  devices.backend.volumes = {volume("1", "a"), volume("2", "ax"), volume("z", "a")};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 3);
  QStringList drives;
  for (int row = 0; row < devices.model.count(); ++row) {
    drives.append(devices.value(row, DevicesModel::DriveId).toString());
  }
  EXPECT_EQ(drives, (QStringList{"a", "a", "ax"}));
  EXPECT_TRUE(devices.value(0, DevicesModel::GroupStart).toBool());
  EXPECT_FALSE(devices.value(1, DevicesModel::GroupStart).toBool());
  EXPECT_FALSE(devices.value(2, DevicesModel::GroupStart).toBool());
}

// --- device-actions: capacity (T-005) ---

namespace {
constexpr quint64 kTerabyte = 1000ULL * 1000 * 1000 * 1000;
constexpr quint64 kFree = 345ULL * 1000 * 1000 * 1000;
}  // namespace

TEST(FilesStorage, CapacityArrivesFromAWorkerAndIsHiddenUntilThen) {
  const QLocale previous;
  QLocale::setDefault(QLocale::c());
  const auto restore = qScopeGuard([&] { QLocale::setDefault(previous); });
  Devices devices;
  devices.probe->set("/media/data", kTerabyte, kFree);
  devices.probe->gate("/media/data");
  auto data = volume("data", "stick");
  data.mountPoints = {"/media/data"};
  devices.backend.drives = {device("stick")};
  devices.backend.volumes = {data};
  devices.backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] { return devices.probe->callCount("/media/data") == 1; }));
  QTest::qWait(20);
  EXPECT_FALSE(devices.value("data", DevicesModel::CapacityValid).toBool());  // REQ-F-043: no bar at zero
  devices.probe->release("/media/data");
  ASSERT_TRUE(QTest::qWaitFor([&] { return devices.value("data", DevicesModel::CapacityValid).toBool(); }));
  EXPECT_DOUBLE_EQ(devices.value("data", DevicesModel::CapacityFraction).toDouble(), 1.0 - (345.0 / 1000.0));
  EXPECT_EQ(devices.value("data", DevicesModel::CapacityText).toString(), "345 GB free of 1 TB");  // REQ-F-039
  for (const auto& call : devices.probe->calls()) {
    EXPECT_NE(call.thread, QThread::currentThread());  // REQ-F-042
  }
}

TEST(FilesStorage, CapacityTextIsATranslatableTwoArgumentFormat) {
  QFile source(QStringLiteral(FILES_SOURCE_DIR "/apps/files/storage/devices_model.cpp"));
  ASSERT_TRUE(source.open(QIODevice::ReadOnly));
  EXPECT_TRUE(source.readAll().contains(R"(tr("%1 free of %2"))"));
}

TEST(FilesStorage, RowsWithoutAMountHaveNoCapacity) {
  Devices devices;
  devices.backend.drives = {device("stick"), opticalDrive("dvd", false)};
  devices.backend.volumes = {volume("usb", "stick")};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 2);
  QTest::qWait(20);
  EXPECT_FALSE(devices.value("usb", DevicesModel::CapacityValid).toBool());
  EXPECT_FALSE(devices.value("dvd", DevicesModel::CapacityValid).toBool());
  EXPECT_TRUE(devices.probe->calls().isEmpty());
}

TEST(FilesStorage, MountingMeasuresAndUnmountingForgets) {
  Devices devices;
  devices.probe->set("/media/usb", kTerabyte, kFree);
  devices.backend.drives = {device("stick")};
  devices.backend.volumes = {volume("usb", "stick")};
  devices.backend.publish();
  devices.backend.volumes[0].mountPoints = {"/media/usb"};
  devices.backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] { return devices.value("usb", DevicesModel::CapacityValid).toBool(); }));
  EXPECT_EQ(devices.probe->callCount("/media/usb"), 1);  // REQ-F-044
  devices.backend.volumes[0].mountPoints.clear();
  devices.backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !devices.value("usb", DevicesModel::CapacityValid).toBool(); }));
  EXPECT_TRUE(devices.value("usb", DevicesModel::CapacityText).toString().isEmpty());
  devices.backend.volumes[0].mountPoints = {"/media/usb"};
  devices.backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] { return devices.value("usb", DevicesModel::CapacityValid).toBool(); }));
  EXPECT_EQ(devices.probe->callCount("/media/usb"), 2);  // Remount measures afresh, not from a stale cache.
}

TEST(FilesStorage, VolumesSharingAMountPointShareOneQuery) {
  Devices devices;
  devices.probe->set("/media/shared", kTerabyte, kFree);
  auto first = volume("first", "stick");
  first.mountPoints = {"/media/shared"};
  auto second = volume("second", "stick");
  second.mountPoints = {"/media/shared"};
  devices.backend.drives = {device("stick")};
  devices.backend.volumes = {first, second};
  devices.backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] {
    return devices.value("first", DevicesModel::CapacityValid).toBool() &&
           devices.value("second", DevicesModel::CapacityValid).toBool();
  }));
  EXPECT_EQ(devices.probe->callCount("/media/shared"), 1);
}

TEST(FilesStorage, CapacityIsRefreshedAfterTheStalenessInterval) {
  Devices devices;
  auto& timer = DevicesModelTestAccess::capacityTimer(devices.model);
  EXPECT_EQ(timer.intervalAsDuration(), DevicesModel::kCapacityRefreshInterval);
  EXPECT_FALSE(timer.isActive());  // No rows, no timer.
  devices.probe->set("/media/usb", kTerabyte, kFree);
  auto data = volume("usb", "stick");
  data.mountPoints = {"/media/usb"};
  devices.backend.drives = {device("stick")};
  devices.backend.volumes = {data};
  devices.backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] { return devices.value("usb", DevicesModel::CapacityValid).toBool(); }));
  EXPECT_TRUE(timer.isActive());
  timer.setInterval(10);  // Stand-in for the interval elapsing (REQ-F-045).
  ASSERT_TRUE(QTest::qWaitFor([&] { return devices.probe->callCount("/media/usb") >= 2; }));
  devices.backend.volumes.clear();
  devices.backend.drives.clear();
  devices.backend.publish();
  EXPECT_EQ(devices.model.count(), 0);
  EXPECT_FALSE(timer.isActive());
}

TEST(FilesStorage, AHungQueryBlocksOnlyItsOwnRow) {
  Devices devices;
  devices.probe->gate("/media/hung");
  devices.probe->set("/media/hung", kTerabyte, kFree);
  devices.probe->set("/media/fine", kTerabyte, kFree);
  const auto release = qScopeGuard([&] { devices.probe->release("/media/hung"); });
  auto hung = volume("hung", "stick");
  hung.mountPoints = {"/media/hung"};
  auto fine = volume("fine", "stick");
  fine.mountPoints = {"/media/fine"};
  devices.backend.drives = {device("stick")};
  devices.backend.volumes = {hung, fine};
  devices.backend.publish();
  ASSERT_TRUE(QTest::qWaitFor([&] { return devices.value("fine", DevicesModel::CapacityValid).toBool(); }));
  EXPECT_FALSE(devices.value("hung", DevicesModel::CapacityValid).toBool());
  devices.model.refreshCapacity();  // An outstanding query is not stacked behind itself.
  QTest::qWait(20);
  EXPECT_EQ(devices.probe->callCount("/media/hung"), 1);
}

TEST(FilesStorage, InternalDrivesListFirstThenExternalThenOptical) {
  Devices devices;
  auto internal = volume("zz-internal", "zz-disk");
  internal.mountPoints = {"/mnt/data"};
  devices.backend.drives = {opticalDrive("aa-dvd", false), device("mm-stick"), internalDrive("zz-disk")};
  devices.backend.volumes = {volume("usb", "mm-stick"), internal};
  devices.backend.publish();
  ASSERT_EQ(devices.model.count(), 3);
  EXPECT_EQ(devices.value(0, DevicesModel::TargetId).toString(), "zz-internal");
  EXPECT_EQ(devices.value(1, DevicesModel::TargetId).toString(), "usb");
  EXPECT_EQ(devices.value(2, DevicesModel::TargetId).toString(), "aa-dvd");
}

TEST(FilesStorage, OperationFailureIsReportedOnceForTheStatusBar) {
  Devices devices;
  auto usb = volume("usb", "stick");
  usb.mountPoints = {"/media/usb"};
  devices.backend.drives = {device("stick")};
  devices.backend.volumes = {usb};
  devices.backend.publish();
  QSignalSpy failures(&devices.model, &DevicesModel::operationFailed);
  devices.model.remove("usb");
  ASSERT_TRUE(QTest::qWaitFor([&] { return !devices.backend.calls.isEmpty(); }));
  auto result = devices.backend.calls.first();
  result.errorName = "org.freedesktop.UDisks2.Error.DeviceBusy";
  emit devices.backend.operationFinished(result);
  ASSERT_EQ(failures.size(), 1);
  EXPECT_EQ(failures.first().first().toString(), devices.model.errorMessage());
  ASSERT_TRUE(QTest::qWaitFor([&] { return !devices.value("usb", DevicesModel::Busy).toBool(); }));
  devices.backend.calls.clear();
  devices.model.remove("usb");
  result = files_test::removalCall(devices.backend);
  ASSERT_EQ(result.operation, StorageOperation::PowerOff);
  emit devices.backend.operationFinished(result);  // Success reports nothing.
  EXPECT_EQ(failures.size(), 1);
}

TEST(FilesStorage, UnmountedVolumesOfferNoRemoval) {
  Devices devices;
  devices.backend.drives = {device("stick"), internalDrive("disk"), opticalDrive("dvd", true), readerSlot(0, true)};
  auto internal = volume("internal", "disk");
  internal.mountPoints = {"/mnt/data"};
  devices.backend.volumes = {volume("usb", "stick"), internal, volume("disc", "dvd"), volume("card", "slot0")};
  devices.backend.publish();
  for (const auto* row : {"usb", "disc", "card"}) {
    ASSERT_GE(devices.model.findRow(row), 0) << row;
    EXPECT_EQ(devices.value(row, DevicesModel::RemovalVerb).toInt(), DevicesModel::NoVerb) << row;
    devices.model.remove(row);
  }
  QTest::qWait(20);
  EXPECT_TRUE(devices.backend.calls.isEmpty());
  EXPECT_TRUE(devices.model.errorMessage().isEmpty());
}
