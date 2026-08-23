add_executable(VnaDriverC99Contract DriverAbiCCompileTest.c)
target_link_libraries(VnaDriverC99Contract PRIVATE VnaDriverSdk)
add_test(NAME VnaDriverC99Contract COMMAND VnaDriverC99Contract)

add_executable(
    VnaHardwareTests
    DriverAbiLayoutTest.cpp
    DriverPluginSecurityTest.cpp
    VnaCallbackLifetimeTest.cpp
    VnaDriverIntegrationTest.cpp
    VnaIntegrationTestSupport.cpp
    VnaLoggingTest.cpp
    VnaValidationTest.cpp
)
configure_file(vnapro-test.conf ${CMAKE_BINARY_DIR}/drivers/vnapro.conf COPYONLY)
target_link_libraries(VnaHardwareTests PRIVATE VnaHardware GTest::gtest_main)
target_include_directories(
    VnaHardwareTests PRIVATE ${PROJECT_SOURCE_DIR}/modules/VnaHardware/src
)
target_compile_definitions(
    VnaHardwareTests PRIVATE
    VNA_TEST_DRIVER_DIRECTORY="$<TARGET_FILE_DIR:VnaDriverSimulator>"
    VNA_TEST_LOG_CONFIGURATION="${CMAKE_BINARY_DIR}/drivers/vnapro.conf"
)
add_dependencies(VnaHardwareTests VnaDriverSimulator)
vna_enable_warnings(VnaHardwareTests)

include(GoogleTest)
gtest_discover_tests(VnaHardwareTests DISCOVERY_TIMEOUT 30)
