add_library(vna_scpi STATIC scpi_engine.cpp scpi_parser.cpp scpi_display_adapter.cpp scpi_query_policy.cpp)
target_include_directories(vna_scpi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(vna_scpi PUBLIC vna_service)
target_compile_features(vna_scpi PUBLIC cxx_std_14)
target_compile_options(vna_scpi PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(vna_scpi_parser_tests tests/scpi_parser_test.cpp)
    target_link_libraries(vna_scpi_parser_tests PRIVATE vna_scpi)
    target_compile_options(vna_scpi_parser_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME service_scpi_parser COMMAND vna_scpi_parser_tests)

    add_executable(vna_scpi_display_tests tests/shared_display_scpi_test.cpp tests/native_fixture.cpp)
    target_link_libraries(vna_scpi_display_tests PRIVATE vna_scpi)
    target_compile_options(vna_scpi_display_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME service_scpi_display COMMAND vna_scpi_display_tests $<TARGET_FILE:tdc1vn08_drv_sim>)
    set_tests_properties(service_scpi_display PROPERTIES TIMEOUT 20)

    add_executable(vna_scpi_window_tests tests/window_scpi_test.cpp tests/native_fixture.cpp)
    target_link_libraries(vna_scpi_window_tests PRIVATE vna_scpi)
    target_compile_options(vna_scpi_window_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME service_scpi_windows COMMAND vna_scpi_window_tests $<TARGET_FILE:tdc1vn08_drv_sim>)
    set_tests_properties(service_scpi_windows PROPERTIES TIMEOUT 20)

    add_executable(vna_scpi_observer_tests tests/scpi_observer_test.cpp)
    target_link_libraries(vna_scpi_observer_tests PRIVATE vna_scpi vna_service_test_support)
    add_test(NAME service_scpi_observer COMMAND vna_scpi_observer_tests)
    set_tests_properties(service_scpi_observer PROPERTIES TIMEOUT 15)

    add_executable(vna_scpi_tests tests/scpi_test_main.cpp tests/native_fixture.cpp)
    target_link_libraries(vna_scpi_tests PRIVATE vna_scpi vna_service_test_support)
    target_include_directories(vna_scpi_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../VnaService/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../VnaCore/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../VnaCore/tests)
    target_compile_options(vna_scpi_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME service_scpi COMMAND vna_scpi_tests $<TARGET_FILE:tdc1vn08_drv_sim>)
    set_tests_properties(service_scpi PROPERTIES TIMEOUT 20)
endif()
