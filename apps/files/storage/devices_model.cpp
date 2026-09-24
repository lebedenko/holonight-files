#include "devices_model.h"

#include "storage_filter.h"

#include <QTimer>

#include <algorithm>
using namespace HoloNight::System;
namespace {
QString operationError(const StorageResult& result) {
  if (result.succeeded()) {
    return {};
  }
  if (result.errorName.endsWith("NotAuthorizedDismissed")) {
    return DevicesModel::tr("Authorization was canceled.");
  }
  if (result.errorName.endsWith("NotAuthorized")) {
    return DevicesModel::tr("Permission to access this storage was denied.");
  }
  if (result.errorName.endsWith("Busy")) {
    return DevicesModel::tr("The device is busy. Close files using it and try again.");
  }
  if (result.errorName.endsWith("ScopeChanged")) {
    return DevicesModel::tr("The affected devices changed. Review them before trying again.");
  }
  if (result.errorName.endsWith("Unavailable") || result.errorName.endsWith("Disappeared")) {
    return DevicesModel::tr("The storage device or service is no longer available.");
  }
  return DevicesModel::tr("Storage operation failed: %1")
      .arg(result.errorMessage.isEmpty() ? result.errorName : result.errorMessage);
}
QString driveName(const StorageDrive& drive) {
  const auto name = (drive.vendor + ' ' + drive.model).trimmed();
  return name.isEmpty() ? DevicesModel::tr("Storage device") : name;
}
QString volumeName(const StorageVolume& volume) {
  if (!volume.label.isEmpty()) {
    return volume.label;
  }
  return !volume.device.isEmpty() ? volume.device : DevicesModel::tr("Volume");
}
QString volumeState(const StorageVolume& volume, bool busy) {
  if (volume.locked) {
    return DevicesModel::tr("Locked — unlocking is not available");
  }
  if (busy) {
    return DevicesModel::tr("Working…");
  }
  return volume.mountPoints.isEmpty() ? DevicesModel::tr("Not mounted") : volume.mountPoints.join(", ");
}
}  // namespace
DevicesModel::DevicesModel(QObject* parent) : DevicesModel(new StorageController, parent) {
  controller_->setParent(this);
}
DevicesModel::DevicesModel(StorageController* controller, QObject* parent)
    : QAbstractListModel(parent), controller_(controller) {
  for (auto* model : {static_cast<QAbstractItemModel*>(controller_->drives()),
                      static_cast<QAbstractItemModel*>(controller_->volumes())}) {
    connect(model, &QAbstractItemModel::rowsInserted, this, &DevicesModel::scheduleRefresh);
    connect(model, &QAbstractItemModel::rowsRemoved, this, &DevicesModel::scheduleRefresh);
    connect(model, &QAbstractItemModel::dataChanged, this, &DevicesModel::scheduleRefresh);
  }
  connect(controller_, &StorageController::operationStateChanged, this, &DevicesModel::scheduleRefresh);
  connect(controller_, &StorageController::availableChanged, this, &DevicesModel::scheduleRefresh);
  connect(controller_, &StorageController::operationFinished, this, [this](const StorageResult& result) {
    error_message_ = operationError(result);
    if (result.requestId == activation_request_) {
      activation_request_.clear();
      if (result.succeeded() && interaction_enabled_ && !result.mountPath.isEmpty()) {
        const auto volume = controller_->volumes()->find(result.targetId);
        if (volume && volume->mountPoints.contains(result.mountPath)) {
          emit openRequested(result.mountPath);
        } else {
          error_message_ = tr("The volume is no longer mounted. The current location was preserved.");
        }
      }
    }
    scheduleRefresh();
  });
  refresh();
}
int DevicesModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }
QVariant DevicesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
    return {};
  }
  return rows_[index.row()].value(QString::fromLatin1(roleNames().value(role)));
}
QHash<int, QByteArray> DevicesModel::roleNames() const {
  return {{TargetId, "targetId"},   {DriveId, "driveId"},         {Name, "name"},
          {DriveName, "driveName"}, {State, "stateText"},         {Mounted, "mounted"},
          {Busy, "busy"},           {CanMount, "canMount"},       {CanUnmount, "canUnmount"},
          {CanEject, "canEject"},   {CanPowerOff, "canPowerOff"}, {CanActivate, "canActivate"}};
}
void DevicesModel::refresh() {
  QList<QVariantMap> rows;
  for (const auto& volume : controller_->volumes()->items()) {
    const auto drive = controller_->drives()->find(volume.driveId);
    if (!StorageFilter::eligible(volume)) {
      continue;
    }
    const bool removable = drive && drive->removable;
    if (removable ? !drive->mediaPresent : volume.mountPoints.isEmpty()) {
      continue;
    }
    const bool busy = controller_->busy(volume.id) || (drive && controller_->busy(drive->id));
    rows.append({{"targetId", volume.id},
                 {"driveId", volume.driveId},
                 {"name", volumeName(volume)},
                 {"driveName", drive ? driveName(*drive) : tr("Volumes")},
                 {"mounted", !volume.mountPoints.isEmpty()},
                 {"busy", busy},
                 {"stateText", volumeState(volume, busy)},
                 {"canActivate", !volume.locked && !busy && (volume.canMount || !volume.mountPoints.isEmpty())},
                 {"canMount", volume.canMount && !volume.locked && !busy},
                 {"canUnmount", volume.canUnmount && !busy},
                 {"canEject", drive && drive->canEject && !busy},
                 {"canPowerOff", drive && drive->canPowerOff && !busy}});
  }
  appendEmptyDrives(rows);
  std::ranges::sort(rows, [](const auto& first, const auto& second) {
    return first.value("driveId").toString() + first.value("targetId").toString() <
           second.value("driveId").toString() + second.value("targetId").toString();
  });
  if (rows != rows_) {
    beginResetModel();
    rows_ = rows;
    endResetModel();
  }
  if (!current_volume_.isEmpty()) {
    const auto volume = controller_->volumes()->find(current_volume_);
    if (!volume || !volume->mountPoints.contains(current_mount_)) {
      current_volume_.clear();
      current_mount_.clear();
      activation_request_.clear();
      emit recoveryRequested();
    }
  }
  trackLocation();
  emit changed();
}
bool DevicesModel::visibleTarget(const QString& targetId, const char* capability) const {
  if (!interaction_enabled_) {
    return false;
  }
  return std::ranges::any_of(rows_, [&](const auto& row) {
    return (row.value("targetId").toString() == targetId || row.value("driveId").toString() == targetId) &&
           row.value(QLatin1String(capability)).toBool();
  });
}

