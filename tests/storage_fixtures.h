#pragma once

#include "capacity_probe.h"

#include <QCoreApplication>
#include <QHash>
#include <QList>
#include <QSemaphore>
#include <QTest>
#include <QThread>

#include <StorageBackend.h>
#include <algorithm>
#include <memory>
#include <mutex>
#include <utility>

// Hardware-free storage fixtures (device-actions SPEC REQ-C-003, REQ-C-006): no test reads the
// host's UDisks2 service or real filesystem capacity through these.
namespace files_test {

class FakeStorage : public HoloNight::System::StorageBackend {
 public:
  QList<HoloNight::System::StorageDrive> drives;
  QList<HoloNight::System::StorageVolume> volumes;
  QList<HoloNight::System::StorageResult> calls;
  void start() override {}
  void stop() override {}
  void execute(const QString& requestId, HoloNight::System::StorageOperation operation,
               const QString& target) override {
    calls.append({.requestId = requestId,
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

// A USB stick unless told otherwise: external bus, the drive itself is the removable object.
inline HoloNight::System::StorageDrive storageDrive(QString id = "drive", bool removable = true, bool media = true) {
  HoloNight::System::StorageDrive record;
  record.id = std::move(id);
  record.model = "USB SSD";
  record.connectionBus = "usb";
  record.removable = removable;
  record.mediaPresent = media;
  record.canPowerOff = true;
  return record;
}

inline HoloNight::System::StorageVolume storageVolume(const QString& id = "volume", QString drive = "drive") {
  HoloNight::System::StorageVolume record;
  record.id = id;
  record.driveId = std::move(drive);
  record.label = id;
  record.usage = "filesystem";
  record.canMount = true;
  record.canUnmount = true;
  return record;
}

// A drive-level removal first unmounts the affected mounted volumes. Completes each such step as the
// provider would (the volume loses its mount point, then the step succeeds) and returns the first call
// that is not a pending unmount: the drive-level Eject/PowerOff, or the last Unmount for an Unmount verb.
inline HoloNight::System::StorageResult removalCall(FakeStorage& backend) {
  if (!QTest::qWaitFor([&] { return !backend.calls.isEmpty(); })) {
    return {};
  }
  while (backend.calls.last().operation == HoloNight::System::StorageOperation::Unmount) {
    const auto step = backend.calls.last();
    const auto count = backend.calls.size();
    for (auto& volume : backend.volumes) {
      if (volume.id == step.targetId) {
        volume.mountPoints.clear();
      }
    }
    backend.publish();
    emit backend.operationFinished(step);
    if (!QTest::qWaitFor([&] { return backend.calls.size() > count; }, 200)) {
      return step;
    }
  }
  return backend.calls.last();
}

// Per-path fixed figures; records every call's path and thread; a gated path blocks its caller
// until release(), standing in for a hung mount.
class FakeCapacityProbe : public CapacityProbe {
 public:
  struct Call {
    QString path;
    QThread* thread = nullptr;
  };
  Capacity measure(const QString& mountPoint) const override {
    std::shared_ptr<QSemaphore> gate;
    {
      const std::scoped_lock lock(mutex_);
      calls_.append({.path = mountPoint, .thread = QThread::currentThread()});
      gate = gates_.value(mountPoint);
    }
    if (gate) {
      gate->acquire();
    }
    const std::scoped_lock lock(mutex_);
    return results_.value(mountPoint);
  }
  void set(const QString& mountPoint, quint64 total, quint64 available) {
    const std::scoped_lock lock(mutex_);
    results_.insert(mountPoint, {.valid = true, .bytesAvailable = available, .bytesTotal = total});
  }
  void gate(const QString& mountPoint) {
    const std::scoped_lock lock(mutex_);
    gates_.insert(mountPoint, std::make_shared<QSemaphore>(0));
  }
  // Frees every current and future call on this path.
  void release(const QString& mountPoint) {
    const std::scoped_lock lock(mutex_);
    if (const auto gate = gates_.take(mountPoint)) {
      gate->release(1 << 20);
    }
  }
  [[nodiscard]] QList<Call> calls() const {
    const std::scoped_lock lock(mutex_);
    return calls_;
  }
  [[nodiscard]] qsizetype callCount(const QString& mountPoint) const {
    const std::scoped_lock lock(mutex_);
    return std::count_if(calls_.cbegin(), calls_.cend(), [&](const Call& call) { return call.path == mountPoint; });
  }

 private:
  mutable std::mutex mutex_;
  mutable QList<Call> calls_;
  QHash<QString, Capacity> results_;
  QHash<QString, std::shared_ptr<QSemaphore>> gates_;
};

}  // namespace files_test
