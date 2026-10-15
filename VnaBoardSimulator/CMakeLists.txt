cmake_minimum_required(VERSION 3.13)

project(VnaBoardSimulator VERSION 1.0.0 LANGUAGES CXX)
include(CTest)
find_package(Threads REQUIRED)

add_library(tdc1vn10_drv_sim_core OBJECT
    src/board_sim/board_signal_model.cpp
    src/board_sim/board_simulator_state.cpp
    src/board_sim/board_sweep_validation.cpp
)
target_compile_features(tdc1vn10_drv_sim_core PUBLIC cxx_std_14)
target_include_directories(tdc1vn10_drv_sim_core PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/src/board_sim
)
target_link_libraries(tdc1vn10_drv_sim_core PUBLIC Threads::Threads)
set_target_properties(tdc1vn10_drv_sim_core PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)

add_library(tdc1vn10_drv_sim SHARED
    $<TARGET_OBJECTS:tdc1vn10_drv_sim_core>
    src/board_sim/board_function_exports.cpp
)
target_compile_features(tdc1vn10_drv_sim PUBLIC cxx_std_14)
target_include_directories(tdc1vn10_drv_sim PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_include_directories(tdc1vn10_drv_sim PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src/board_sim)
target_compile_definitions(tdc1vn10_drv_sim PRIVATE VNA_BOARD_BUILD_SHARED)
target_link_libraries(tdc1vn10_drv_sim PRIVATE Threads::Threads)
set_target_properties(tdc1vn10_drv_sim PROPERTIES
    OUTPUT_NAME tdc1vn10_drv_sim
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    WINDOWS_EXPORT_ALL_SYMBOLS OFF
)

if(MSVC)
    target_compile_options(tdc1vn10_drv_sim_core PRIVATE /W4)
    target_compile_options(tdc1vn10_drv_sim PRIVATE /W4)
else()
    target_compile_options(tdc1vn10_drv_sim_core PRIVATE -Wall -Wextra -Wpedantic)
    target_compile_options(tdc1vn10_drv_sim PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(UNIX AND NOT APPLE)
    target_link_options(tdc1vn10_drv_sim PRIVATE
        "-Wl,--version-script=${CMAKE_CURRENT_SOURCE_DIR}/cmake/tdc1vn10_drv_sim.map"
    )
    set_property(TARGET tdc1vn10_drv_sim APPEND PROPERTY LINK_DEPENDS
        ${CMAKE_CURRENT_SOURCE_DIR}/cmake/tdc1vn10_drv_sim.map
    )
endif()

if(BUILD_TESTING)
    add_executable(tdc1vn10_drv_provider_contract_tests tests/provider_contract_test.cpp)
    target_compile_features(tdc1vn10_drv_provider_contract_tests PRIVATE cxx_std_14)
    target_include_directories(tdc1vn10_drv_provider_contract_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
    if(MSVC)
        target_compile_options(tdc1vn10_drv_provider_contract_tests PRIVATE /W4)
    else()
        target_compile_options(tdc1vn10_drv_provider_contract_tests PRIVATE -Wall -Wextra -Wpedantic)
    endif()
    add_test(NAME board_provider_contract COMMAND tdc1vn10_drv_provider_contract_tests)
    set_tests_properties(board_provider_contract PROPERTIES TIMEOUT 10)

    add_executable(tdc1vn10_drv_sim_unit_tests
        $<TARGET_OBJECTS:tdc1vn10_drv_sim_core>
        tests/batch_gate.cpp
        tests/batch_gate_release.cpp
        tests/test_assertions.cpp
        tests/test_support.cpp
        tests/board_state_test.cpp
        tests/noise_amplitude_test.cpp
        tests/board_async_test.cpp
        tests/unit_test_main.cpp
    )
    target_compile_features(tdc1vn10_drv_sim_unit_tests PRIVATE cxx_std_14)
    target_include_directories(tdc1vn10_drv_sim_unit_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/board_sim
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(tdc1vn10_drv_sim_unit_tests PRIVATE Threads::Threads)

    add_executable(tdc1vn10_drv_sim_contract_tests
        tests/board_library_probe.cpp
        tests/board_contract_test.cpp
        tests/test_assertions.cpp
    )
    target_compile_features(tdc1vn10_drv_sim_contract_tests PRIVATE cxx_std_14)
    target_include_directories(tdc1vn10_drv_sim_contract_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/include
        ${CMAKE_CURRENT_SOURCE_DIR}/src/board_sim
        ${CMAKE_CURRENT_SOURCE_DIR}/tests
    )
    target_link_libraries(tdc1vn10_drv_sim_contract_tests PRIVATE ${CMAKE_DL_LIBS} Threads::Threads)

    add_test(NAME board_unit COMMAND tdc1vn10_drv_sim_unit_tests)
    add_test(NAME board_contract COMMAND tdc1vn10_drv_sim_contract_tests $<TARGET_FILE:tdc1vn10_drv_sim>)
    add_test(NAME board_export_symbols COMMAND ${CMAKE_COMMAND}
        -DLIBRARY_FILE=$<TARGET_FILE:tdc1vn10_drv_sim>
        -DNM_TOOL=${CMAKE_NM}
        -DOBJDUMP_TOOL=${CMAKE_OBJDUMP}
        -DHOST_SYSTEM=${CMAKE_SYSTEM_NAME}
        -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/CheckBoardExports.cmake
    )
    set_tests_properties(board_unit board_contract PROPERTIES TIMEOUT 20)
endif()