void DevicesModel::mount(const QString& targetId) {
  if (visibleTarget(targetId, "canMount")) {
    controller_->mount(targetId);
  }
}
void DevicesModel::unmount(const QString& targetId) {
  if (visibleTarget(targetId, "canUnmount")) {
    controller_->unmount(targetId);
  }
}
void DevicesModel::eject(const QString& targetId) {
  if (visibleTarget(targetId, "canEject")) {
    controller_->eject(targetId);
  }
}
void DevicesModel::requestPowerOff(const QString& targetId) {
  if (!visibleTarget(targetId, "canPowerOff")) {
    return;
  }
  confirmation_drive_ = targetId;
  confirmation_scope_ = controller_->removalScope(targetId, true);
  QStringList names;
  for (const auto& target : confirmation_scope_) {
    if (const auto drive = controller_->drives()->find(target)) {
      names.append(driveName(*drive));
    } else if (const auto volume = controller_->volumes()->find(target)) {
      names.append(volumeName(*volume));
    }
  }
  confirmation_text_ = tr("Power off these devices and volumes?\n%1").arg(names.join("\n"));
  emit changed();
}
void DevicesModel::confirmPowerOff() {
  if (!interaction_enabled_ || confirmation_drive_.isEmpty()) {
    return;
  }
  const auto targetId = confirmation_drive_;
  const auto scope = confirmation_scope_;
  cancelPowerOff();
  controller_->powerOff(targetId, scope);
}
void DevicesModel::cancelPowerOff() {
  confirmation_drive_.clear();
  confirmation_scope_.clear();
  confirmation_text_.clear();
  emit changed();
}

