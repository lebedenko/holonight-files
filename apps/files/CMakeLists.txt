add_library(files-private STATIC
  application/directory_controller.cpp
  application/directory_controller.h
  application/editing_session.cpp
  application/editing_session.h
  application/file_command_router.cpp
  application/file_command_router.h
  application/initial_directory.cpp
  application/initial_directory.h
  application/navigation_session.cpp
  application/navigation_session.h
  application/session_lifecycle.cpp
  application/session_lifecycle.h
  application/vim_mode_controller.cpp
  application/vim_mode_controller.h
  browsing/directory_model.cpp
  browsing/directory_model.h
  browsing/directory_proxy_model.cpp
  browsing/directory_proxy_model.h
  browsing/fuzzy_matcher.cpp
  browsing/fuzzy_matcher.h
  browsing/jump_list.cpp
  browsing/jump_list.h
  browsing/location_classifier.cpp
  browsing/location_classifier.h
  browsing/name_validator.cpp
  browsing/name_validator.h
  browsing/places_model.cpp
  browsing/places_model.h
  browsing/restore_outcome.cpp
  browsing/restore_outcome.h
  operations/clipboard_register.h
  operations/file_operation_service.cpp
  operations/file_operation_service.h
  operations/task_manager.cpp
  operations/task_manager.h
  operations/trash_service.cpp
  operations/trash_service.h
  places/bookmark_store.cpp
  places/bookmark_store.h
  places/place_availability_checker.cpp
  places/place_availability_checker.h
  places/user_dirs_parser.cpp
  places/user_dirs_parser.h
  preview/exif_reader.cpp
  preview/exif_reader.h
  preview/preview_selection.cpp
  preview/preview_selection.h
  preview/preview_service.cpp
  preview/preview_service.h
  preview/text_line_model.cpp
  preview/text_line_model.h
  preview/text_lines.cpp
  preview/text_lines.h
  preview/text_preview_service.cpp
  preview/text_preview_service.h
  preview/thumbnail_service.cpp
  preview/thumbnail_service.h
  settings/app_settings.cpp
  settings/app_settings.h
  settings/general_settings.cpp
  settings/general_settings.h
  settings/setting.h
  settings/settings_registry.cpp
  settings/settings_registry.h
  settings/toml_document.cpp
  settings/toml_document.h
  settings/warning_sink.cpp
  settings/warning_sink.h
  settings/xdg_paths.cpp
  settings/xdg_paths.h
  state/last_location_tracker.cpp
  state/last_location_tracker.h
  state/state_store.cpp
  state/state_store.h
)
target_include_directories(files-private PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}"
  "${CMAKE_CURRENT_SOURCE_DIR}/application"
  "${CMAKE_CURRENT_SOURCE_DIR}/browsing"
  "${CMAKE_CURRENT_SOURCE_DIR}/operations"
  "${CMAKE_CURRENT_SOURCE_DIR}/preview"
  "${CMAKE_CURRENT_SOURCE_DIR}/presentation"
  "${CMAKE_CURRENT_SOURCE_DIR}/settings"
  "${CMAKE_CURRENT_SOURCE_DIR}/state"
  "${CMAKE_CURRENT_SOURCE_DIR}/places"
)
target_link_libraries(files-private PUBLIC Qt6::Quick Qt6::Svg PRIVATE tomlplusplus::tomlplusplus PkgConfig::EXIF)

qt_extract_metatypes(files-private)

function(add_files_application target)
  set(app_dir "${PROJECT_SOURCE_DIR}/apps/files")
  set(module_qml)
  foreach(file
    qml/Main.qml
    qml/AppHeaderBar.qml
    qml/listing/DirectoryListing.qml
    qml/places/PlacesPanel.qml
    qml/inspection/PreviewPane.qml
    qml/inspection/QuickLookOverlay.qml
    qml/status/ModeStatusBar.qml
)
    get_filename_component(alias "${file}" NAME)
    set_source_files_properties("${app_dir}/${file}" PROPERTIES QT_RESOURCE_ALIAS "${alias}")
    list(APPEND module_qml "${app_dir}/${file}")
  endforeach()
  set(module_icons)
  foreach(icon folder-fallback generic-file-fallback go-back go-forward warning-fallback)
    set(file "${app_dir}/icons/${icon}.svg")
    set_source_files_properties("${file}" PROPERTIES QT_RESOURCE_ALIAS "icons/${icon}.svg")
    list(APPEND module_icons "${file}")
  endforeach()
  qt_add_executable(${target} ${ARGN})
  qt_add_qml_module(${target}
    URI HolonightFiles VERSION 1.0
    OUTPUT_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/qml/${target}/HolonightFiles"
    RESOURCE_PREFIX "/qt/qml"
    IMPORT_PATH "${CMAKE_CURRENT_BINARY_DIR}/qml/${target}" ${QML_IMPORT_PATH}
    DEPENDENCIES QtQuick
    QML_FILES ${module_qml}
    RESOURCES ${module_icons}
    SOURCES
      "${app_dir}/presentation/engine_setup.cpp"
      "${app_dir}/presentation/engine_setup.h"
      "${app_dir}/presentation/icon_fallbacks.cpp"
      "${app_dir}/presentation/icon_fallbacks.h"
      "${app_dir}/presentation/icon_image_provider.cpp"
      "${app_dir}/presentation/icon_image_provider.h"
      "${app_dir}/presentation/icon_name_resolver.cpp"
      "${app_dir}/presentation/icon_name_resolver.h"
      "${app_dir}/presentation/inspection_keys.cpp"
      "${app_dir}/presentation/inspection_keys.h"
      "${app_dir}/presentation/preview_image_item.cpp"
      "${app_dir}/presentation/preview_image_item.h"
      "${app_dir}/presentation/quick_look_presentation_model.cpp"
      "${app_dir}/presentation/quick_look_presentation_model.h"
      "${app_dir}/presentation/size_format.cpp"
      "${app_dir}/presentation/size_format.h"
      "${app_dir}/presentation/window_event_filter.cpp"
      "${app_dir}/presentation/window_event_filter.h"
      "${app_dir}/presentation/window_state.h"
      "${app_dir}/presentation/qml_types.h"
    NO_IMPORT_SCAN)
  qt_add_resources(${target} files-config PREFIX "/" BASE "${app_dir}" FILES "${app_dir}/qtquickcontrols2.conf")
  target_link_libraries(${target} PRIVATE files-private Qt6::QuickControls2 HolonightQt::Core HolonightQt::Controls)
  target_compile_definitions(${target} PRIVATE FILES_VERSION="${PROJECT_VERSION}")
endfunction()

add_files_application(hn-files main.cpp)
install(TARGETS hn-files RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
