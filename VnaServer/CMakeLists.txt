add_subdirectory(src/network)
add_subdirectory(src/scpi)
add_subdirectory(src/web)
add_executable(vna_server src/main.cpp src/scpi_tcp_server.cpp)
target_link_libraries(vna_server PRIVATE vna_scpi vna_web)
target_compile_definitions(vna_server PRIVATE
    VNA_DEFAULT_DRIVER="$<TARGET_FILE:vnb_drv_sim>"
    VNA_DEFAULT_WEB_ROOT="${CMAKE_SOURCE_DIR}/frontend/dist")
if(WIN32)
    target_link_libraries(vna_server PRIVATE ws2_32)
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
    add_executable(vna_network_shutdown_tests tests/network_shutdown_test.cpp src/scpi_tcp_server.cpp)
    target_include_directories(vna_network_shutdown_tests PRIVATE src)
    target_link_libraries(vna_network_shutdown_tests PRIVATE vna_scpi vna_web)
    if(WIN32)
        target_link_libraries(vna_network_shutdown_tests PRIVATE ws2_32)
    endif()
    add_test(NAME scpi_network_shutdown
        COMMAND vna_network_shutdown_tests $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(scpi_network_shutdown PROPERTIES TIMEOUT 15)
    add_test(NAME web_http_integration
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/WebHttpIntegrationTest.py
            $<TARGET_FILE:vna_server> $<TARGET_FILE:vnb_drv_sim> ${CMAKE_SOURCE_DIR}/frontend/dist)
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
endif()
