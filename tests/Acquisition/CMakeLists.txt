add_executable(
    VnaAcquisitionTests
    AcquisitionAbortTest.cpp
    AcquisitionAdmissionTest.cpp
    AcquisitionCallbackGateTest.cpp
    AcquisitionMappingTest.cpp
    AcquisitionPrepareTest.cpp
    AcquisitionRobustnessTest.cpp
    AcquisitionRunTest.cpp
    AcquisitionTestSupport.cpp
    FakeVnaHardware.cpp
    FakeVnaHardwareCommands.cpp
    RecordingAcquisitionSink.cpp
)
target_link_libraries(VnaAcquisitionTests PRIVATE VnaAcquisition GTest::gtest_main)
target_include_directories(
    VnaAcquisitionTests PRIVATE
    ${PROJECT_SOURCE_DIR}/modules/VnaAcquisition/src
    ${PROJECT_SOURCE_DIR}/modules/VnaHardware/src
)
vna_enable_warnings(VnaAcquisitionTests)

include(GoogleTest)
gtest_discover_tests(VnaAcquisitionTests DISCOVERY_TIMEOUT 30)
