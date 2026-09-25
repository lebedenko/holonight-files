#pragma once
#include <QObject>
#include <QPointer>

class DevicesModel;
class PlacesModel;

// The sidebar's single keyboard cursor across Places and Devices (device-actions REQ-F-049..053).
// Both lists bind their currentIndex to it and handle no movement keys themselves, so two sections
// can never both appear focused: there is only one position.
class SidebarNavigator : public QObject {
  Q_OBJECT
  Q_PROPERTY(int section READ section NOTIFY cursorChanged)
  Q_PROPERTY(int index READ index NOTIFY cursorChanged)
 public:
  enum Section { None = -1, Places = 0, Devices = 1 };
  Q_ENUM(Section)
  SidebarNavigator(PlacesModel* places, DevicesModel* devices, QObject* parent = nullptr);
  int section() const { return section_; }
  int index() const { return index_; }
  // Step through [Places rows...][Devices rows...]; false, and no change, when there is nowhere to go.
  Q_INVOKABLE bool moveDown();
  Q_INVOKABLE bool moveUp();
  // Pointer focus and delegate clicks; out-of-range positions are clamped.
  Q_INVOKABLE void setCursor(int section, int index);
  Q_INVOKABLE void clear();
 signals:
  void cursorChanged();
  void revealRequested(int section, int index);

 private:
  int placesCount() const;
  int devicesCount() const;
  int flatCount() const { return placesCount() + devicesCount(); }
  int flatPosition() const;
  void moveTo(int position);
  void clamp();
  void assign(int section, int index);
  QPointer<PlacesModel> places_;
  QPointer<DevicesModel> devices_;
  int section_ = None;
  int index_ = -1;
};
