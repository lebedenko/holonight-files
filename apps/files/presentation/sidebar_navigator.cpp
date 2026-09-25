#include "sidebar_navigator.h"

#include "devices_model.h"
#include "places_model.h"

#include <algorithm>

SidebarNavigator::SidebarNavigator(PlacesModel* places, DevicesModel* devices, QObject* parent)
    : QObject(parent), places_(places), devices_(devices) {
  for (auto* model : {static_cast<QAbstractItemModel*>(places), static_cast<QAbstractItemModel*>(devices)}) {
    connect(model, &QAbstractItemModel::rowsRemoved, this, &SidebarNavigator::clamp);
    connect(model, &QAbstractItemModel::rowsInserted, this, &SidebarNavigator::clamp);
    connect(model, &QAbstractItemModel::modelReset, this, &SidebarNavigator::clamp);
  }
  clamp();
}
int SidebarNavigator::placesCount() const { return places_ ? places_->rowCount() : 0; }
int SidebarNavigator::devicesCount() const { return devices_ ? devices_->rowCount() : 0; }
int SidebarNavigator::flatPosition() const {
  switch (section_) {
    case Places:
      return index_;
    case Devices:
      return placesCount() + index_;
    default:
      return -1;
  }
}
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
  emit revealRequested(section_, index_);
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
  auto section = section_;
  auto index = index_;
  if (section == Devices && devicesCount() == 0) {
    section = Places;
    index = placesCount() - 1;
  }
  if (section == Places && placesCount() == 0) {
    section = None;
  }
  if (section == None) {
    if (placesCount() > 0) {
      section = Places;
    } else if (devicesCount() > 0) {
      section = Devices;
    }
    index = section == None ? -1 : 0;
  } else {
    index = std::clamp(index, 0, (section == Places ? placesCount() : devicesCount()) - 1);
  }
  assign(section, index);
}
void SidebarNavigator::assign(int section, int index) {
  if (section == section_ && index == index_) {
    return;
  }
  section_ = section;
  index_ = index;
  emit cursorChanged();
}
