qt_add_library(files-ui STATIC)
qt_add_qml_module(files-ui
  URI HolonightFiles VERSION 1.0
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/HolonightFiles"
  IMPORT_PATH "${HOLONIGHT_QML_IMPORT_PATH}"
  QML_FILES InspectionKeys.js Main.qml PlacesPanel.qml DirectoryListing.qml PreviewPane.qml QuickLookOverlay.qml
  SOURCES initial_directory.h initial_directory.cpp directory_model.h directory_model.cpp directory_proxy_model.h directory_proxy_model.cpp places_model.h places_model.cpp directory_controller.h directory_controller.cpp window_state.h preview_service.h preview_service.cpp thumbnail_service.h thumbnail_service.cpp exif_reader.h exif_reader.cpp text_preview_service.h text_preview_service.cpp preview_image_item.h preview_image_item.cpp
  NO_IMPORT_SCAN)
target_link_libraries(files-ui PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Svg HolonightQt::Core HolonightQt::Controls PkgConfig::EXIF)
target_include_directories(files-ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
qt_add_executable(holonight-files main.cpp)
qt_add_resources(holonight-files files-config PREFIX "/" FILES qtquickcontrols2.conf)
target_link_libraries(holonight-files PRIVATE files-ui files-uiplugin)
target_compile_definitions(holonight-files PRIVATE FILES_VERSION="${PROJECT_VERSION}")
install(TARGETS holonight-files RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set_target_properties(holonight-files PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
