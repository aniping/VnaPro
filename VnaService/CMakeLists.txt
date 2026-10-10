add_library(vna_service STATIC
    src/instrument_service.cpp
    src/measurement.cpp
)
target_include_directories(vna_service PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vna_service PUBLIC vna_core)
target_compile_features(vna_service PUBLIC cxx_std_14)
target_compile_options(vna_service PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_library(vna_service_test_support STATIC tests/controlled_device.cpp tests/fixture.cpp)
    target_include_directories(vna_service_test_support PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/tests)
    target_link_libraries(vna_service_test_support PUBLIC vna_service)
endif()
