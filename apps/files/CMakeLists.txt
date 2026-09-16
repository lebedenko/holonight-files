qt_add_library(files-ui STATIC)
qt_add_qml_module(files-ui
  URI HolonightFiles VERSION 1.0
  OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/HolonightFiles"
  IMPORT_PATH "${HOLONIGHT_QML_IMPORT_PATH}"
  QML_FILES InspectionKeys.js IconFallbacks.js SizeFormat.js Main.qml AppHeaderBar.qml PlacesPanel.qml DirectoryListing.qml PreviewPane.qml QuickLookOverlay.qml ModeStatusBar.qml
  SOURCES quick_look_presentation_model.h quick_look_presentation_model.cpp initial_directory.h initial_directory.cpp directory_model.h directory_model.cpp directory_proxy_model.h directory_proxy_model.cpp places_model.h places_model.cpp directory_controller.h directory_controller.cpp window_state.h preview_service.h preview_service.cpp thumbnail_service.h thumbnail_service.cpp exif_reader.h exif_reader.cpp text_preview_service.h text_preview_service.cpp preview_image_item.h preview_image_item.cpp vim_mode_controller.h vim_mode_controller.cpp name_validator.h name_validator.cpp fuzzy_matcher.h fuzzy_matcher.cpp clipboard_register.h file_operation_service.h file_operation_service.cpp trash_service.h trash_service.cpp task_manager.h task_manager.cpp icon_name_resolver.h icon_name_resolver.cpp jump_list.h jump_list.cpp icon_image_provider.h icon_image_provider.cpp
  RESOURCES icons/folder-fallback.svg icons/generic-file-fallback.svg icons/go-back.svg icons/go-forward.svg
  NO_IMPORT_SCAN
  NO_GENERATE_PLUGIN_SOURCE
  NO_PLUGIN_OPTIONAL
  CLASS_NAME HolonightFilesPlugin)
# Registers the image://icon/ provider on every engine that imports the module (see the file). The
# plugin must not be optional: Qt skips optional plugins of modules whose types are already registered.
target_sources(files-uiplugin PRIVATE holonight_files_plugin.cpp)
target_link_libraries(files-ui PUBLIC Qt6::Quick Qt6::QuickControls2 Qt6::Svg HolonightQt::Core HolonightQt::Controls PkgConfig::EXIF)
target_include_directories(files-ui PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
qt_add_executable(hn-files main.cpp)
qt_add_resources(hn-files files-config PREFIX "/" FILES qtquickcontrols2.conf)
target_link_libraries(hn-files PRIVATE files-ui files-uiplugin)
target_compile_definitions(hn-files PRIVATE FILES_VERSION="${PROJECT_VERSION}")
install(TARGETS hn-files RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

set_target_properties(hn-files PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
