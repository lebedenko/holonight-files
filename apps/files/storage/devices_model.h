#pragma once
#include "capacity_probe.h"

#include <QAbstractListModel>
#include <QHash>
#include <QTimer>
#include <QVariantMap>

#include <StorageController.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <optional>

class DevicesModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY changed)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
 public:
  // Capacity refresh is attempted at this interval while mounted rows exist (REQ-F-045).
  static constexpr std::chrono::milliseconds kCapacityRefreshInterval{std::chrono::seconds(30)};

  explicit DevicesModel(QObject* parent = nullptr);
  // A null probe measures the real filesystem (StorageInfoCapacityProbe).
  DevicesModel(HoloNight::System::StorageController* controller, QObject* parent = nullptr,
               std::shared_ptr<const CapacityProbe> probe = {});
  ~DevicesModel() override;
  DevicesModel(const DevicesModel&) = delete;
  DevicesModel& operator=(const DevicesModel&) = delete;
  DevicesModel(DevicesModel&&) = delete;
  DevicesModel& operator=(DevicesModel&&) = delete;

  enum Role {
    TargetId = Qt::UserRole + 1,
    DriveId,
    Name,
    DriveName,
    State,
    Mounted,
    Busy,
    CanActivate,
    IconName,
    RemovalVerb,
    RemovalLabel,
    GroupLabel,
    GroupStart,
    CapacityValid,
    CapacityFraction,
    CapacityText
  };
  // Mirrors HoloNight::System::StorageOperation value for value, which QML cannot name.
  enum Verb { NoVerb = -1, Mount = 0, Unmount, Eject, PowerOff };
  Q_ENUM(Verb)

  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  int count() const { return static_cast<int>(rows_.size()); }
  QString errorMessage() const { return error_message_; }
  void resolvedLocation(const QString& path, const QString& canonicalPath);
  void navigationChanged(const QString& path);
  void setInteractionEnabled(bool enabled);
  // Re-measures every mounted row, e.g. after a copy or delete finished.
  void refreshCapacity();
  // Opens a mounted volume, or mounts it first (REQ-F-013). The only route to a mount (REQ-F-012).
  Q_INVOKABLE void activate(const QString& targetId);
  // Runs the row's single removal verb (REQ-F-021); inert for a row without one (REQ-F-022).
  Q_INVOKABLE void remove(const QString& targetId);
  // Row index of targetId, or -1.
  Q_INVOKABLE int findRow(const QString& targetId) const;
 signals:
  void changed();
  void openRequested(const QString& path);
  void recoveryRequested();
  // A storage operation failed; the window shows message in its status bar (device-actions REQ-F-029).
  void operationFailed(const QString& message);

 private:
  friend struct DevicesModelTestAccess;
  // Shared with every detached capacity thread; ~DevicesModel() locks it and nulls `model`.
  struct DeliveryGuard {
    std::mutex mutex;
    DevicesModel* model = nullptr;
  };
  void refresh();
  void appendEmptyDrives(QList<QVariantMap>& rows) const;
  std::optional<QVariantMap> volumeRow(const HoloNight::System::StorageVolume& volume) const;
  std::optional<HoloNight::System::StorageOperation> safeRemovalVerb(const HoloNight::System::StorageDrive& drive,
                                                                     bool volumeCanUnmount) const;
  bool scopeContainsOtherDrive(const QString& driveId, const QStringList& scope) const;
  void applyCapacity(QVariantMap& row) const;
  void syncCapacity();
  void dispatchCapacity(const QString& mountPoint);
  void deliverCapacity(const QString& mountPoint, quint64 generation, const Capacity& capacity);
  void scheduleRefresh();
  void trackLocation();
  HoloNight::System::StorageController* controller_;
  std::shared_ptr<const CapacityProbe> probe_;
  std::shared_ptr<DeliveryGuard> guard_;
  QList<QVariantMap> rows_;
  QString error_message_;
  bool refresh_pending_ = false;
  bool interaction_enabled_ = true;
  QString activation_request_;
  QString current_path_;
  QString canonical_path_;
  QString current_volume_;
  QString current_mount_;
  // Keyed on mount point: what the probe measures, shared by volumes at one path, evicted on unmount.
  QHash<QString, Capacity> capacity_;
  QHash<QString, quint64> capacity_in_flight_;  // mount point -> generation of its outstanding query
  quint64 capacity_generation_ = 0;
  QTimer capacity_timer_;
};
