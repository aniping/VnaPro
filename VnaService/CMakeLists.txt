add_library(vna_service STATIC
    src/instrument_service.cpp
)
target_include_directories(vna_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vna_service PUBLIC vna_core vna_measurement)
target_link_libraries(vna_service PRIVATE vna_log)
target_compile_features(vna_service PUBLIC cxx_std_14)
target_compile_options(vna_service PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_library(vna_service_test_support STATIC tests/controlled_device.cpp tests/fixture.cpp)
    target_include_directories(vna_service_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(vna_service_test_support PUBLIC vna_service)
    add_executable(vna_service_measurement_tests tests/measurement_service_test.cpp)
    target_link_libraries(vna_service_measurement_tests PRIVATE vna_service_test_support)
    add_test(NAME service_measurement COMMAND vna_service_measurement_tests)
    set_tests_properties(service_measurement PROPERTIES TIMEOUT 15)
endif()
