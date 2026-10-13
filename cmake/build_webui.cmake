cmake_minimum_required(VERSION 3.16)
if(NOT DEFINED INPUT OR NOT EXISTS "${INPUT}")
    message(FATAL_ERROR "VnaPro WebUI build input is missing")
endif()
include("${INPUT}")
foreach(package vite vue-tsc typescript vue)
    if(NOT EXISTS "${WEBUI_DIRECTORY}/node_modules/${package}/package.json")
        message(FATAL_ERROR "WebUI dependency ${package} is missing. Run npm ci once in ${WEBUI_DIRECTORY}; subsequent CMake builds build WebUI automatically")
    endif()
endforeach()

# 多个构建树共用 WebUI/dist，序列化检查与生成，避免并发清空或写入同一产物。
file(LOCK "${WEBUI_DIRECTORY}/.vnapro-build.lock" GUARD PROCESS TIMEOUT 60
    RESULT_VARIABLE lock_result)
if(NOT lock_result STREQUAL "0")
    message(FATAL_ERROR "Cannot acquire WebUI build lock: ${lock_result}")
endif()
execute_process(COMMAND "${NODE_EXECUTABLE}" --version
    RESULT_VARIABLE node_result OUTPUT_VARIABLE node_version ERROR_VARIABLE node_error
    OUTPUT_STRIP_TRAILING_WHITESPACE)
if(NOT node_result EQUAL 0 OR NOT node_version MATCHES "^v([0-9]+)[.]([0-9]+)[.]([0-9]+)")
    message(FATAL_ERROR "Cannot read Node.js version: ${node_error}")
endif()
string(SUBSTRING "${node_version}" 1 -1 version)
if(version VERSION_LESS "22.13.0")
    message(FATAL_ERROR "WebUI requires Node.js >= 22.13; found ${node_version}")
endif()

# 内容指纹同时记录文件名，因此删除输入、安装依赖变化也会触发重建。
if(EXISTS "${WEBUI_DIRECTORY}/node_modules/.package-lock.json")
    list(APPEND WEBUI_SOURCES "${WEBUI_DIRECTORY}/node_modules/.package-lock.json")
endif()
set(source_contents "${node_version}\n${NODE_EXECUTABLE}\n${NPM_EXECUTABLE}\n")
foreach(source IN LISTS WEBUI_SOURCES)
    file(SHA256 "${source}" hash)
    string(APPEND source_contents "${source}:${hash}\n")
endforeach()
string(SHA256 source_fingerprint "${source_contents}")

function(webui_output_fingerprint output)
    file(GLOB_RECURSE files LIST_DIRECTORIES FALSE RELATIVE "${WEBUI_DIRECTORY}/dist"
        "${WEBUI_DIRECTORY}/dist/*")
    list(SORT files)
    set(contents "")
    foreach(relative IN LISTS files)
        file(SHA256 "${WEBUI_DIRECTORY}/dist/${relative}" hash)
        string(APPEND contents "${relative}:${hash}\n")
    endforeach()
    string(SHA256 fingerprint "${contents}")
    set(${output} "${fingerprint}" PARENT_SCOPE)
endfunction()

if(EXISTS "${WEBUI_CACHE}")
    include("${WEBUI_CACHE}")
endif()
webui_output_fingerprint(output_fingerprint)
if(EXISTS "${WEBUI_DIRECTORY}/dist/index.html" AND
    source_fingerprint STREQUAL previous_source_fingerprint AND
    output_fingerprint STREQUAL previous_output_fingerprint)
    message(STATUS "VnaPro WebUI is up to date")
    return()
endif()

message(STATUS "Building VnaPro WebUI with ${node_version}")
file(REMOVE "${WEBUI_CACHE}")
execute_process(COMMAND "${NPM_EXECUTABLE}" run build WORKING_DIRECTORY "${WEBUI_DIRECTORY}"
    RESULT_VARIABLE build_result)
if(NOT build_result EQUAL 0)
    message(FATAL_ERROR "WebUI production build failed; release resources were not updated")
endif()
if(NOT EXISTS "${WEBUI_DIRECTORY}/dist/index.html")
    message(FATAL_ERROR "WebUI production build did not produce dist/index.html")
endif()
webui_output_fingerprint(output_fingerprint)
get_filename_component(cache_directory "${WEBUI_CACHE}" DIRECTORY)
file(MAKE_DIRECTORY "${cache_directory}")
file(WRITE "${WEBUI_CACHE}"
    "set(previous_source_fingerprint ${source_fingerprint})\nset(previous_output_fingerprint ${output_fingerprint})\n")
