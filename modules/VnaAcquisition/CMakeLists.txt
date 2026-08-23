add_library(
    VnaAcquisition STATIC
    src/AcquisitionCapacity.cpp
    src/PreparedMapping.cpp
    src/RawFrameAssembly.cpp
    src/RequestedPlanInspection.cpp
    src/VnaAcquisitionAbortEvents.cpp
    src/VnaAcquisitionCommandFailures.cpp
    src/VnaAcquisitionControl.cpp
    src/VnaAcquisitionEngine.cpp
    src/VnaAcquisitionEventEntry.cpp
    src/VnaAcquisitionPrepare.cpp
    src/VnaAcquisitionPublication.cpp
    src/VnaAcquisitionRunEvents.cpp
    src/VnaAcquisitionTypes.cpp
)
target_include_directories(
    VnaAcquisition PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(VnaAcquisition PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VnaAcquisition PUBLIC VnaHardware)
vna_enable_warnings(VnaAcquisition)
