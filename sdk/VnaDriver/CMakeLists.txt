add_library(VnaDriverSdk INTERFACE)
target_include_directories(
    VnaDriverSdk INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)
