# 固定日志后端版本与校验值；显式源码覆盖用于离线构建。
include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

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

FetchContent_Declare(spdlog
    URL https://codeload.github.com/gabime/spdlog/tar.gz/refs/tags/v1.17.0
    URL_HASH SHA256=d8862955c6d74e5846b3f580b1605d2428b11d97a410d86e2fb13e857cd3a744
)
FetchContent_GetProperties(spdlog)
if(NOT spdlog_POPULATED)
    # 兼容项目最低要求的 CMake 3.13。
    FetchContent_Populate(spdlog)
    function(vnapro_add_spdlog)
        # 使用函数作用域，不改变其他正式模块的共享库配置。
        set(BUILD_SHARED_LIBS OFF)
        add_subdirectory("${spdlog_SOURCE_DIR}" "${spdlog_BINARY_DIR}" EXCLUDE_FROM_ALL)
    endfunction()
    vnapro_add_spdlog()
endif()

# 即使上层启用了共享库，也保持本项目的日志后端为静态库。
get_target_property(VNAPRO_SPDLOG_TYPE spdlog TYPE)
if(NOT VNAPRO_SPDLOG_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "VnaPro requires a static spdlog backend")
endif()
