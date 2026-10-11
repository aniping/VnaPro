include("${CMAKE_CURRENT_SOURCE_DIR}/../cmake/spdlog.cmake")

add_library(vna_log STATIC
    src/logger.cpp
    src/log_config.cpp
    src/bounded_pattern_formatter.cpp
    src/uppercase_level_flag.cpp
)
target_compile_features(vna_log PUBLIC cxx_std_14)
set_target_properties(vna_log PROPERTIES CXX_EXTENSIONS OFF)
target_include_directories(vna_log
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include"
    PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(vna_log PUBLIC spdlog::spdlog)
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(vna_log PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(MSVC)
    target_compile_options(vna_log PUBLIC /utf-8)
endif()

if(BUILD_TESTING)
    add_executable(vna_log_tests tests/logger_test.cpp)
    target_link_libraries(vna_log_tests PRIVATE vna_log)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/logger-test")
    add_test(NAME logging_backend
        COMMAND vna_log_tests "${CMAKE_CURRENT_BINARY_DIR}/logger-test")
    set_tests_properties(logging_backend PROPERTIES TIMEOUT 20)
endif()
