#pragma once
#include <QAbstractListModel>
#include <QVariantMap>

#include <StorageController.h>

class DevicesModel : public QAbstractListModel {
  Q_OBJECT
  Q_PROPERTY(int count READ count NOTIFY changed)
  Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY changed)
  Q_PROPERTY(QString confirmationText READ confirmationText NOTIFY changed)
 public:
  explicit DevicesModel(QObject* parent = nullptr);
  DevicesModel(HoloNight::System::StorageController* controller, QObject* parent = nullptr);
  enum Role {
    TargetId = Qt::UserRole + 1,
    DriveId,
    Name,
    DriveName,
    State,
    Mounted,
    Busy,
    CanMount,
    CanUnmount,
    CanEject,
    CanPowerOff,
    CanActivate
  };
  int rowCount(const QModelIndex& parent = {}) const override;
  QVariant data(const QModelIndex& index, int role) const override;
  QHash<int, QByteArray> roleNames() const override;
  int count() const { return static_cast<int>(rows_.size()); }
  QString errorMessage() const { return error_message_; }
  QString confirmationText() const { return confirmation_text_; }
  void resolvedLocation(const QString& path, const QString& canonicalPath);
  void navigationChanged(const QString& path);
  void setInteractionEnabled(bool enabled);
  Q_INVOKABLE void activate(const QString& targetId);
  Q_INVOKABLE void mount(const QString& targetId);
  Q_INVOKABLE void unmount(const QString& targetId);
  Q_INVOKABLE void eject(const QString& targetId);
  Q_INVOKABLE void requestPowerOff(const QString& targetId);
  Q_INVOKABLE void confirmPowerOff();
  Q_INVOKABLE void cancelPowerOff();
 signals:
  void changed();
  void openRequested(const QString& path);
  void recoveryRequested();

 private:
  void refresh();
  void appendEmptyDrives(QList<QVariantMap>& rows) const;
  void scheduleRefresh();
  void trackLocation();
  bool visibleTarget(const QString& targetId, const char* capability) const;
  HoloNight::System::StorageController* controller_;
  QList<QVariantMap> rows_;
  QString error_message_;
  QString confirmation_text_;
  QString confirmation_drive_;
  QStringList confirmation_scope_;
  bool refresh_pending_ = false;
  bool interaction_enabled_ = true;
  QString activation_request_;
  QString current_path_;
  QString canonical_path_;
  QString current_volume_;
  QString current_mount_;
};
