set_source_files_properties(../apps/files/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_executable(files-smoke smoke.cpp directory_model_test.cpp directory_proxy_model_test.cpp
  directory_controller_test.cpp directory_performance_test.cpp initial_directory_test.cpp
  thumbnail_service_test.cpp exif_reader_test.cpp text_preview_service_test.cpp preview_service_test.cpp
  preview_integration_test.cpp preview_decode_limits_test.cpp preview_stress_test.cpp
  name_validator_test.cpp fuzzy_matcher_test.cpp vim_mode_controller_test.cpp
  clipboard_register_test.cpp file_operation_service_test.cpp trash_service_test.cpp
  task_manager_test.cpp directory_controller_file_ops_test.cpp)
qt_add_resources(files-smoke test-config PREFIX "/" BASE ../apps/files FILES ../apps/files/qtquickcontrols2.conf)
target_link_libraries(files-smoke PRIVATE files-ui files-uiplugin Qt6::Test GTest::gtest)
target_compile_definitions(files-smoke PRIVATE FILES_FIXTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}/fixtures/files")
add_test(NAME files-smoke COMMAND files-smoke)
set_tests_properties(files-smoke PROPERTIES ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;QML_IMPORT_PATH=${HOLONIGHT_QML_IMPORT_PATH};LD_LIBRARY_PATH=${HOLONIGHT_DEPENDENCY_PREFIX}/${CMAKE_INSTALL_LIBDIR}")

# A fresh headless process prevents earlier rendered tests from hiding copy memory growth.
qt_add_executable(files-memory-benchmark file_operation_memory_test.cpp)
target_link_libraries(files-memory-benchmark PRIVATE files-ui GTest::gtest)
add_test(NAME files-memory-benchmark COMMAND files-memory-benchmark)
set_tests_properties(files-memory-benchmark PROPERTIES TIMEOUT 180)
set_target_properties(files-memory-benchmark PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

# Cross-filesystem-boundary coverage (EXDEV move fallback, per-partition trash directory
# selection, ENOSPC) runs in its own process: it unshares into a fresh user+mount namespace to
# create genuinely distinct filesystems without root (see fs_isolation.h), which must never share
# a process with files-smoke's chmod-000 permission-denied fixtures.
qt_add_executable(files-fsops-smoke cross_filesystem_test.cpp fs_isolation.cpp)
target_link_libraries(files-fsops-smoke PRIVATE files-ui GTest::gtest)
add_test(NAME files-fsops-smoke COMMAND files-fsops-smoke)
set_tests_properties(files-fsops-smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 60)

# T-118/T-119/T-122: a rendered VISUAL-selection trash task needs the real QML window (files-ui/
# files-uiplugin) *and* fs_isolation's real distinct filesystems in the same process. Kept separate
# from both files-smoke (this binary's unshare() would grant CAP_DAC_OVERRIDE over files-smoke's
# chmod-000 permission fixtures) and files-fsops-smoke (no QML/rendering dependency there).
qt_add_executable(files-fsops-window-smoke window_cross_filesystem_test.cpp fs_isolation.cpp)
target_link_libraries(files-fsops-window-smoke PRIVATE files-ui files-uiplugin Qt6::Test GTest::gtest)
add_test(NAME files-fsops-window-smoke COMMAND files-fsops-window-smoke)
set_tests_properties(files-fsops-window-smoke PROPERTIES ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;QML_IMPORT_PATH=${HOLONIGHT_QML_IMPORT_PATH};LD_LIBRARY_PATH=${HOLONIGHT_DEPENDENCY_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
  TIMEOUT 60)
set_target_properties(files-fsops-window-smoke PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)

add_test(NAME files-help COMMAND holonight-files --help)
add_test(NAME files-version COMMAND holonight-files --version)
add_test(NAME files-reject-argument COMMAND holonight-files one two)
set_tests_properties(files-help files-version files-reject-argument PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
set_tests_properties(files-reject-argument PROPERTIES WILL_FAIL TRUE)

# Shadow the installed module with an invalid provider to exercise startup failure.
file(WRITE "${CMAKE_CURRENT_BINARY_DIR}/invalid-import/Holonight/Controls/qmldir"
  "module Holonight.Controls\nHnApplicationWindow 1.0 Missing.qml\n")
add_test(NAME files-qml-failure COMMAND ${CMAKE_COMMAND} -E env
  "QT_QPA_PLATFORM=offscreen"
  "QML_IMPORT_PATH=${CMAKE_CURRENT_BINARY_DIR}/invalid-import:${HOLONIGHT_QML_IMPORT_PATH}"
  "LD_LIBRARY_PATH=${HOLONIGHT_DEPENDENCY_PREFIX}/${CMAKE_INSTALL_LIBDIR}"
  $<TARGET_FILE:holonight-files>)
set_tests_properties(files-qml-failure PROPERTIES WILL_FAIL TRUE TIMEOUT 5)

set_target_properties(files-smoke PROPERTIES QT_QML_MODULE_NO_IMPORT_SCAN TRUE)
set_tests_properties(files-smoke PROPERTIES TIMEOUT 180)
