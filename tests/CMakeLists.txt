add_files_application(files-smoke smoke.cpp separator_window_test.cpp size_format_test.cpp inspection_helpers_test.cpp directory_model_test.cpp directory_proxy_model_test.cpp
  directory_controller_test.cpp directory_performance_test.cpp initial_directory_test.cpp
  thumbnail_service_test.cpp exif_reader_test.cpp text_preview_service_test.cpp text_lines_test.cpp text_line_model_test.cpp preview_service_test.cpp
  quick_look_presentation_model_test.cpp preview_integration_test.cpp preview_decode_limits_test.cpp preview_stress_test.cpp
  name_validator_test.cpp fuzzy_matcher_test.cpp vim_mode_controller_test.cpp
  clipboard_register_test.cpp file_operation_service_test.cpp trash_service_test.cpp
  task_manager_test.cpp directory_controller_file_ops_test.cpp icon_name_resolver_test.cpp icon_image_provider_test.cpp
  jump_list_test.cpp history_navigation_window_test.cpp places_model_test.cpp places_window_test.cpp
  warning_sink_test.cpp xdg_paths_test.cpp toml_document_test.cpp settings_registry_test.cpp app_settings_test.cpp
  location_classifier_test.cpp state_store_test.cpp last_location_tracker_test.cpp user_dirs_parser_test.cpp
  bookmark_store_test.cpp place_availability_checker_test.cpp)
find_package(Qt6 REQUIRED COMPONENTS OpenGL)
pkg_check_modules(EXIF REQUIRED IMPORTED_TARGET libexif)
target_link_libraries(files-smoke PRIVATE Qt6::Test Qt6::OpenGL GTest::gtest PkgConfig::EXIF)
target_compile_definitions(files-smoke PRIVATE FILES_FIXTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}/fixtures/files"
  FILES_SOURCE_DIR="${CMAKE_SOURCE_DIR}")
add_test(NAME files-smoke COMMAND files-smoke)
set_tests_properties(files-smoke PROPERTIES ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${FILES_RUNTIME_QML_PATH}")

# A fresh headless process prevents earlier rendered tests from hiding copy memory growth.
qt_add_executable(files-memory-benchmark file_operation_memory_test.cpp)
target_link_libraries(files-memory-benchmark PRIVATE files-private GTest::gtest)
add_test(NAME files-memory-benchmark COMMAND files-memory-benchmark)
set_tests_properties(files-memory-benchmark PROPERTIES TIMEOUT 180)
set_target_properties(files-memory-benchmark PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

# Cross-filesystem-boundary coverage (EXDEV move fallback, per-partition trash directory
# selection, ENOSPC) runs in its own process: it unshares into a fresh user+mount namespace to
# create genuinely distinct filesystems without root (see fs_isolation.h), which must never share
# a process with files-smoke's chmod-000 permission-denied fixtures.
qt_add_executable(files-fsops-smoke cross_filesystem_test.cpp fs_isolation.cpp)
target_link_libraries(files-fsops-smoke PRIVATE files-private GTest::gtest)
add_test(NAME files-fsops-smoke COMMAND files-fsops-smoke)
set_tests_properties(files-fsops-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

# T-118/T-119/T-122: a rendered VISUAL-selection trash task needs an executable-owned QML module
# and fs_isolation's real distinct filesystems in the same process. Kept separate
# from both files-smoke (this binary's unshare() would grant CAP_DAC_OVERRIDE over files-smoke's
# chmod-000 permission fixtures) and files-fsops-smoke (no QML/rendering dependency there).
add_files_application(files-fsops-window-smoke window_cross_filesystem_test.cpp fs_isolation.cpp)
target_link_libraries(files-fsops-window-smoke PRIVATE Qt6::Test GTest::gtest)
add_test(NAME files-fsops-window-smoke COMMAND files-fsops-window-smoke)
set_tests_properties(files-fsops-window-smoke PROPERTIES ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${FILES_RUNTIME_QML_PATH}"
  TIMEOUT 60)
set_target_properties(files-fsops-window-smoke PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

add_test(NAME files-help COMMAND hn-files --help)
add_test(NAME files-version COMMAND hn-files --version)
add_test(NAME files-reject-argument COMMAND hn-files one two)
set_tests_properties(files-help files-version files-reject-argument PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(files-reject-argument PROPERTIES WILL_FAIL TRUE)

find_program(FILES_BASH_EXECUTABLE bash REQUIRED)
add_test(NAME files-provider-revisions COMMAND ${FILES_BASH_EXECUTABLE}
  "${CMAKE_CURRENT_SOURCE_DIR}/shared_provider_revisions_test.sh" "${CMAKE_SOURCE_DIR}")
set_tests_properties(files-provider-revisions PROPERTIES TIMEOUT 30)

# Shadow the installed module with an invalid provider to exercise startup failure.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/invalid-import/Holonight/Controls/qmldir"
  "module Holonight.Controls\nHnApplicationWindow 1.0 Missing.qml\n")
add_test(NAME files-qml-failure COMMAND ${CMAKE_COMMAND} -E env
  "QT_QPA_PLATFORM=offscreen"
  "QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}/invalid-import:${FILES_RUNTIME_QML_PATH}"
  $<TARGET_FILE:hn-files>)
set_tests_properties(files-qml-failure PROPERTIES WILL_FAIL TRUE TIMEOUT 5)

set_target_properties(files-smoke PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
set_tests_properties(files-smoke PROPERTIES TIMEOUT 180)

add_test(NAME files-fusion-editing COMMAND files-smoke --gtest_filter=Files.RuntimeStyleEditingShowsValidationAndCommits)
set_tests_properties(files-fusion-editing PROPERTIES ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QML_IMPORT_PATH=${FILES_RUNTIME_QML_PATH};FILES_TEST_STYLE=Fusion" TIMEOUT 15)

# Opt in on hosts with a real GPU; each test asserts the backend rather than accepting fallback.
option(FILES_ACCELERATED_SEPARATOR_TESTS "Run GPU separator acceptance (requires OpenGL)" OFF)
foreach(ratio IN ITEMS 1.0 1.25 1.5 1.5625 1.75 2.0)
  add_test(NAME files-separator-software-${ratio}
    COMMAND files-smoke --gtest_filter=Files.WindowSeparatorJunctions)
  set_tests_properties(files-separator-software-${ratio} PROPERTIES ENVIRONMENT
    "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=software;QT_SCALE_FACTOR=${ratio};QML_IMPORT_PATH=${FILES_RUNTIME_QML_PATH}")
  if(FILES_ACCELERATED_SEPARATOR_TESTS)
    add_test(NAME files-separator-opengl-${ratio}
      COMMAND files-smoke --gtest_filter=Files.WindowSeparatorJunctions)
    set_tests_properties(files-separator-opengl-${ratio} PROPERTIES LABELS accelerated ENVIRONMENT
      "QT_QPA_PLATFORM=offscreen;QT_QUICK_BACKEND=rhi;QSG_RHI_BACKEND=opengl;QT_SCALE_FACTOR=${ratio};FILES_SEPARATOR_BACKEND=opengl;QML_IMPORT_PATH=${FILES_RUNTIME_QML_PATH}")
  endif()
endforeach()
