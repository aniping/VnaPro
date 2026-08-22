add_library(
    VnaDriverSimulator MODULE
    src/SimulatorDevice.cpp
    src/SimulatorDriver.cpp
    src/SimulatorExecution.cpp
    src/SimulatorPrepareTransaction.cpp
    src/SimulatorWaveModel.cpp
)
target_include_directories(VnaDriverSimulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VnaDriverSimulator PRIVATE VnaDriverSdk)
set_target_properties(
    VnaDriverSimulator PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/drivers"
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(VnaDriverSimulator PRIVATE _GLIBCXX_USE_CXX11_ABI=1)
endif()
vna_enable_warnings(VnaDriverSimulator)
