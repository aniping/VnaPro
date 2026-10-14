add_library(vna_web STATIC
    web_endpoint.cpp
    static_file.cpp
    http_api.cpp
    display_json.cpp
    trace_action_adapter.cpp
    frame_selection.cpp
    frame_array_json.cpp
    json.cpp
    json_parser.cpp
    json_encoder.cpp
)
target_include_directories(vna_web PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(vna_web PUBLIC vna_service vna_network)
target_link_libraries(vna_web PRIVATE vna_log)
target_compile_features(vna_web PUBLIC cxx_std_14)
target_compile_options(vna_web PRIVATE -Wall -Wextra -Wpedantic)
if(WIN32)
    target_link_libraries(vna_web PRIVATE ws2_32)
endif()

if(BUILD_TESTING)
    add_executable(vna_frame_array_json_tests tests/frame_array_json_test.cpp)
    target_link_libraries(vna_frame_array_json_tests PRIVATE vna_web)
    add_test(NAME web_frame_array_json COMMAND vna_frame_array_json_tests)
    set_tests_properties(web_frame_array_json PROPERTIES TIMEOUT 10)
    add_executable(vna_web_all_sparameters_tests tests/all_sparameters_test.cpp)
    target_link_libraries(vna_web_all_sparameters_tests PRIVATE vna_web vna_service_test_support)
    add_test(NAME web_all_sparameters COMMAND vna_web_all_sparameters_tests)
    set_tests_properties(web_all_sparameters PROPERTIES TIMEOUT 20)
    add_executable(vna_web_diagram_actions_tests tests/diagram_actions_test.cpp)
    target_link_libraries(vna_web_diagram_actions_tests PRIVATE vna_web vna_scpi vna_service_test_support)
    add_test(NAME web_diagram_actions COMMAND vna_web_diagram_actions_tests)
    set_tests_properties(web_diagram_actions PROPERTIES TIMEOUT 20)
    add_executable(vna_web_point_projection_tests tests/point_projection_test.cpp)
    target_link_libraries(vna_web_point_projection_tests PRIVATE vna_web)
    add_test(NAME web_point_projection COMMAND vna_web_point_projection_tests)
    set_tests_properties(web_point_projection PROPERTIES TIMEOUT 10)
    add_executable(vna_web_display_control_tests tests/display_control_test.cpp)
    target_link_libraries(vna_web_display_control_tests PRIVATE vna_web vna_scpi vna_service_test_support)
    add_test(NAME web_display_control COMMAND vna_web_display_control_tests)
    set_tests_properties(web_display_control PROPERTIES TIMEOUT 20)
    add_executable(vna_web_shared_display_tests tests/shared_display_test.cpp)
    target_link_libraries(vna_web_shared_display_tests PRIVATE vna_web vna_scpi vna_service_test_support)
    add_test(NAME web_shared_display COMMAND vna_web_shared_display_tests)
    set_tests_properties(web_shared_display PROPERTIES TIMEOUT 20)
    add_executable(vna_web_measurement_update_tests tests/measurement_update_test.cpp)
    target_link_libraries(vna_web_measurement_update_tests PRIVATE vna_web vna_service_test_support)
    add_test(NAME web_measurement_update COMMAND vna_web_measurement_update_tests)
    set_tests_properties(web_measurement_update PROPERTIES TIMEOUT 20)
    add_executable(vna_frame_selection_tests tests/frame_selection_test.cpp)
    target_link_libraries(vna_frame_selection_tests PRIVATE vna_web vna_service_test_support)
    add_test(NAME web_frame_selection COMMAND vna_frame_selection_tests)
    add_executable(vna_web_tests tests/web_api_test.cpp)
    target_link_libraries(vna_web_tests PRIVATE vna_web vna_service_test_support)
    target_include_directories(vna_web_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../VnaService/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../VnaCore/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../VnaCore/tests)
    target_compile_options(vna_web_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME web_api COMMAND vna_web_tests)
    set_tests_properties(web_api PROPERTIES TIMEOUT 20)
endif()
