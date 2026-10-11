add_library(vna_web STATIC
    web_endpoint.cpp
    static_file.cpp
    http_api.cpp
    frame_selection.cpp
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
