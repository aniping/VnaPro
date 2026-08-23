add_library(
    VnaSimulatorCore STATIC
    src/SimulatorDevice.cpp
    src/SimulatorExecution.cpp
    src/SimulatorLayout.cpp
    src/SimulatorPrepareTransaction.cpp
    src/SimulatorStatus.cpp
    src/SimulatorTiming.cpp
    src/SimulatorWaveBlockLease.cpp
    src/SimulatorWaveModel.cpp
)
target_include_directories(VnaSimulatorCore PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VnaSimulatorCore PUBLIC VnaDriverSdk)
set_target_properties(
    VnaSimulatorCore PROPERTIES
    POSITION_INDEPENDENT_CODE ON
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    # Core 的公开 C++ 类型跨目标传递 std::vector，必须与模块和测试固定为同一 libstdc++ ABI。
    target_compile_definitions(VnaSimulatorCore PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
endif()
vna_enable_warnings(VnaSimulatorCore)

add_library(VnaDriverSimulator MODULE src/SimulatorDriver.cpp)
target_include_directories(VnaDriverSimulator PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VnaDriverSimulator PRIVATE VnaDriverSdk VnaSimulatorCore)
set_target_properties(
    VnaDriverSimulator PROPERTIES
    PREFIX ""
    CXX_VISIBILITY_PRESET hidden
    VISIBILITY_INLINES_HIDDEN YES
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/drivers"
)
vna_enable_warnings(VnaDriverSimulator)
