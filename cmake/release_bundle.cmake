# 保留真实可执行目标，兼容 CLion CMake Application；无重新链接时仍同步资源。
# 迁移上一版默认值；用户指定的其他发布目录继续保留。
if(VNAPRO_RELEASE_DIR STREQUAL "${CMAKE_BINARY_DIR}/release")
    set(VNAPRO_RELEASE_DIR "${CMAKE_SOURCE_DIR}/release" CACHE PATH "VnaPro runtime bundle directory" FORCE)
endif()
set(VNAPRO_RELEASE_DIR "${CMAKE_SOURCE_DIR}/release" CACHE PATH "VnaPro runtime bundle directory")
option(VNAPRO_BUNDLE_WEBUI "Build WebUI and include it in the runtime bundle" ON)
if(CMAKE_CONFIGURATION_TYPES)
    set(VNAPRO_RELEASE_OUTPUT "${VNAPRO_RELEASE_DIR}/$<CONFIG>")
else()
    set(VNAPRO_RELEASE_OUTPUT "${VNAPRO_RELEASE_DIR}")
endif()

get_filename_component(VNAPRO_COMPILER_DIRECTORY "${CMAKE_CXX_COMPILER}" DIRECTORY)
set(VNAPRO_RUNTIME_DIRECTORIES "${CMAKE_RUNTIME_OUTPUT_DIRECTORY};${VNAPRO_COMPILER_DIRECTORY}")
set(VNAPRO_RUNTIME_EXTRA_FILES "")
if(MSVC)
    # 使用工具链自带运行库，不把 VC 运行库误当作 Windows 系统库排除。
    set(CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS_SKIP TRUE)
    set(CMAKE_INSTALL_DEBUG_LIBRARIES TRUE)
    set(CMAKE_INSTALL_UCRT_LIBRARIES TRUE)
    include(InstallRequiredSystemLibraries)
    set(VNAPRO_RUNTIME_EXTRA_FILES "${CMAKE_INSTALL_SYSTEM_RUNTIME_LIBS}")
    foreach(runtime IN LISTS VNAPRO_RUNTIME_EXTRA_FILES)
        get_filename_component(directory "${runtime}" DIRECTORY)
        list(APPEND VNAPRO_RUNTIME_DIRECTORIES "${directory}")
    endforeach()
    find_program(VNAPRO_DEPENDENCY_COMMAND dumpbin HINTS "${VNAPRO_COMPILER_DIRECTORY}")
    set(VNAPRO_DEPENDENCY_TOOL dumpbin)
else()
    set(VNAPRO_DEPENDENCY_COMMAND "${CMAKE_OBJDUMP}")
    set(VNAPRO_DEPENDENCY_TOOL objdump)
endif()
if(WIN32)
    set(VNAPRO_DEPENDENCY_PLATFORM windows+pe)
elseif(APPLE)
    set(VNAPRO_DEPENDENCY_PLATFORM macos+macho)
    set(VNAPRO_DEPENDENCY_TOOL otool)
    unset(VNAPRO_DEPENDENCY_COMMAND)
    find_program(VNAPRO_DEPENDENCY_COMMAND otool)
else()
    set(VNAPRO_DEPENDENCY_PLATFORM linux+elf)
endif()
if(NOT EXISTS "${VNAPRO_DEPENDENCY_COMMAND}")
    message(FATAL_ERROR "VnaPro runtime dependency inspection tool is missing")
endif()
list(REMOVE_DUPLICATES VNAPRO_RUNTIME_DIRECTORIES)

# TARGET_FILE 仅在生成输入文件时使用，避免资源目标反向依赖主程序形成环。
set(VNAPRO_RELEASE_EXECUTABLE "$<TARGET_FILE:vna_server>")
set(VNAPRO_RELEASE_DRIVERS "$<TARGET_FILE:tdc1vn08_drv_sim>;$<TARGET_FILE:tdc1vn10_drv_sim>")
get_target_property(VNAPRO_SPDLOG_SOURCE_DIR spdlog SOURCE_DIR)
get_target_property(VNAPRO_YAML_CPP_SOURCE_DIR yaml-cpp SOURCE_DIR)
configure_file("${CMAKE_SOURCE_DIR}/cmake/release_inputs.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/release-inputs.cmake.in" @ONLY)
set(VNAPRO_RELEASE_INPUT "${CMAKE_CURRENT_BINARY_DIR}/release-inputs-$<CONFIG>.cmake")
file(GENERATE OUTPUT "${VNAPRO_RELEASE_INPUT}"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/release-inputs.cmake.in")

add_custom_target(vna_release_resources
    COMMAND ${CMAKE_COMMAND} "-DINPUT=${VNAPRO_RELEASE_INPUT}" -DPHASE=resources
        -P "${CMAKE_SOURCE_DIR}/cmake/sync_release.cmake"
    DEPENDS tdc1vn08_drv_sim tdc1vn10_drv_sim
    COMMENT "Synchronizing VnaPro release resources"
    VERBATIM)
if(VNAPRO_BUNDLE_WEBUI)
    include("${CMAKE_SOURCE_DIR}/cmake/webui.cmake")
    add_dependencies(vna_release_resources vna_webui)
endif()
add_dependencies(vna_server vna_release_resources)
add_custom_command(TARGET vna_server POST_BUILD
    COMMAND ${CMAKE_COMMAND} "-DINPUT=${VNAPRO_RELEASE_INPUT}" -DPHASE=complete
        -P "${CMAKE_SOURCE_DIR}/cmake/sync_release.cmake"
    COMMENT "Bundling VnaPro executable and runtime dependencies"
    VERBATIM)
