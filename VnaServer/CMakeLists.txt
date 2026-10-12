include("${CMAKE_SOURCE_DIR}/cmake/yaml_cpp.cmake")
add_subdirectory(src/network)
add_subdirectory(src/scpi)
add_subdirectory(src/web)
set(SERVER_CONFIG_SOURCES src/server_config_loader.cpp src/server_config_io.cpp src/yaml_config_reader.cpp)
add_executable(vna_server src/main.cpp ${SERVER_CONFIG_SOURCES} src/utf8_argument.cpp
    src/scpi_tcp_server.cpp src/scpi_connection.cpp)
target_link_libraries(vna_server PRIVATE vna_scpi vna_web vna_log yaml-cpp::yaml-cpp)
set(DEFAULT_VN08_DRIVER_PATH "$<TARGET_FILE:vnb_drv_sim>")
set(DEFAULT_VN10_DRIVER_PATH "$<TARGET_FILE:tdc1vn10_drv_sim>")
set(DEFAULT_WEB_ROOT_PATH "${CMAKE_SOURCE_DIR}/WebUI/dist")
set(DEFAULT_CONFIG_PATH "${CMAKE_RUNTIME_OUTPUT_DIRECTORY}/vnapro.yml")
configure_file("${CMAKE_SOURCE_DIR}/config/vnapro.yml" "${DEFAULT_CONFIG_PATH}" COPYONLY)
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
add_dependencies(vna_server vnb_drv_sim tdc1vn10_drv_sim)
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
    add_test(NAME sparameter_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/SParameterIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim>
            ${CMAKE_SOURCE_DIR}/VnaNativeSimulator/tests/golden_iq_vectors.h)
    set_tests_properties(sparameter_integration PROPERTIES TIMEOUT 45)
    add_test(NAME noise_iq_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/SimulatorNoiseIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(noise_iq_integration PROPERTIES TIMEOUT 60)
    add_executable(vna_server_config_tests tests/server_config_loader_test.cpp ${SERVER_CONFIG_SOURCES})
    target_include_directories(vna_server_config_tests PRIVATE src)
    target_link_libraries(vna_server_config_tests PRIVATE vna_service vna_log yaml-cpp::yaml-cpp)
    add_test(NAME server_config
        COMMAND vna_server_config_tests "${CMAKE_CURRENT_BINARY_DIR}/server-config-test")
    add_test(NAME logging_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/LoggingIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(logging_integration PROPERTIES TIMEOUT 45)
    add_test(NAME instrument_configuration_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/InstrumentConfigurationIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(instrument_configuration_integration PROPERTIES TIMEOUT 45)
    add_test(NAME board_driver_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/BoardDriverIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn10_drv_sim> $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(board_driver_integration PROPERTIES TIMEOUT 60)
endif()
