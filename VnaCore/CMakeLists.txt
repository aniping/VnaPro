cmake_minimum_required(VERSION 3.13)

if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(VnaCore LANGUAGES CXX)
    include(CTest)
endif()

find_package(Threads REQUIRED)
add_library(vna_core STATIC
    src/acquisition_core.cpp
    src/core/acquisition_core_impl.cpp
    src/device/native/native_library.cpp
    src/device/native/native_device_session.cpp
)
target_compile_features(vna_core PUBLIC cxx_std_14)
set_target_properties(vna_core PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(vna_core
    PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../VnaNativeSimulator/include
)
target_link_libraries(vna_core PRIVATE Threads::Threads ${CMAKE_DL_LIBS})
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_core PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(vna_core_concurrency_tests tests/core_concurrency_test.cpp)
    target_include_directories(vna_core_concurrency_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(vna_core_concurrency_tests PRIVATE vna_core Threads::Threads)
    add_test(NAME core_concurrency COMMAND vna_core_concurrency_tests)
    set_tests_properties(core_concurrency PROPERTIES TIMEOUT 20)

    add_executable(vna_core_native_tests tests/native_integration_test.cpp)
    target_link_libraries(vna_core_native_tests PRIVATE vna_core)
    if(TARGET vnb_drv_sim)
        add_dependencies(vna_core_native_tests vnb_drv_sim)
        add_test(NAME core_native_integration
            COMMAND vna_core_native_tests $<TARGET_FILE:vnb_drv_sim>)
        set_tests_properties(core_native_integration PROPERTIES TIMEOUT 20)
    endif()
endif()
