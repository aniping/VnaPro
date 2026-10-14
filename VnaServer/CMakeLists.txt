include("${CMAKE_SOURCE_DIR}/cmake/yaml_cpp.cmake")
add_subdirectory(src/network)
add_subdirectory(src/scpi)
add_subdirectory(src/web)
set(SERVER_CONFIG_SOURCES src/server_config_loader.cpp src/server_config_io.cpp src/yaml_config_reader.cpp
    src/server_startup_config_reader.cpp)
add_executable(vna_server src/main.cpp ${SERVER_CONFIG_SOURCES} src/startup_paths.cpp src/simulator_environment.cpp
    src/scpi_tcp_server.cpp src/scpi_connection.cpp)
target_link_libraries(vna_server PRIVATE vna_scpi vna_web vna_log yaml-cpp::yaml-cpp)
set(DEFAULT_VN08_DRIVER_FILENAME "$<TARGET_FILE_NAME:tdc1vn08_drv_sim>")
set(DEFAULT_VN10_DRIVER_FILENAME "$<TARGET_FILE_NAME:tdc1vn10_drv_sim>")
set(DEFAULT_WEB_ROOT_PATH "${CMAKE_SOURCE_DIR}/WebUI/dist")
set(DEFAULT_CONFIG_PATH "$<TARGET_FILE_DIR:vna_server>/vnapro.yml")
# 源模板保留可直接阅读的 MinGW 文件名；生成时替换为当前工具链真实产物名称。
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/config/vnapro.yml")
file(READ "${CMAKE_SOURCE_DIR}/config/vnapro.yml" DEFAULT_CONFIG_CONTENT)
string(REPLACE "libtdc1vn10_drv_sim.dll" "$<TARGET_FILE_NAME:tdc1vn10_drv_sim>"
    DEFAULT_CONFIG_CONTENT "${DEFAULT_CONFIG_CONTENT}")
string(REPLACE "libtdc1vn08_drv_sim.dll" "$<TARGET_FILE_NAME:tdc1vn08_drv_sim>"
    DEFAULT_CONFIG_CONTENT "${DEFAULT_CONFIG_CONTENT}")
file(GENERATE OUTPUT "${DEFAULT_CONFIG_PATH}" CONTENT "${DEFAULT_CONFIG_CONTENT}")
configure_file(src/server_defaults.h.in generated/server_defaults.h.in @ONLY)
file(GENERATE OUTPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>/server_defaults.h"
    INPUT "${CMAKE_CURRENT_BINARY_DIR}/generated/server_defaults.h.in")
