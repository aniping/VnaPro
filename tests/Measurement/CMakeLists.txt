add_executable(
    VnaMeasurementTests
    MeasurementBinderTest.cpp
    MeasurementCapacityTest.cpp
    MeasurementCompilerTest.cpp
    MeasurementTestSupport.cpp
    ObservationBuilderTest.cpp
)
target_link_libraries(VnaMeasurementTests PRIVATE VnaMeasurement GTest::gtest_main)
target_include_directories(
    VnaMeasurementTests PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/VnaAcquisition/src
    ${PROJECT_SOURCE_DIR}/modules/VnaMeasurement/src
)
vna_enable_warnings(VnaMeasurementTests)

include(GoogleTest)
gtest_discover_tests(VnaMeasurementTests DISCOVERY_TIMEOUT 30)
