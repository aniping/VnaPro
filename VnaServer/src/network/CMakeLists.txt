add_library(vna_network STATIC
    NetworkRuntime.cpp
    SocketOwner.cpp
    SocketPlatform.cpp
)
target_include_directories(vna_network PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_compile_features(vna_network PUBLIC cxx_std_14)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_network PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(vna_network PUBLIC ws2_32)
endif()
