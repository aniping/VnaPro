add_library(
    VnaMeasurement STATIC
    src/MeasurementBinder.cpp
    src/MeasurementBindingValidation.cpp
    src/MeasurementCapacity.cpp
    src/MeasurementCompiler.cpp
    src/MeasurementValidation.cpp
    src/ObservationBuilder.cpp
    src/VnaMeasurementTypes.cpp
)
target_include_directories(
    VnaMeasurement PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(VnaMeasurement PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VnaMeasurement PUBLIC VnaAcquisition)
vna_enable_warnings(VnaMeasurement)
