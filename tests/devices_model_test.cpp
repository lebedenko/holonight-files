#include "devices_model.h"

#include "directory_controller.h"
#include "engine_setup.h"

#include <QAbstractItemModelTester>
#include <QCoreApplication>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTest>

#include <StorageBackend.h>
#include <gtest/gtest.h>
#include <memory>
#include <utility>

using namespace HoloNight::System;
namespace {
class FakeStorage : public StorageBackend {
 public:
  QList<StorageDrive> drives;
  QList<StorageVolume> volumes;
  QList<StorageResult> calls;
  void start() override {}
  void stop() override {}
  void execute(const QString& targetId, StorageOperation operation, const QString& target) override {
    calls.append({.requestId = targetId,
                  .targetId = target,
                  .operation = operation,
                  .mountPath = {},
                  .errorName = {},
                  .errorMessage = {}});
  }
  void publish() {
    emit snapshotChanged(drives, volumes, true);
    QCoreApplication::processEvents();
  }
};
StorageDrive device(QString targetId = "drive", bool removable = true, bool media = true) {
  StorageDrive driveRecord;
  driveRecord.id = std::move(targetId);
  driveRecord.model = "USB SSD";
  driveRecord.removable = removable;
  driveRecord.mediaPresent = media;
  driveRecord.canPowerOff = true;
  return driveRecord;
}
StorageVolume volume(const QString& targetId = "volume", QString drive = "drive") {
  StorageVolume record;
  record.id = targetId;
  record.driveId = std::move(drive);
  record.label = targetId;
  record.usage = "filesystem";
  record.canMount = true;
  return record;
}
}  // namespace
TEST(FilesStorage, FiltersInfrastructureWithoutHidingHintSystemData) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller);
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
  DevicesModel model(&controller);
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
TEST(FilesStorage, PowerOffPresentsHiddenSiblingScopeAndRejectsChangedConfirmation) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller);
  auto first = device("first");
  first.siblingId = "physical";
  auto second = device("second");
  second.siblingId = "physical";
  auto visible = volume("visible", "first");
  auto hidden = volume("hidden", "second");
  hidden.hintIgnore = true;
  backend.drives = {first, second};
  backend.volumes = {visible, hidden};
  backend.publish();
  model.requestPowerOff("first");
  EXPECT_TRUE(model.confirmationText().contains("hidden"));
  backend.volumes.append(volume("new", "first"));
  backend.publish();
  model.confirmPowerOff();
  ASSERT_TRUE(QTest::qWaitFor([&] { return !model.errorMessage().isEmpty(); }));
  EXPECT_TRUE(backend.calls.isEmpty());
}

TEST(FilesStorage, FiltersFixedDisksAndEmptyReadersAccordingToConsumerPolicy) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller);
  backend.drives = {device("empty", true, false), device("fixed", false), device("external")};
  auto mounted = volume("mounted", "fixed");
  mounted.mountPoints = {"/mnt/backup"};
  backend.volumes = {mounted, volume("unmounted", "fixed"), volume("usb", "external")};
  backend.publish();
  EXPECT_EQ(model.count(), 3);
}
TEST(FilesStorage, MountOpensOnlyForCurrentActivationAndPreservesLocationOnFailure) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller);
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
  DevicesModel model(&controller);
  backend.drives = {device()};
  backend.volumes = {volume()};
  backend.publish();
  QSignalSpy opens(&model, &DevicesModel::openRequested);
  model.activate("volume");
  QCoreApplication::processEvents();
  ASSERT_EQ(backend.calls.size(), 1);
  model.setInteractionEnabled(false);
  model.activate("volume");
  model.requestPowerOff("drive");
  EXPECT_TRUE(model.confirmationText().isEmpty());
  model.setInteractionEnabled(true);
  auto result = backend.calls.last();
  result.mountPath = "/media/Data";
  emit backend.operationFinished(result);
  EXPECT_TRUE(opens.isEmpty());
  EXPECT_EQ(backend.calls.size(), 1);
}
TEST(FilesStorage, UnmountRecoversOnlyWhenCurrentLocationIsWithinAffectedMount) {
  FakeStorage backend;
  StorageController controller(&backend);
  DevicesModel model(&controller);
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
  DirectoryController controller(&storage, nullptr);
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
  DirectoryController controller(&storage, nullptr);
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
  DevicesModel model(&storage);
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
  DevicesModel model(&storage);
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
