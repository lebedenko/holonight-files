#include "sidebar_navigator.h"

#include "places_model.h"

#include <QFile>

#include <HolonightFileBrowser/storage/devices_model.h>
#include <algorithm>

SidebarNavigator::SidebarNavigator(PlacesModel* places, DevicesModel* devices, QObject* parent)
    : QObject(parent), places_(places), devices_(devices) {
  for (auto* model : {static_cast<QAbstractItemModel*>(places), static_cast<QAbstractItemModel*>(devices)}) {
    connect(model, &QAbstractItemModel::rowsRemoved, this, &SidebarNavigator::clamp);
    connect(model, &QAbstractItemModel::rowsInserted, this, &SidebarNavigator::clamp);
    connect(model, &QAbstractItemModel::modelReset, this, &SidebarNavigator::clamp);
    connect(model, &QAbstractItemModel::layoutChanged, this, &SidebarNavigator::clamp);
    connect(model, &QAbstractItemModel::rowsMoved, this, &SidebarNavigator::clamp);
  }
  clamp();
}
int SidebarNavigator::placesCount() const { return places_ ? places_->rowCount() : 0; }
int SidebarNavigator::devicesCount() const { return devices_ ? devices_->rowCount() : 0; }
int SidebarNavigator::flatPosition() const { return cursor_.index(); }
bool SidebarNavigator::moveDown() {
  const auto position = flatPosition();
  if (position + 1 >= flatCount()) {
    return false;
  }
  moveTo(position + 1);
  return true;
}
bool SidebarNavigator::moveUp() {
  const auto position = flatPosition();
  if (position < 0) {
    return moveDown();  // No cursor yet: either key lands on the first row.
  }
  if (position == 0) {
    return false;
  }
  moveTo(position - 1);
  return true;
}
void SidebarNavigator::moveTo(int position) {
  const auto places = placesCount();
  if (position < places) {
    assign(Places, position);
  } else {
    assign(Devices, position - places);
  }
  emit revealRequested(section(), index());
}
void SidebarNavigator::setCursor(int section, int index) {
  if (section == Places && placesCount() > 0) {
    assign(Places, std::clamp(index, 0, placesCount() - 1));
  } else if (section == Devices && devicesCount() > 0) {
    assign(Devices, std::clamp(index, 0, devicesCount() - 1));
  }
}
void SidebarNavigator::clear() { assign(None, -1); }
// Keeps the cursor on a real row as either model changes beneath it; the first row once one exists.
void SidebarNavigator::clamp() {
  QVector<QByteArray> ids;
  for (int row = 0; row < placesCount(); ++row) {
    ids.append("place:" + QFile::encodeName(places_->index(row).data(PlacesModel::PathRole).toString()));
  }
  for (int row = 0; row < devicesCount(); ++row) {
    ids.append("device:" + devices_->index(row).data(DevicesModel::TargetId).toString().toUtf8());
  }
  cursor_.reconcile(std::move(ids));
  emit cursorChanged();
}
void SidebarNavigator::assign(int section, int index) {
  if (section == None) {
    cursor_.clear();
  } else {
    cursor_.setIndex(section == Devices ? placesCount() + index : index);
  }
  emit cursorChanged();
}
void SidebarNavigator::navigate(int command, int count, qreal viewportHeight, const QVariantList& tops) {
  cursor_.move({.command = static_cast<Holonight::NavigationCommand>(command), .count = count},
               {.count = flatCount(), .columns = 1, .viewportHeight = viewportHeight, .rect = [&](int row) {
                  return QRectF(0, row < tops.size() ? tops[row].toReal() : row * 32, 1, 32);
                }});
  emit cursorChanged();
  emit revealRequested(section(), index());
}

bool SidebarNavigator::startsBookmarks(int row) const {
  return places_ && places_->index(row).data(PlacesModel::StartsBookmarksRole).toBool();
}
bool SidebarNavigator::startsDeviceGroup(int row) const {
  return devices_ && devices_->index(row).data(DevicesModel::GroupStart).toBool();
}
