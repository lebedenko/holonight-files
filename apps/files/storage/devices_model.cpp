#include "devices_model.h"

#include "storage_filter.h"
#include "storage_policy.h"

#include <QLocale>
#include <QSet>

#include <algorithm>
#include <thread>
#include <tuple>
#include <utility>
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
int verbValue(std::optional<StorageOperation> verb) {
  return verb ? static_cast<int>(*verb) : static_cast<int>(DevicesModel::NoVerb);
}
// Names the verb, since External and Optical rows share one eject glyph (REQ-NF-002).
QString verbLabel(std::optional<StorageOperation> verb, const QString& name) {
  if (!verb) {
    return {};
  }
  switch (*verb) {
    case StorageOperation::Unmount:
      return DevicesModel::tr("Unmount %1").arg(name);
    case StorageOperation::Eject:
      return DevicesModel::tr("Eject %1").arg(name);
    case StorageOperation::PowerOff:
      return DevicesModel::tr("Safely remove %1").arg(name);
    case StorageOperation::Mount:
      break;
  }
  return {};
}
// A drive contributing several rows labels them with its name; a single row needs no label
// (REQ-F-018, REQ-F-019). Rows arrive sorted, so each drive's rows are one contiguous run.
void assignGroups(QList<QVariantMap>& rows) {
  QHash<QString, int> perDrive;
  for (const auto& row : std::as_const(rows)) {
    ++perDrive[row.value("driveId").toString()];
  }
  QString previous;
  for (qsizetype index = 0; index < rows.size(); ++index) {
    auto& row = rows[index];
    const auto driveId = row.value("driveId").toString();
    const bool grouped = perDrive.value(driveId) > 1;
    row.insert("groupLabel", grouped ? row.value("driveName").toString() : QString());
    row.insert("groupStart", grouped && (index == 0 || driveId != previous));
    previous = driveId;
  }
}
}  // namespace
DevicesModel::DevicesModel(QObject* parent) : DevicesModel(new StorageController, parent) {
  controller_->setParent(this);
}
DevicesModel::DevicesModel(StorageController* controller, QObject* parent, std::shared_ptr<const CapacityProbe> probe)
    : QAbstractListModel(parent),
      controller_(controller),
      probe_(probe ? std::move(probe) : std::make_shared<StorageInfoCapacityProbe>()),
      guard_(std::make_shared<DeliveryGuard>()) {
  guard_->model = this;
  capacity_timer_.setInterval(kCapacityRefreshInterval);
  connect(&capacity_timer_, &QTimer::timeout, this, &DevicesModel::refreshCapacity);
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
    if (!error_message_.isEmpty()) {
      emit operationFailed(error_message_);
    }
    scheduleRefresh();
  });
  refresh();
}
DevicesModel::~DevicesModel() {
  const std::scoped_lock lock(guard_->mutex);
  guard_->model = nullptr;
}
int DevicesModel::rowCount(const QModelIndex& parent) const { return parent.isValid() ? 0 : count(); }
QVariant DevicesModel::data(const QModelIndex& index, int role) const {
  if (!index.isValid() || index.row() < 0 || index.row() >= count()) {
    return {};
  }
  return rows_[index.row()].value(QString::fromLatin1(roleNames().value(role)));
}
QHash<int, QByteArray> DevicesModel::roleNames() const {
  return {{TargetId, "targetId"},
          {DriveId, "driveId"},
          {Name, "name"},
          {DriveName, "driveName"},
          {State, "stateText"},
          {Mounted, "mounted"},
          {Busy, "busy"},
          {CanActivate, "canActivate"},
          {IconName, "iconName"},
          {RemovalVerb, "removalVerb"},
          {RemovalLabel, "removalLabel"},
          {GroupLabel, "groupLabel"},
          {GroupStart, "groupStart"},
          {CapacityValid, "capacityValid"},
          {CapacityFraction, "capacityFraction"},
          {CapacityText, "capacityText"}};
}
bool DevicesModel::scopeContainsOtherDrive(const QString& driveId, const QStringList& scope) const {
  return std::ranges::any_of(controller_->drives()->items(),
                             [&](const auto& drive) { return drive.id != driveId && scope.contains(drive.id); });
}
std::optional<StorageOperation> DevicesModel::safeRemovalVerb(const StorageDrive& drive, bool volumeCanUnmount) const {
  const auto verb = StoragePolicy::removalVerb(drive, volumeCanUnmount);
  if (verb != StorageOperation::PowerOff) {
    return verb;
  }
  const auto scope = controller_->removalScope(drive.id, true);
  if (!scope.isEmpty() && !scopeContainsOtherDrive(drive.id, scope)) {
    return verb;
  }
  return volumeCanUnmount ? std::optional{StorageOperation::Unmount} : std::nullopt;
}
// The volume's row, or nothing when the panel omits it.
std::optional<QVariantMap> DevicesModel::volumeRow(const StorageVolume& volume) const {
  const auto drive = controller_->drives()->find(volume.driveId);
  if (!StorageFilter::eligible(volume)) {
    return std::nullopt;
  }
  // A volume without a drive record is treated as internal: it gets no drive-level verb.
  const auto policyDrive = drive ? *drive : StorageDrive{};
  const bool mounted = !volume.mountPoints.isEmpty();
  const bool removable = drive && drive->removable;
  if (removable && !drive->mediaPresent) {
    return std::nullopt;
  }
  // Unmounted internal volumes are hidden (REQ-F-015); removable media stay visible to be mounted.
  if (!mounted && (!removable || StoragePolicy::classify(policyDrive) == StoragePolicy::DeviceClass::Internal)) {
    return std::nullopt;
  }
  const bool busy = controller_->busy(volume.id) || (drive && controller_->busy(drive->id));
  // Only a mounted volume offers removal; an unmounted one is opened (and mounted) by activation.
  const auto verb = mounted ? safeRemovalVerb(policyDrive, volume.canUnmount) : std::optional<StorageOperation>{};
  const auto name = volumeName(volume);
  return QVariantMap{{"targetId", volume.id},
                     {"driveId", volume.driveId},
                     {"name", name},
                     {"driveName", drive ? driveName(*drive) : tr("Volumes")},
                     {"mounted", mounted},
                     {"busy", busy},
                     {"stateText", volumeState(volume, busy)},
                     {"canActivate", !volume.locked && !busy && (volume.canMount || mounted)},
                     {"classRank", static_cast<int>(StoragePolicy::classify(policyDrive))},
                     {"iconName", StoragePolicy::iconName(policyDrive)},
                     {"removalVerb", verbValue(verb)},
                     {"removalLabel", verbLabel(verb, name)},
                     // Deferred: a volume with several mount points is measured at the first, as it is opened.
                     {"mountPoint", mounted ? volume.mountPoints.first() : QString()}};
}
void DevicesModel::refresh() {
  QList<QVariantMap> rows;
  for (const auto& volume : controller_->volumes()->items()) {
    if (auto row = volumeRow(volume)) {
      rows.append(std::move(*row));
    }
  }
  appendEmptyDrives(rows);
  // Internal drives first, then external, then optical; rows of one drive stay adjacent (REQ-F-020).
  std::ranges::sort(rows, [](const auto& first, const auto& second) {
    const auto key = [](const QVariantMap& row) {
      return std::tuple{row.value("classRank").toInt(), row.value("driveId").toString(),
                        row.value("targetId").toString()};
    };
    return key(first) < key(second);
  });
  assignGroups(rows);
  for (auto& row : rows) {
    applyCapacity(row);
  }
  if (rows != rows_) {
    beginResetModel();
    rows_ = rows;
    endResetModel();
  }
  syncCapacity();
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
int DevicesModel::findRow(const QString& targetId) const {
  for (qsizetype index = 0; index < rows_.size(); ++index) {
    if (rows_[index].value("targetId").toString() == targetId) {
      return static_cast<int>(index);
    }
  }
  return -1;
}
void DevicesModel::remove(const QString& targetId) {
  if (!interaction_enabled_) {
    return;  // REQ-F-023
  }
  const auto index = findRow(targetId);
  if (index < 0) {
    return;
  }
  const auto& row = rows_[index];
  const auto driveId = row.value("driveId").toString();
  // The row can lag a request started this turn, so the controller's own busy state is checked too.
  if (row.value("busy").toBool() || controller_->busy(targetId) || controller_->busy(driveId)) {
    return;  // REQ-F-028
  }
  switch (row.value("removalVerb").toInt()) {
    case Unmount:
      controller_->unmount(targetId);
      break;
    case Eject:
      controller_->eject(driveId);
      break;
    case PowerOff: {
      const auto scope = controller_->removalScope(driveId, true);
      if (scope.isEmpty() || scopeContainsOtherDrive(driveId, scope)) {
        scheduleRefresh();  // A stale row must not bypass the presentation guard (REQ-F-011).
        break;
      }
      // No confirmation (REQ-F-025): the scope is derived now, and the controller re-derives it
      // before acting, rejecting a change with ScopeChanged (REQ-F-026, REQ-F-027).
      controller_->powerOff(driveId, scope);
      break;
    }
    default:
      break;  // No verb, no operation and no error (REQ-F-022).
  }
}

void DevicesModel::applyCapacity(QVariantMap& row) const {
  const auto mountPoint = row.value("mountPoint").toString();
  const auto capacity = mountPoint.isEmpty() ? Capacity{} : capacity_.value(mountPoint);
  const bool valid = capacity.valid && capacity.bytesTotal > 0;
  const auto available = std::min(capacity.bytesAvailable, capacity.bytesTotal);
  const QLocale locale;
  row.insert("capacityValid", valid);
  row.insert("capacityFraction",
             valid ? 1.0 - (static_cast<double>(available) / static_cast<double>(capacity.bytesTotal)) : 0.0);
  row.insert(
      "capacityText",
      valid ? tr("%1 free of %2")
                  .arg(locale.formattedDataSize(static_cast<qint64>(available), 0, QLocale::DataSizeSIFormat),
                       locale.formattedDataSize(static_cast<qint64>(capacity.bytesTotal), 0, QLocale::DataSizeSIFormat))
            : QString());
}
// Evicts figures for paths no longer mounted (REQ-F-044) and measures newly mounted ones.
void DevicesModel::syncCapacity() {
  QSet<QString> mounted;
  for (const auto& row : std::as_const(rows_)) {
    const auto mountPoint = row.value("mountPoint").toString();
    if (!mountPoint.isEmpty()) {
      mounted.insert(mountPoint);
    }
  }
  capacity_.removeIf([&](const auto& entry) { return !mounted.contains(entry.key()); });
  capacity_in_flight_.removeIf([&](const auto& entry) { return !mounted.contains(entry.key()); });
  for (const auto& mountPoint : std::as_const(mounted)) {
    if (!capacity_.contains(mountPoint)) {
      dispatchCapacity(mountPoint);
    }
  }
  if (mounted.isEmpty()) {
    capacity_timer_.stop();
  } else if (!capacity_timer_.isActive()) {
    capacity_timer_.start();
  }
}
void DevicesModel::refreshCapacity() {
  for (const auto& row : std::as_const(rows_)) {
    const auto mountPoint = row.value("mountPoint").toString();
    if (!mountPoint.isEmpty()) {
      dispatchCapacity(mountPoint);
    }
  }
}
// One detached thread per query, as PlacesModel does for availability: a hung mount blocks only
// its own query (REQ-F-042) and never another row's or application exit. A path with a query
// still outstanding is not queried again, so a hung mount costs one thread, not one per refresh.
void DevicesModel::dispatchCapacity(const QString& mountPoint) {
  if (capacity_in_flight_.contains(mountPoint)) {
    return;
  }
  const auto generation = ++capacity_generation_;
  capacity_in_flight_.insert(mountPoint, generation);
  std::thread([guard = guard_, probe = probe_, mountPoint, generation] {
    const auto capacity = probe->measure(mountPoint);  // may block indefinitely; no lock held
    const std::scoped_lock lock(guard->mutex);
    if (guard->model == nullptr) {
      return;
    }
    QMetaObject::invokeMethod(
        guard->model,
        [model = guard->model, mountPoint, generation, capacity] {
          model->deliverCapacity(mountPoint, generation, capacity);
        },
        Qt::QueuedConnection);
  }).detach();
}
void DevicesModel::deliverCapacity(const QString& mountPoint, quint64 generation, const Capacity& capacity) {
  const auto pending = capacity_in_flight_.constFind(mountPoint);
  if (pending == capacity_in_flight_.cend() || *pending != generation) {
    return;  // Unmounted, or remounted, while measuring.
  }
  capacity_in_flight_.erase(pending);
  capacity_.insert(mountPoint, capacity);
  for (qsizetype index = 0; index < rows_.size(); ++index) {
    auto& row = rows_[index];
    if (row.value("mountPoint").toString() != mountPoint) {
      continue;
    }
    const auto before = row;
    applyCapacity(row);
    if (row != before) {
      const auto changed = this->index(static_cast<int>(index));
      emit dataChanged(changed, changed, {CapacityValid, CapacityFraction, CapacityText});
    }
  }
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
  if (findRow(targetId) < 0 || volume->locked) {
    return;
  }
  if (!volume->mountPoints.isEmpty()) {
    emit openRequested(volume->mountPoints.first());
  } else if (volume->canMount) {
    activation_request_ = controller_->mount(targetId);
  }
}

void DevicesModel::appendEmptyDrives(QList<QVariantMap>& rows) const {
  // Only an empty optical drive stays visible: opening its tray is the one thing to do with it
  // (REQ-F-017). Empty card-reader slots are noise and are hidden (REQ-F-016).
  for (const auto& drive : controller_->drives()->items()) {
    if (!drive.removable || drive.mediaPresent || !drive.optical) {
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
    const auto verb = safeRemovalVerb(drive, false);
    const auto name = driveName(drive);
    rows.append({{"targetId", drive.id},
                 {"driveId", drive.id},
                 {"name", name},
                 {"driveName", name},
                 {"stateText", tr("No media")},
                 {"mounted", false},
                 {"busy", busy},
                 {"canActivate", false},
                 {"classRank", static_cast<int>(StoragePolicy::classify(drive))},
                 {"iconName", StoragePolicy::iconName(drive)},
                 {"removalVerb", verbValue(verb)},
                 {"removalLabel", verbLabel(verb, name)},
                 {"mountPoint", QString()}});
  }
}
