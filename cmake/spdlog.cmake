# 固定日志后端版本与校验值，源码只来自仓库内压缩包。
include("${CMAKE_CURRENT_LIST_DIR}/local_archive.cmake")

set(SPDLOG_BUILD_ALL OFF CACHE BOOL "Disable optional spdlog artifacts" FORCE)
set(SPDLOG_BUILD_SHARED OFF CACHE BOOL "Build static spdlog" FORCE)
set(SPDLOG_BUILD_EXAMPLE OFF CACHE BOOL "Disable spdlog examples" FORCE)
set(SPDLOG_BUILD_EXAMPLE_HO OFF CACHE BOOL "Disable header-only examples" FORCE)
set(SPDLOG_BUILD_TESTS OFF CACHE BOOL "Disable spdlog tests" FORCE)
set(SPDLOG_BUILD_TESTS_HO OFF CACHE BOOL "Disable header-only tests" FORCE)
set(SPDLOG_BUILD_BENCH OFF CACHE BOOL "Disable spdlog benchmarks" FORCE)
set(SPDLOG_INSTALL OFF CACHE BOOL "Disable spdlog installation" FORCE)
set(SPDLOG_USE_STD_FORMAT OFF CACHE BOOL "Use bundled fmt with C++14" FORCE)
set(SPDLOG_FMT_EXTERNAL OFF CACHE BOOL "Use bundled fmt" FORCE)
set(SPDLOG_FMT_EXTERNAL_HO OFF CACHE BOOL "Use bundled fmt" FORCE)
set(SPDLOG_NO_EXCEPTIONS OFF CACHE BOOL "Handle log failures without aborting acquisition" FORCE)
if(WIN32)
    set(SPDLOG_WCHAR_FILENAMES ON CACHE BOOL "Use Unicode Windows log file paths" FORCE)
else()
    set(SPDLOG_WCHAR_FILENAMES OFF CACHE BOOL "Use UTF-8 native log file paths" FORCE)
endif()

vnapro_extract_local_archive(
    "spdlog-1.17.0"
    "spdlog-1.17.0"
    "d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744"
    VNAPRO_SPDLOG_SOURCE_DIR
)
function(vnapro_add_spdlog)
    # 使用函数作用域，不改变其他正式模块的共享库配置。
    set(BUILD_SHARED_LIBS OFF)
    add_subdirectory("${VNAPRO_SPDLOG_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third-part/spdlog" EXCLUDE_FROM_ALL)
endfunction()
vnapro_add_spdlog()

# 即使上层启用了共享库，也保持本项目的日志后端为静态库。
get_target_property(VNAPRO_SPDLOG_TYPE spdlog TYPE)
if(NOT VNAPRO_SPDLOG_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "VnaPro requires a static spdlog backend")
endif()
