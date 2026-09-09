set_source_files_properties(../apps/files/Main.qml PROPERTIES QT_RESOURCE_ALIAS Main.qml)
qt_add_executable(files-smoke smoke.cpp directory_model_test.cpp directory_proxy_model_test.cpp
  directory_controller_test.cpp directory_performance_test.cpp initial_directory_test.cpp)
qt_add_resources(files-smoke test-config PREFIX "/" BASE ../apps/files FILES ../apps/files/qtquickcontrols2.conf)
target_link_libraries(files-smoke PRIVATE files-ui files-uiplugin Qt6::Test GTest::gtest)
target_compile_definitions(files-smoke PRIVATE FILES_FIXTURE_DIR="${CMAKE_CURRENT_BINARY_DIR}/fixtures/files")
add_test(NAME files-smoke COMMAND files-smoke)
set_tests_properties(files-smoke PROPERTIES ENVIRONMENT
  "QT_QPA_PLATFORM=offscreen;QSG_RHI_BACKEND=software;QML_IMPORT_PATH=${HOLONIGHT_QML_IMPORT_PATH};LD_LIBRARY_PATH=${HOLONIGHT_DEPENDENCY_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
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
