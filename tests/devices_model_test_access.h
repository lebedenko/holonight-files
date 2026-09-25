#pragma once

#include "devices_model.h"

// The capacity refresh timer, so a test can shorten the staleness interval instead of waiting it out.
struct DevicesModelTestAccess {
  static QTimer& capacityTimer(DevicesModel& model) { return model.capacity_timer_; }
};
