qt_add_executable(file-browser-tests reader_test.cpp)
target_link_libraries(file-browser-tests PRIVATE HolonightFileBrowser::Core Qt6::Test GTest::gtest)
add_test(NAME file-browser-core COMMAND file-browser-tests)
set_tests_properties(file-browser-core PROPERTIES TIMEOUT 30)
add_test(NAME file-browser-installed-core COMMAND ${CMAKE_COMMAND}
  -DPROVIDER_BUILD=${CMAKE_BINARY_DIR} -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer
  -DQUICK=OFF "-DPROVIDER_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
  "-DPROVIDER_QML_PATH=${QML_IMPORT_PATH}" -P ${CMAKE_CURRENT_SOURCE_DIR}/installed-consumer.cmake)
if(BUILD_FILE_BROWSER_QUICK)
  add_test(NAME file-browser-installed-quick COMMAND ${CMAKE_COMMAND}
    -DPROVIDER_BUILD=${CMAKE_BINARY_DIR} -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer
    -DQUICK=ON "-DPROVIDER_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DPROVIDER_QML_PATH=${QML_IMPORT_PATH}" -P ${CMAKE_CURRENT_SOURCE_DIR}/installed-consumer.cmake)
  add_test(NAME file-browser-installed-qml-plugin COMMAND ${CMAKE_COMMAND}
    -DPROVIDER_BUILD=${CMAKE_BINARY_DIR} -DCONSUMER_SOURCE=${CMAKE_CURRENT_SOURCE_DIR}/consumer
    -DQUICK=ON -DPLUGIN_ONLY=ON "-DPROVIDER_PREFIX_PATH=${CMAKE_PREFIX_PATH}"
    "-DPROVIDER_QML_PATH=${QML_IMPORT_PATH}" -P ${CMAKE_CURRENT_SOURCE_DIR}/installed-consumer.cmake)
endif()
