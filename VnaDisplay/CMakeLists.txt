add_library(vna_display STATIC
    src/display_workspace.cpp
    src/diagram_collection.cpp
    src/display_format.cpp
    src/trace_projection.cpp
    src/trace_sequence_formatter.cpp
    src/scale_calculator.cpp
    src/marker_resolver.cpp
    src/marker_collection.cpp
)
target_include_directories(vna_display PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(vna_display PUBLIC vna_measurement)
target_compile_features(vna_display PUBLIC cxx_std_14)
set_target_properties(vna_display PROPERTIES CXX_EXTENSIONS OFF)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_display PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(BUILD_TESTING)
    add_executable(vna_diagram_collection_tests tests/diagram_collection_test.cpp)
    target_link_libraries(vna_diagram_collection_tests PRIVATE vna_display)
    add_test(NAME diagram_collection COMMAND vna_diagram_collection_tests)
    set_tests_properties(diagram_collection PROPERTIES TIMEOUT 10)
    add_executable(vna_display_workspace_tests tests/display_workspace_test.cpp)
    target_link_libraries(vna_display_workspace_tests PRIVATE vna_display)
    add_test(NAME display_workspace COMMAND vna_display_workspace_tests)
    add_executable(vna_trace_projection_tests tests/trace_projection_test.cpp)
    target_link_libraries(vna_trace_projection_tests PRIVATE vna_display)
    add_test(NAME trace_projection COMMAND vna_trace_projection_tests)
    add_executable(vna_trace_formats_tests tests/trace_formats_test.cpp)
    target_include_directories(vna_trace_formats_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
    target_link_libraries(vna_trace_formats_tests PRIVATE vna_display)
    add_test(NAME trace_formats COMMAND vna_trace_formats_tests)
    add_executable(vna_marker_collection_tests tests/marker_collection_test.cpp)
    target_link_libraries(vna_marker_collection_tests PRIVATE vna_display)
    add_test(NAME marker_collection COMMAND vna_marker_collection_tests)
    set_tests_properties(display_workspace trace_projection trace_formats marker_collection PROPERTIES TIMEOUT 10)
endif()
