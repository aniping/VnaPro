add_library(
    VnaHardware STATIC
    src/DriverModuleAdapter.cpp
    src/DriverModule.cpp
    src/DriverResultValidation.cpp
    src/DriverPluginLoader.cpp
    src/HostServices.cpp
    src/PosixDriverFile.cpp
    src/VnaBufferPool.cpp
    src/VnaEventDispatcher.cpp
    src/VnaEventQueue.cpp
    src/VnaFrameTracker.cpp
    src/VnaHardware.cpp
    src/VnaHardwareDiscovery.cpp
    src/VnaHardwareManager.cpp
    src/VnaHardwareSession.cpp
    src/VnaLogConfiguration.cpp
    src/VnaLogger.cpp
    src/VnaSessionCommands.cpp
    src/VnaSessionController.cpp
    src/VnaSessionShutdown.cpp
    src/VnaSweepCommands.cpp
    src/VnaSweepTermination.cpp
    src/VnaPlanMarshaller.cpp
    src/VnaValidation.cpp
    src/VnaWaveBlockCommands.cpp
    src/VnaWaveBlock.cpp
)
target_include_directories(
    VnaHardware PUBLIC
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
target_include_directories(VnaHardware PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/src)
target_link_libraries(VnaHardware PUBLIC VnaDriverSdk PRIVATE ${CMAKE_DL_LIBS} spdlog::spdlog)
if(CMAKE_SYSTEM_NAME STREQUAL "Linux")
    target_compile_definitions(VnaHardware PUBLIC _GLIBCXX_USE_CXX11_ABI=1)
endif()
vna_enable_warnings(VnaHardware)
