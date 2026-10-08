include(CMakePackageConfigHelpers)
add_library(holonight_file_browser_core STATIC
  src/directory_scan.cpp src/directory_reader.cpp src/directory_proxy_model.cpp
  src/places/place_list.cpp src/places/user_dirs_parser.cpp src/icon_name_resolver.cpp
  include/HolonightFileBrowser/directory_reader.h include/HolonightFileBrowser/directory_proxy_model.h)
add_library(HolonightFileBrowser::Core ALIAS holonight_file_browser_core)
set_target_properties(holonight_file_browser_core PROPERTIES EXPORT_NAME Core POSITION_INDEPENDENT_CODE ON)
target_compile_features(holonight_file_browser_core PUBLIC cxx_std_23)
target_include_directories(holonight_file_browser_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_link_libraries(holonight_file_browser_core PUBLIC Qt6::Core)
qt_extract_metatypes(holonight_file_browser_core)
install(TARGETS holonight_file_browser_core EXPORT HolonightFileBrowserCoreTargets
  ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
  PATTERN quick.h EXCLUDE PATTERN icon_image_provider.h EXCLUDE)
install(EXPORT HolonightFileBrowserCoreTargets NAMESPACE HolonightFileBrowser::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HolonightFileBrowser)

if(BUILD_FILE_BROWSER_QUICK)
  find_package(Qt6 6.11 REQUIRED COMPONENTS Gui Quick Qml QuickControls2)
  find_package(HolonightQt CONFIG REQUIRED)
  qt_add_library(holonight_file_browser_quick SHARED)
  add_library(HolonightFileBrowser::Quick ALIAS holonight_file_browser_quick)
  set_target_properties(holonight_file_browser_quick PROPERTIES EXPORT_NAME Quick)
  set_source_files_properties(qml/DirectoryListing.qml PROPERTIES QT_RESOURCE_ALIAS DirectoryListing.qml)
  qt_add_qml_module(holonight_file_browser_quick
    URI Holonight.FileBrowser VERSION 1.0
    OUTPUT_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}/qml/Holonight/FileBrowser
    RESOURCE_PREFIX /qt/qml
    IMPORT_PATH ${QML_IMPORT_PATH}
    DEPENDENCIES QtQuick Holonight.Core Holonight.Controls
    QML_FILES qml/DirectoryListing.qml SOURCES qml/registrations.h src/quick.cpp src/icon_image_provider.cpp)
  target_include_directories(holonight_file_browser_quick PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/qml")
  target_link_libraries(holonight_file_browser_quick PUBLIC holonight_file_browser_core Qt6::Quick
    PRIVATE Qt6::QuickControls2 HolonightQt::Core HolonightQt::Controls)
  install(FILES include/HolonightFileBrowser/quick.h include/HolonightFileBrowser/icon_image_provider.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/HolonightFileBrowser)
  install(TARGETS holonight_file_browser_quick EXPORT HolonightFileBrowserQuickTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR})
  install(TARGETS holonight_file_browser_quickplugin
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/Holonight/FileBrowser)
  install(FILES
    ${CMAKE_CURRENT_BINARY_DIR}/qml/Holonight/FileBrowser/qmldir
    ${CMAKE_CURRENT_BINARY_DIR}/qml/Holonight/FileBrowser/holonight_file_browser_quick.qmltypes
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/Holonight/FileBrowser)
  install(FILES qml/DirectoryListing.qml DESTINATION ${CMAKE_INSTALL_LIBDIR}/qt6/qml/Holonight/FileBrowser)
  install(EXPORT HolonightFileBrowserQuickTargets NAMESPACE HolonightFileBrowser::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HolonightFileBrowser)
endif()
configure_package_config_file(${CMAKE_CURRENT_SOURCE_DIR}/HolonightFileBrowserConfig.cmake.in
  ${CMAKE_CURRENT_BINARY_DIR}/HolonightFileBrowserConfig.cmake
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HolonightFileBrowser)
write_basic_package_version_file(${CMAKE_CURRENT_BINARY_DIR}/HolonightFileBrowserConfigVersion.cmake
  VERSION ${PROJECT_VERSION} COMPATIBILITY SameMajorVersion)
install(FILES ${CMAKE_CURRENT_BINARY_DIR}/HolonightFileBrowserConfig.cmake
  ${CMAKE_CURRENT_BINARY_DIR}/HolonightFileBrowserConfigVersion.cmake
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/HolonightFileBrowser)