target_include_directories(vna_server PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/generated/$<CONFIG>")
if(WIN32)
    target_link_libraries(vna_server PRIVATE ws2_32)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_server PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_dependencies(vna_server tdc1vn08_drv_sim tdc1vn10_drv_sim)
include("${CMAKE_SOURCE_DIR}/cmake/release_bundle.cmake")
if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME calibration_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/CalibrationIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn10_drv_sim>)
    set_tests_properties(calibration_integration PROPERTIES TIMEOUT 90)
    add_library(vna_hardware_environment_probe SHARED tests/hardware_environment_probe.cpp)
    target_compile_definitions(vna_hardware_environment_probe PRIVATE VNA_NATIVE_BUILD_SHARED)
    target_include_directories(vna_hardware_environment_probe PRIVATE
        "${CMAKE_SOURCE_DIR}/VnaNativeSimulator/include")
    add_test(NAME hardware_driver_isolation
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/HardwareDriverIsolationIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vna_hardware_environment_probe>)
    set_tests_properties(hardware_driver_isolation PROPERTIES TIMEOUT 30)
    add_test(NAME hardware_board_acquisition
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/HardwareBoardAcquisitionIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vna_board_hardware_probe>)
    set_tests_properties(hardware_board_acquisition PROPERTIES TIMEOUT 60)
    add_executable(vna_startup_paths_tests tests/startup_paths_test.cpp src/startup_paths.cpp src/web/static_file.cpp)
    target_include_directories(vna_startup_paths_tests PRIVATE src)
    add_test(NAME startup_paths COMMAND vna_startup_paths_tests)
    add_test(NAME startup_configuration_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/StartupConfigurationIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn10_drv_sim>)
    set_tests_properties(startup_configuration_integration PROPERTIES TIMEOUT 30)
    if(VNAPRO_BUNDLE_WEBUI)
        add_test(NAME webui_build_pipeline
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/tests/WebUiBuildTest.py
                ${CMAKE_COMMAND} ${CMAKE_SOURCE_DIR} "${VNAPRO_NODE_EXECUTABLE}" "${VNAPRO_NPM_EXECUTABLE}")
        set_tests_properties(webui_build_pipeline PROPERTIES TIMEOUT 60)
        add_test(NAME release_bundle
            COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/tests/ReleaseBundleTest.py
                "${VNAPRO_RELEASE_OUTPUT}" $<TARGET_FILE_NAME:vna_server>
                $<TARGET_FILE_NAME:tdc1vn10_drv_sim> $<TARGET_FILE_NAME:tdc1vn08_drv_sim>
                "${DEFAULT_CONFIG_PATH}" ${CMAKE_SOURCE_DIR}/WebUI/dist)
        set_tests_properties(release_bundle PROPERTIES TIMEOUT 60)
    endif()
    add_test(NAME scpi_tcp_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/ScpiTcpIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn08_drv_sim>)
    set_tests_properties(scpi_tcp_integration PROPERTIES TIMEOUT 45)
    add_executable(vna_network_shutdown_tests tests/network_shutdown_test.cpp tests/client.cpp
        src/scpi_tcp_server.cpp src/scpi_connection.cpp)
    target_include_directories(vna_network_shutdown_tests PRIVATE src)
    target_link_libraries(vna_network_shutdown_tests PRIVATE vna_scpi vna_web vna_log)
    if(WIN32)
        target_link_libraries(vna_network_shutdown_tests PRIVATE ws2_32)
    endif()
    add_test(NAME scpi_network_shutdown
        COMMAND vna_network_shutdown_tests $<TARGET_FILE:tdc1vn08_drv_sim>)
    set_tests_properties(scpi_network_shutdown PROPERTIES TIMEOUT 15)
    add_test(NAME web_http_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/WebHttpIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn08_drv_sim> ${CMAKE_SOURCE_DIR}/WebUI/dist)
    set_tests_properties(web_http_integration PROPERTIES TIMEOUT 60)
    add_test(NAME web_frame_scheduling
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/WebFrameSchedulingTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn10_drv_sim>)
    set_tests_properties(web_frame_scheduling PROPERTIES TIMEOUT 45)
    add_test(NAME web_frame_resolution
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/WebFrameResolutionTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn10_drv_sim> ${CMAKE_SOURCE_DIR}/WebUI/dist)
    set_tests_properties(web_frame_resolution PROPERTIES TIMEOUT 60)
    add_test(NAME web_frame_diagnostics
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/WebFrameDiagnosticsTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn10_drv_sim>)
    set_tests_properties(web_frame_diagnostics PROPERTIES TIMEOUT 30)
    add_test(NAME golden_iq_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/GoldenIqIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn08_drv_sim>
            ${CMAKE_SOURCE_DIR}/VnaNativeSimulator/tests/golden_iq_vectors.h)
    set_tests_properties(golden_iq_integration PROPERTIES TIMEOUT 60)
    add_test(NAME sparameter_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/SParameterIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn08_drv_sim>
            ${CMAKE_SOURCE_DIR}/VnaNativeSimulator/tests/golden_iq_vectors.h)
    set_tests_properties(sparameter_integration PROPERTIES TIMEOUT 45)
    add_test(NAME four_port_sparameter_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/FourPortSParameterIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn08_drv_sim> $<TARGET_FILE:tdc1vn10_drv_sim>
            ${CMAKE_SOURCE_DIR}/VnaNativeSimulator/tests/golden_iq_vectors.h)
    set_tests_properties(four_port_sparameter_integration PROPERTIES TIMEOUT 75)
    add_test(NAME all_sparameters_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/AllSParametersIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn08_drv_sim> $<TARGET_FILE:tdc1vn10_drv_sim>)
    set_tests_properties(all_sparameters_integration PROPERTIES TIMEOUT 120)
    add_test(NAME noise_iq_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/SimulatorNoiseIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn08_drv_sim>)
    set_tests_properties(noise_iq_integration PROPERTIES TIMEOUT 60)
    add_test(NAME noise_iq_vn10_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/SimulatorNoiseIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn10_drv_sim> tdc1vn10)
    set_tests_properties(noise_iq_vn10_integration PROPERTIES TIMEOUT 60)
    add_executable(vna_server_config_tests tests/server_config_loader_test.cpp ${SERVER_CONFIG_SOURCES})
    target_include_directories(vna_server_config_tests PRIVATE src)
    target_link_libraries(vna_server_config_tests PRIVATE vna_service vna_log yaml-cpp::yaml-cpp)
    add_test(NAME server_config
        COMMAND vna_server_config_tests "${CMAKE_CURRENT_BINARY_DIR}/server-config-test")
    add_test(NAME logging_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/LoggingIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn08_drv_sim>)
    set_tests_properties(logging_integration PROPERTIES TIMEOUT 45)
    add_test(NAME instrument_configuration_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/InstrumentConfigurationIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn08_drv_sim>)
    set_tests_properties(instrument_configuration_integration PROPERTIES TIMEOUT 45)
    add_test(NAME board_driver_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/BoardDriverIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:tdc1vn10_drv_sim> $<TARGET_FILE:tdc1vn08_drv_sim>)
    set_tests_properties(board_driver_integration PROPERTIES TIMEOUT 60)
endif()
