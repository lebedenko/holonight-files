#pragma once

#include "devices_model.h"
#include "directory_controller.h"
#include "directory_proxy_model.h"
#include "places_model.h"
#include "preview_service.h"
#include "task_manager.h"
#include "vim_mode_controller.h"

#include <QtQml/qqmlregistration.h>

struct DirectoryControllerRegistration {
  Q_GADGET
  QML_FOREIGN(DirectoryController)
  QML_NAMED_ELEMENT(DirectoryController)
  QML_UNCREATABLE("Created by the application")
};

struct VimModeControllerRegistration {
  Q_GADGET
  QML_FOREIGN(VimModeController)
  QML_NAMED_ELEMENT(VimModeController)
  QML_UNCREATABLE("Created by the application")
};

struct DirectoryProxyModelRegistration {
  Q_GADGET
  QML_FOREIGN(DirectoryProxyModel)
  QML_NAMED_ELEMENT(DirectoryProxyModel)
  QML_UNCREATABLE("Created by the application")
};

struct PlacesModelRegistration {
  Q_GADGET
  QML_FOREIGN(PlacesModel)
  QML_NAMED_ELEMENT(PlacesModel)
  QML_UNCREATABLE("Created by the application")
};

struct TaskManagerRegistration {
  Q_GADGET
  QML_FOREIGN(TaskManager)
  QML_NAMED_ELEMENT(TaskManager)
  QML_UNCREATABLE("Created by the application")
};

struct PreviewServiceRegistration {
  Q_GADGET
  QML_FOREIGN(PreviewService)
  QML_NAMED_ELEMENT(PreviewService)
  QML_UNCREATABLE("Created by the application")
};

struct DevicesModelRegistration {
  Q_GADGET
  QML_FOREIGN(DevicesModel)
  QML_NAMED_ELEMENT(DevicesModel)
  QML_UNCREATABLE("Created by the application")
};
