add_library(vna_scpi STATIC ScpiEngine.cpp ScpiParser.cpp)
target_include_directories(vna_scpi PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/..)
target_link_libraries(vna_scpi PUBLIC vna_service)
target_compile_features(vna_scpi PUBLIC cxx_std_14)
target_compile_options(vna_scpi PRIVATE -Wall -Wextra -Wpedantic)

if(BUILD_TESTING)
    add_executable(vna_scpi_tests tests/ScpiTestMain.cpp)
    target_link_libraries(vna_scpi_tests PRIVATE vna_scpi)
    target_include_directories(vna_scpi_tests PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../VnaService/tests
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../VnaCore/src
        ${CMAKE_CURRENT_SOURCE_DIR}/../../../VnaCore/tests)
    target_compile_options(vna_scpi_tests PRIVATE -Wall -Wextra -Wpedantic)
    add_test(NAME service_scpi COMMAND vna_scpi_tests $<TARGET_FILE:vnb_drv_sim>)
    set_tests_properties(service_scpi PROPERTIES TIMEOUT 20)
endif()
