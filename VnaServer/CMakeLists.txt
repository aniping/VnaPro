add_subdirectory(src/network)
add_subdirectory(src/scpi)
add_subdirectory(src/web)
add_executable(vna_server src/main.cpp src/log_config_loader.cpp src/utf8_argument.cpp
    src/scpi_tcp_server.cpp src/scpi_connection.cpp)
target_link_libraries(vna_server PRIVATE vna_scpi vna_web vna_log)
set(DEFAULT_DRIVER_PATH "$<TARGET_FILE:vnb_drv_sim>")
set(DEFAULT_WEB_ROOT_PATH "${CMAKE_SOURCE_DIR}/WebUI/dist")
set(DEFAULT_LOG_CONFIG_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/logging.properties")
configure_file("${CMAKE_SOURCE_DIR}/config/logging.properties" "${DEFAULT_LOG_CONFIG_PATH}" COPYONLY)
configure_file(src/server_defaults.h.in generated/server_defaults.h.in @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/server_defaults.h"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/server_defaults.h.in")
target_include_directories(vna_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
if(WIN32)
    target_link_libraries(vna_server PRIVATE ws2_32 shell32)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_dependencies(vna_server vnb_drv_sim)
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME scpi_tcp_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ScpiTcpIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(scpi_tcp_integration PROPERTIES TIMEOUT 45)
    add_executable(vna_network_shutdown_tests tests/network_shutdown_test.cpp tests/client.cpp
        src/scpi_tcp_server.cpp src/scpi_connection.cpp)
    target_include_directories(vna_network_shutdown_tests PRIVATE src)
    target_link_libraries(vna_network_shutdown_tests PRIVATE vna_scpi vna_web vna_log)
    if(WIN32)
        target_link_libraries(vna_network_shutdown_tests PRIVATE ws2_32)
    endif()
    add_test(NAME scpi_network_shutdown
        COMMAND vna_network_shutdown_tests $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(scpi_network_shutdown PROPERTIES TIMEOUT 15)
    add_test(NAME web_http_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/WebHttpIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim> ${CMAKE_SOURCE_DIR}/WebUI/dist)
    set_tests_properties(web_http_integration PROPERTIES TIMEOUT 60)
    add_test(NAME golden_iq_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/GoldenIqIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim>
            ${CMAKE_SOURCE_DIR}/VnaNativeSimulator/tests/golden_iq_vectors.h)
    set_tests_properties(golden_iq_integration PROPERTIES TIMEOUT 60)
    add_test(NAME noise_iq_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/SimulatorNoiseIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(noise_iq_integration PROPERTIES TIMEOUT 60)
    add_executable(vna_log_config_tests tests/log_config_loader_test.cpp src/log_config_loader.cpp)
    target_include_directories(vna_log_config_tests PRIVATE src)
    target_link_libraries(vna_log_config_tests PRIVATE vna_log)
    add_test(NAME log_config
        COMMAND vna_log_config_tests "${CMAKE_CURRENT_BINARY_DIR}/log-config-test")
    add_test(NAME logging_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/LoggingIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(logging_integration PROPERTIES TIMEOUT 45)
endif()
