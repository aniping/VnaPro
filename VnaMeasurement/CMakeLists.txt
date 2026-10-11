add_library(vna_measurement STATIC
    src/measurement.cpp
    src/measurement_processor.cpp
)
target_include_directories(vna_measurement PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vna_measurement PUBLIC vna_core)
target_compile_features(vna_measurement PUBLIC cxx_std_14)
set_target_properties(vna_measurement PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_measurement PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(vna_measurement_tests tests/measurement_processor_test.cpp)
    target_link_libraries(vna_measurement_tests PRIVATE vna_measurement)
    add_test(NAME measurement_processing COMMAND vna_measurement_tests)
    set_tests_properties(measurement_processing PROPERTIES TIMEOUT 10)
endif()
