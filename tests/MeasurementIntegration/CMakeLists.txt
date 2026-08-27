add_executable(
    VnaMeasurementIntegrationTests
    MeasurementPipelineTest.cpp
    ../Acquisition/FakeVnaHardware.cpp
    ../Acquisition/FakeVnaHardwareCommands.cpp
    ../Acquisition/RecordingAcquisitionSink.cpp
    ../Measurement/MeasurementTestSupport.cpp
)
target_link_libraries(VnaMeasurementIntegrationTests PRIVATE VnaMeasurement GTest::gtest_main)
target_include_directories(
    VnaMeasurementIntegrationTests PRIVATE
    ${PROJECT_SOURCE_DIR}/tests/Acquisition
    ${PROJECT_SOURCE_DIR}/tests/Measurement
    ${PROJECT_SOURCE_DIR}/modules/VnaAcquisition/src
    ${PROJECT_SOURCE_DIR}/modules/VnaHardware/src
    ${PROJECT_SOURCE_DIR}/modules/VnaMeasurement/src
)
vna_enable_warnings(VnaMeasurementIntegrationTests)

include(GoogleTest)
gtest_discover_tests(VnaMeasurementIntegrationTests DISCOVERY_TIMEOUT 30)
