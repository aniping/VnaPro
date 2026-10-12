add_library(vna_network STATIC
    network_runtime.cpp
    socket_owner.cpp
    socket_platform.cpp
    socket_poll_set.cpp
)
target_include_directories(vna_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(vna_network PUBLIC cxx_std_14)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_network PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(vna_network PUBLIC ws2_32)
endif()

if(BUILD_TESTING)
    add_executable(vna_socket_poll_set_tests tests/socket_poll_set_test.cpp)
    target_link_libraries(vna_socket_poll_set_tests PRIVATE vna_network)
    add_test(NAME socket_poll_set COMMAND vna_socket_poll_set_tests)
    set_tests_properties(socket_poll_set PROPERTIES TIMEOUT 10)
endif()