void DevicesModel::scheduleRefresh() {
  if (refresh_pending_) {
    return;
  }
  refresh_pending_ = true;
  QTimer::singleShot(0, this, [this] {
    refresh_pending_ = false;
    refresh();
  });
}
void DevicesModel::setInteractionEnabled(bool enabled) {
  interaction_enabled_ = enabled;
  if (!enabled) {
    activation_request_.clear();
  }
}
void DevicesModel::navigationChanged(const QString& path) {
  activation_request_.clear();
  current_path_ = QDir::cleanPath(path);
  canonical_path_.clear();
  current_volume_.clear();
  current_mount_.clear();
  trackLocation();
}
void DevicesModel::resolvedLocation(const QString& path, const QString& canonicalPath) {
  if (QDir::cleanPath(path) != current_path_) {
    return;
  }
  canonical_path_ = canonicalPath;
  trackLocation();
}
void DevicesModel::trackLocation() {
  const auto path = canonical_path_.isEmpty() ? current_path_ : canonical_path_;
  if (!current_volume_.isEmpty()) {
    return;
  }
  for (const auto& volume : controller_->volumes()->items()) {
    for (const auto& mount : volume.mountPoints) {
      const auto root = QDir::cleanPath(mount);
      if (root == "/") {
        continue;
      }
      if ((path == root || path.startsWith(root + '/')) && root.size() > current_mount_.size()) {
        current_mount_ = root;
        current_volume_ = volume.id;
      }
    }
  }
}
void DevicesModel::activate(const QString& targetId) {
  if (!interaction_enabled_) {
    return;
  }
  activation_request_.clear();
  const auto volume = controller_->volumes()->find(targetId);
  if (!volume || !StorageFilter::eligible(*volume) || controller_->busy(targetId)) {
    return;
  }
  bool visible = false;
  for (const auto& row : rows_) {
    if (row.value("targetId").toString() == targetId) {
      visible = true;
    }
  }
  if (!visible || volume->locked) {
    return;
  }
  if (!volume->mountPoints.isEmpty()) {
    emit openRequested(volume->mountPoints.first());
  } else if (volume->canMount) {
    activation_request_ = controller_->mount(targetId);
  }
}

void DevicesModel::appendEmptyDrives(QList<QVariantMap>& rows) const {
  // Empty readers remain visible without creating ignored-device placeholders.
  for (const auto& drive : controller_->drives()->items()) {
    if (!drive.removable || drive.mediaPresent) {
      continue;
    }
    const bool hasBlocks = std::any_of(controller_->volumes()->items().cbegin(), controller_->volumes()->items().cend(),
                                       [&](const auto& record) { return record.driveId == drive.id; });
    const bool allIgnored =
        hasBlocks && std::all_of(controller_->volumes()->items().cbegin(), controller_->volumes()->items().cend(),
                                 [&](const auto& record) { return record.driveId != drive.id || record.hintIgnore; });
    if (allIgnored) {
      continue;
    }
    const bool busy = controller_->busy(drive.id);
    rows.append({{"targetId", drive.id},
                 {"driveId", drive.id},
                 {"name", driveName(drive)},
                 {"driveName", driveName(drive)},
                 {"stateText", tr("No media")},
                 {"mounted", false},
                 {"busy", busy},
                 {"canActivate", false},
                 {"canMount", false},
                 {"canUnmount", false},
                 {"canEject", drive.canEject && !busy},
                 {"canPowerOff", drive.canPowerOff && !busy}});
  }
}
