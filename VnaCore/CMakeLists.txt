cmake_minimum_required(VERSION 3.13)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(VnaCore LANGUAGES CXX)
    include(CTest)
endif()

find_package(Threads REQUIRED)
if(NOT TARGET vna_log)
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../VnaLog" "${CMAKE_CURRENT_BINARY_DIR}/vna_log")
endif()
add_library(vna_core STATIC
    src/acquisition_core.cpp
    src/sweep_plan_validation.cpp
    src/device/plan_preparation_error.cpp
    src/device/dynamic_library.cpp
    src/device/board/board_library.cpp
    src/device/board/board_device_session.cpp
    src/device/native/native_library.cpp
    src/device/native/native_device_session.cpp
)
target_compile_features(vna_core PUBLIC cxx_std_14)
set_target_properties(vna_core PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(vna_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../VnaNativeSimulator/include
        ${CMAKE_CURRENT_SOURCE_DIR}/../VnaBoardSimulator/include
)
target_link_libraries(vna_core PRIVATE Threads::Threads ${CMAKE_DL_LIBS} vna_log)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_library(vna_board_hardware_probe SHARED
        tests/hardware_board/probe_exports.cpp tests/hardware_board/probe_state.cpp)
    target_include_directories(vna_board_hardware_probe PRIVATE tests/hardware_board)
    target_compile_features(vna_board_hardware_probe PRIVATE cxx_std_14)
    target_link_libraries(vna_board_hardware_probe PRIVATE Threads::Threads)
    set_target_properties(vna_board_hardware_probe PROPERTIES
        CXX_VISIBILITY_PRESET hidden VISIBILITY_INLINES_HIDDEN ON WINDOWS_EXPORT_ALL_SYMBOLS OFF)
    if(UNIX AND NOT APPLE)
        target_link_options(vna_board_hardware_probe PRIVATE
            "LINKER:--version-script=${CMAKE_CURRENT_SOURCE_DIR}/tests/hardware_board/probe_exports.map")
    endif()
    add_executable(vna_core_hardware_board_tests
        tests/hardware_board/hardware_board_test.cpp tests/hardware_board/test_environment.cpp
        tests/hardware_board/test_support.cpp tests/hardware_board/profile_test.cpp
        tests/hardware_board/blocked_read_test.cpp tests/hardware_board/fault_test.cpp
        tests/hardware_board/recovery_test.cpp)
    target_link_libraries(vna_core_hardware_board_tests PRIVATE vna_core Threads::Threads)
    add_dependencies(vna_core_hardware_board_tests vna_board_hardware_probe)
    add_test(NAME core_hardware_board_lifecycle COMMAND vna_core_hardware_board_tests
        $<TARGET_FILE:vna_board_hardware_probe> "${CMAKE_CURRENT_BINARY_DIR}/hardware-board-trace")
    set_tests_properties(core_hardware_board_lifecycle PROPERTIES TIMEOUT 60)
    add_library(vna_core_test_support STATIC
        tests/support/controlled_device.cpp
        tests/support/fixture.cpp
        tests/support/concurrency_assertions.cpp
        tests/support/concurrent_stress_scenario.cpp
        tests/support/acquisition_core_test_peer.cpp
    )
    target_include_directories(vna_core_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(vna_core_test_support PUBLIC vna_core Threads::Threads)
    add_executable(vna_core_concurrency_tests tests/core_concurrency_test.cpp)
    target_include_directories(vna_core_concurrency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(vna_core_concurrency_tests PRIVATE vna_core_test_support)
    add_test(NAME core_concurrency COMMAND vna_core_concurrency_tests)
    set_tests_properties(core_concurrency PROPERTIES TIMEOUT 20)

    add_executable(vna_core_identity_boundary_tests tests/identity_boundary_test.cpp)
    target_link_libraries(vna_core_identity_boundary_tests PRIVATE vna_core_test_support)
    add_test(NAME core_identity_boundaries COMMAND vna_core_identity_boundary_tests)
    set_tests_properties(core_identity_boundaries PROPERTIES TIMEOUT 20)

    add_executable(vna_core_native_tests tests/native_integration_test.cpp)
    target_link_libraries(vna_core_native_tests PRIVATE vna_core)
    if(TARGET tdc1vn08_drv_sim)
        add_dependencies(vna_core_native_tests tdc1vn08_drv_sim)
        add_test(NAME core_native_integration
            COMMAND vna_core_native_tests $<TARGET_FILE:tdc1vn08_drv_sim>)
        set_tests_properties(core_native_integration PROPERTIES TIMEOUT 20)
    endif()
    if(TARGET tdc1vn10_drv_sim AND TARGET tdc1vn08_drv_sim)
        add_executable(vna_core_board_tests tests/board_integration_test.cpp)
        target_include_directories(vna_core_board_tests PRIVATE
            ${CMAKE_CURRENT_SOURCE_DIR}/src
            ${CMAKE_CURRENT_SOURCE_DIR}/../VnaBoardSimulator/include)
        target_link_libraries(vna_core_board_tests PRIVATE vna_core)
        add_dependencies(vna_core_board_tests tdc1vn10_drv_sim tdc1vn08_drv_sim)
        add_test(NAME core_board_integration
            COMMAND vna_core_board_tests $<TARGET_FILE:tdc1vn10_drv_sim> $<TARGET_FILE:tdc1vn08_drv_sim>)
        set_tests_properties(core_board_integration PROPERTIES TIMEOUT 20)
    endif()
endif()
