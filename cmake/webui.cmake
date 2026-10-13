# 前端只在输入或产物发生变化时构建；检查目标每次运行以修复被删除的 dist。
find_program(VNAPRO_NODE_EXECUTABLE NAMES node node.exe)
get_filename_component(node_directory "${VNAPRO_NODE_EXECUTABLE}" DIRECTORY)
find_program(VNAPRO_NPM_EXECUTABLE NAMES npm.cmd npm HINTS "${node_directory}")
if(NOT VNAPRO_NODE_EXECUTABLE OR NOT VNAPRO_NPM_EXECUTABLE)
    message(FATAL_ERROR "WebUI requires Node.js >= 22.13 and npm. Install them or set VNAPRO_BUNDLE_WEBUI=OFF for a SCPI-only bundle")
endif()

file(GLOB_RECURSE webui_sources CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/WebUI/src/*"
    "${CMAKE_SOURCE_DIR}/WebUI/public/*"
    "${CMAKE_SOURCE_DIR}/WebUI/scripts/*")
file(GLOB webui_configuration CONFIGURE_DEPENDS LIST_DIRECTORIES FALSE
    "${CMAKE_SOURCE_DIR}/WebUI/*.json"
    "${CMAKE_SOURCE_DIR}/WebUI/*.ts"
    "${CMAKE_SOURCE_DIR}/WebUI/.env*")
list(APPEND webui_sources ${webui_configuration}
    "${CMAKE_SOURCE_DIR}/WebUI/index.html"
    "${CMAKE_SOURCE_DIR}/cmake/build_webui.cmake")
list(SORT webui_sources)

set(VNAPRO_WEBUI_DIRECTORY "${CMAKE_SOURCE_DIR}/WebUI")
set(VNAPRO_WEBUI_CACHE "${CMAKE_BINARY_DIR}/WebUI/build-fingerprint.cmake")
set(VNAPRO_WEBUI_SOURCES "${webui_sources}")
configure_file("${CMAKE_SOURCE_DIR}/cmake/webui_inputs.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/webui-inputs.cmake" @ONLY)
add_custom_target(vna_webui
    COMMAND "${CMAKE_COMMAND}" "-DINPUT=${CMAKE_CURRENT_BINARY_DIR}/webui-inputs.cmake"
        -P "${CMAKE_SOURCE_DIR}/cmake/build_webui.cmake"
    COMMENT "Checking and building VnaPro WebUI"
    VERBATIM)
