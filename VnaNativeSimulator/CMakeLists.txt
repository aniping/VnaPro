cmake_minimum_required(VERSION 3.13)

project(VnaNativeSimulator VERSION 1.0.0 LANGUAGES CXX)

include(CTest)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(VNA_SIM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(VNA_SIM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)
option(VNA_SIM_ENABLE_TSAN "Enable ThreadSanitizer" OFF)

set(_vna_sanitizer_count 0)
foreach(_vna_option VNA_SIM_ENABLE_ASAN VNA_SIM_ENABLE_UBSAN VNA_SIM_ENABLE_TSAN)
    if(${_vna_option})
        math(EXPR _vna_sanitizer_count "${_vna_sanitizer_count} + 1")
    endif()
endforeach()

if(_vna_sanitizer_count GREATER 1)
    message(FATAL_ERROR "ASan, UBSan and TSan must use separate build directories")
endif()

function(vna_apply_compile_settings target_name)
    target_compile_options(${target_name} PRIVATE
        -Wall
        -Wextra
        -Wpedantic
    )

    if(VNA_SIM_ENABLE_ASAN)
        target_compile_options(${target_name} PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    elseif(VNA_SIM_ENABLE_UBSAN)
        target_compile_options(${target_name} PRIVATE -fsanitize=undefined -fno-omit-frame-pointer)
    elseif(VNA_SIM_ENABLE_TSAN)
        target_compile_options(${target_name} PRIVATE -fsanitize=thread -fno-omit-frame-pointer)
    endif()
endfunction()

function(vna_apply_link_settings target_name)
    if(VNA_SIM_ENABLE_ASAN)
        target_link_options(${target_name} PRIVATE -fsanitize=address)
    elseif(VNA_SIM_ENABLE_UBSAN)
        target_link_options(${target_name} PRIVATE -fsanitize=undefined)
    elseif(VNA_SIM_ENABLE_TSAN)
        target_link_options(${target_name} PRIVATE -fsanitize=thread)
    endif()
endfunction()

set(VNA_SIM_CORE_SOURCES
    src/native_sim/NativeSimulatorState.cpp
    src/native_sim/NormalFrameDecoder.cpp
    src/native_sim/SignalModel.cpp
)

add_library(vnb_drv_sim_core OBJECT ${VNA_SIM_CORE_SOURCES})
set_target_properties(vnb_drv_sim_core PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    POSITION_INDEPENDENT_CODE ON
    VISIBILITY_INLINES_HIDDEN YES
)
target_include_directories(vnb_drv_sim_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/native_sim
)
target_link_libraries(vnb_drv_sim_core PUBLIC Threads::Threads)
vna_apply_compile_settings(vnb_drv_sim_core)

add_library(vnb_drv_sim SHARED
    $<TARGET_OBJECTS:vnb_drv_sim_core>
    src/native_sim/NativeFunctionExports.cpp
)
set_target_properties(vnb_drv_sim PROPERTIES
    CXX_VISIBILITY_PRESET hidden
    OUTPUT_NAME vnb_drv_sim
    VISIBILITY_INLINES_HIDDEN YES
)
target_include_directories(vnb_drv_sim PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/native_sim
)
target_link_libraries(vnb_drv_sim PRIVATE Threads::Threads)
vna_apply_compile_settings(vnb_drv_sim)
vna_apply_link_settings(vnb_drv_sim)

if(UNIX AND NOT APPLE)
    target_link_options(vnb_drv_sim PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/vnb_drv_sim.map"
    )
    set_property(TARGET vnb_drv_sim APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/vnb_drv_sim.map
    )
endif()

if(BUILD_TESTING)
    add_executable(vnb_drv_sim_unit_tests
        $<TARGET_OBJECTS:vnb_drv_sim_core>
        tests/native_unit/UnitTestMain.cpp
        tests/native_unit/NativeApiSignatureTest.cpp
        tests/native_unit/NativeStateTest.cpp
        tests/native_unit/NormalFrameDecoderTest.cpp
        tests/native_unit/SignalModelTest.cpp
    )
    target_include_directories(vnb_drv_sim_unit_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/native_sim
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(vnb_drv_sim_unit_tests PRIVATE Threads::Threads)
    vna_apply_compile_settings(vnb_drv_sim_unit_tests)
    vna_apply_link_settings(vnb_drv_sim_unit_tests)

    add_executable(vnb_drv_sim_contract_tests
        tests/native_contract/NativeLibraryProbe.cpp
        tests/native_contract/NativeAbiContractTest.cpp
        tests/native_contract/NativeNormalSweepTest.cpp
    )
    target_include_directories(vnb_drv_sim_contract_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/native_contract
    )
    target_link_libraries(vnb_drv_sim_contract_tests PRIVATE
        ${CMAKE_DL_LIBS}
        Threads::Threads
    )
    vna_apply_compile_settings(vnb_drv_sim_contract_tests)
    vna_apply_link_settings(vnb_drv_sim_contract_tests)

    add_test(NAME native_unit COMMAND vnb_drv_sim_unit_tests)
    add_test(
        NAME native_contract
        COMMAND vnb_drv_sim_contract_tests $<TARGET_FILE:vnb_drv_sim>
    )
    add_test(
        NAME native_export_symbols
        COMMAND ${CMAKE_COMMAND}
            -DLIBRARY_FILE=$<TARGET_FILE:vnb_drv_sim>
            -DNM_TOOL=${CMAKE_NM}
            -DHOST_SYSTEM=${CMAKE_SYSTEM_NAME}
            -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckNativeExports.cmake
    )
endif()
