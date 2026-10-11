# 固定正式 YAML 解析库与源码校验值；可通过 FetchContent 的源码覆盖选项离线构建。
include(FetchContent)

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()

set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "Build static yaml-cpp" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "Disable optional yaml-cpp contrib" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "Disable yaml-cpp installation" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "Preserve upstream yaml-cpp sources" FORCE)
set(YAML_CPP_DISABLE_UNINSTALL ON CACHE BOOL "Disable yaml-cpp uninstall target" FORCE)

FetchContent_Declare(yaml_cpp
    URL https://codeload.github.com/jbeder/yaml-cpp/tar.gz/refs/tags/yaml-cpp-0.9.0
    URL_HASH SHA256=25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a
)
FetchContent_GetProperties(yaml_cpp)
if(NOT yaml_cpp_POPULATED)
    # 保持与项目最低 CMake 3.13 版本兼容。
    FetchContent_Populate(yaml_cpp)
    add_subdirectory("${yaml_cpp_SOURCE_DIR}" "${yaml_cpp_BINARY_DIR}" EXCLUDE_FROM_ALL)
endif()

get_target_property(VNAPRO_YAML_CPP_TYPE yaml-cpp TYPE)
if(NOT VNAPRO_YAML_CPP_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "VnaPro requires a static yaml-cpp parser")
endif()
