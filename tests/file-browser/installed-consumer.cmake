set(stage "${PROVIDER_BUILD}/file-browser-consumer-${QUICK}-${PLUGIN_ONLY}/prefix")
set(build "${PROVIDER_BUILD}/file-browser-consumer-${QUICK}-${PLUGIN_ONLY}/build")
execute_process(COMMAND "${CMAKE_COMMAND}" --install "${PROVIDER_BUILD}" --prefix "${stage}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result)
  message(FATAL_ERROR "Provider installation failed: ${output}${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" -S "${CONSUMER_SOURCE}" -B "${build}" -G Ninja
  "-DCMAKE_PREFIX_PATH=${stage};${PROVIDER_PREFIX_PATH}" "-DQUICK=${QUICK}" "-DPLUGIN_ONLY=${PLUGIN_ONLY}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result)
  message(FATAL_ERROR "Installed consumer configure failed: ${output}${error}")
endif()
execute_process(COMMAND "${CMAKE_COMMAND}" --build "${build}"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result)
  message(FATAL_ERROR "Installed consumer build failed: ${output}${error}")
endif()
string(REPLACE ";" ":" qml_path "${PROVIDER_QML_PATH}")
execute_process(COMMAND "${CMAKE_COMMAND}" -E env "QT_QPA_PLATFORM=offscreen" "QT_QUICK_BACKEND=software"
  "QML_IMPORT_PATH=${stage}/lib/qt6/qml:${qml_path}"
  "LD_LIBRARY_PATH=${stage}/lib:$ENV{LD_LIBRARY_PATH}" "${build}/consumer"
  RESULT_VARIABLE result OUTPUT_VARIABLE output ERROR_VARIABLE error)
if(result)
  message(FATAL_ERROR "Installed consumer runtime failed (${result}): ${output}${error}")
endif()
message(STATUS "Installed consumer passed (Quick=${QUICK})")
