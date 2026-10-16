#pragma once

#include "directory_controller.h"
#include "directory_proxy_model.h"
#include "path_finder_model.h"
#include "places_model.h"
#include "sidebar_navigator.h"
#include "task_manager.h"
#include "vim_mode_controller.h"

#include <QtQml/qqmlregistration.h>

#include <HolonightFileBrowser/preview/preview_service.h>
#include <HolonightFileBrowser/storage/devices_model.h>

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
  QML_FOREIGN(FilesDirectoryProxyModel)
  QML_NAMED_ELEMENT(DirectoryProxyModel)
  QML_UNCREATABLE("Created by the application")
};

struct PlacesModelRegistration {
  Q_GADGET
  QML_FOREIGN(PlacesModel)
  QML_NAMED_ELEMENT(PlacesModel)
  QML_UNCREATABLE("Created by the application")
};

struct PathFinderModelRegistration {
  Q_GADGET
  QML_FOREIGN(PathFinderModel)
  QML_NAMED_ELEMENT(PathFinderModel)
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

struct SidebarNavigatorRegistration {
  Q_GADGET
  QML_FOREIGN(SidebarNavigator)
  QML_NAMED_ELEMENT(SidebarNavigator)
  QML_UNCREATABLE("Created by the application")
};

#include <HolonightFileBrowser/preview_quick/preview_image_item.h>
struct PreviewImageItemRegistration {
  Q_GADGET
  QML_FOREIGN(PreviewImageItem)
  QML_NAMED_ELEMENT(PreviewImageItem)
};
