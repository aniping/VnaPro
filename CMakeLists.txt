cmake_minimum_required(VERSION 3.13)
project(VnaPro VERSION 0.1.0 LANGUAGES CXX)

include(CTest)
set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_subdirectory(VnaNativeSimulator)
add_subdirectory(VnaLog)
add_subdirectory(VnaCore)
add_subdirectory(VnaMeasurement)
add_subdirectory(VnaDisplay)
add_subdirectory(VnaService)
add_subdirectory(VnaServer)

if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter REQUIRED)
    add_test(NAME offline_dependencies
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_SOURCE_DIR}/cmake/tests/OfflineDependencyTest.py
            ${CMAKE_COMMAND} ${CMAKE_SOURCE_DIR})
    set_tests_properties(offline_dependencies PROPERTIES TIMEOUT 120)
endif()
