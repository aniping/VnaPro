add_library(vna_display STATIC
    src/display_workspace.cpp
    src/trace_projection.cpp
    src/scale_calculator.cpp
    src/marker_resolver.cpp
)
target_include_directories(vna_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vna_display PUBLIC vna_measurement)
target_compile_features(vna_display PUBLIC cxx_std_14)
set_target_properties(vna_display PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_display PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(vna_display_workspace_tests tests/display_workspace_test.cpp)
    target_link_libraries(vna_display_workspace_tests PRIVATE vna_display)
    add_test(NAME display_workspace COMMAND vna_display_workspace_tests)
    add_executable(vna_trace_projection_tests tests/trace_projection_test.cpp)
    target_link_libraries(vna_trace_projection_tests PRIVATE vna_display)
    add_test(NAME trace_projection COMMAND vna_trace_projection_tests)
    set_tests_properties(display_workspace trace_projection PROPERTIES TIMEOUT 10)
endif()
