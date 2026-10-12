# 固定 YAML 解析库与校验值，源码只来自仓库内压缩包。
include("${CMAKE_CURRENT_LIST_DIR}/local_archive.cmake")

set(YAML_BUILD_SHARED_LIBS OFF CACHE BOOL "Build static yaml-cpp" FORCE)
set(YAML_CPP_BUILD_CONTRIB OFF CACHE BOOL "Disable optional yaml-cpp contrib" FORCE)
set(YAML_CPP_BUILD_TOOLS OFF CACHE BOOL "Disable yaml-cpp tools" FORCE)
set(YAML_CPP_BUILD_TESTS OFF CACHE BOOL "Disable yaml-cpp tests" FORCE)
set(YAML_CPP_INSTALL OFF CACHE BOOL "Disable yaml-cpp installation" FORCE)
set(YAML_CPP_FORMAT_SOURCE OFF CACHE BOOL "Preserve upstream yaml-cpp sources" FORCE)
set(YAML_CPP_DISABLE_UNINSTALL ON CACHE BOOL "Disable yaml-cpp uninstall target" FORCE)

vnapro_extract_local_archive(
    "yaml-cpp-0.9.0"
    "yaml-cpp-yaml-cpp-0.9.0"
    "25cb043240f828a8c51beb830569634bc7ac603978e0f69d6b63558dadefd49a"
    VNAPRO_YAML_CPP_SOURCE_DIR
)
add_subdirectory("${VNAPRO_YAML_CPP_SOURCE_DIR}" "${CMAKE_BINARY_DIR}/third-part/yaml_cpp" EXCLUDE_FROM_ALL)

get_target_property(VNAPRO_YAML_CPP_TYPE yaml-cpp TYPE)
if(NOT VNAPRO_YAML_CPP_TYPE STREQUAL "STATIC_LIBRARY")
    message(FATAL_ERROR "VnaPro requires a static yaml-cpp parser")
endif()
