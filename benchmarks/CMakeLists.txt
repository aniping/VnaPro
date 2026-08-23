add_executable(
    VnaLocalThroughputBenchmark
    AcquisitionAssemblyBenchmark.cpp
    BenchmarkModel.cpp
    BenchmarkReport.cpp
    LocalThroughputBenchmark.cpp
    PipelineBenchmarkEvents.cpp
    PipelineBenchmarkSession.cpp
    PipelineThroughputBenchmark.cpp
)
configure_file(vnapro-benchmark.conf vnapro-benchmark.conf COPYONLY)
target_include_directories(
    VnaLocalThroughputBenchmark PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
    ${PROJECT_SOURCE_DIR}/modules/VnaAcquisition/src
    ${PROJECT_SOURCE_DIR}/modules/VnaHardware/src
)
target_compile_definitions(
    VnaLocalThroughputBenchmark PRIVATE
    VNA_BENCHMARK_CONFIGURATION="$<CONFIG>"
    VNA_BENCHMARK_DRIVER_DIRECTORY="$<TARGET_FILE_DIR:VnaDriverSimulator>"
    VNA_BENCHMARK_LOG_CONFIGURATION="${CMAKE_CURRENT_BINARY_DIR}/vnapro-benchmark.conf"
)
target_link_libraries(VnaLocalThroughputBenchmark PRIVATE VnaAcquisition)
add_dependencies(VnaLocalThroughputBenchmark VnaDriverSimulator)
vna_enable_warnings(VnaLocalThroughputBenchmark)
