#ifndef VNA_MEASUREMENT_MEASUREMENT_INTERNAL_H
#define VNA_MEASUREMENT_MEASUREMENT_INTERNAL_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "VnaMeasurement.h"

namespace vna::detail {

struct FlatPlanHeader {
    MeasurementLimits limits{};
    TriggerMode triggerMode{TriggerMode::Internal};
    bool allowQuantization{false};
    bool allowMultiplePhysicalSweeps{false};
    bool requirePhaseCoherence{false};
    bool requireAtomicConfiguration{false};
    bool hasRatio{false};
};

struct FlatPointSignature {
    PointId pointId{0};
    double frequencyHz{0.0};
    double ifBandwidthHz{0.0};
    std::int64_t dwellNanoseconds{0};
    std::size_t excitationOffset{0};
    std::size_t excitationCount{0};
};

struct FlatExcitationSignature {
    ExcitationId excitationId{0};
    std::size_t sourceOffset{0};
    std::size_t sourceCount{0};
    std::size_t dependencyOffset{0};
    std::size_t dependencyCount{0};
};

struct FlatSourceSignature {
    PortId port{0};
    double powerDbm{0.0};
    double phaseDegrees{0.0};
};

struct CompiledDependency {
    ExcitationId excitationId{0};
    VnaWaveKey wave{};
};

struct BindingExcitationDescription {
    ExcitationId excitationId{0};
    std::size_t compiledDependencyOffset{0};
    std::size_t dependencyCount{0};
    std::size_t pointCount{0};
};

struct PreparedExcitationMapping {
    ExcitationId excitationId{0};
    std::size_t pointIndexOffset{0};
    std::size_t pointCount{0};
    std::size_t dependencyOffset{0};
    std::size_t dependencyCount{0};
};

struct BoundDependency {
    ExcitationId excitationId{0};
    VnaWaveKey wave{};
    std::size_t excitationMappingIndex{0};
};

enum class BoundExpressionKind : std::uint8_t { Direct, Ratio };

struct BoundObservation {
    ObservationId id{0};
    ExcitationId excitationId{0};
    BoundExpressionKind kind{BoundExpressionKind::Direct};
    std::size_t excitationMappingIndex{0};
    std::size_t numeratorDependencyOrdinal{0};
    std::size_t denominatorDependencyOrdinal{0};
    std::size_t outputOffset{0};
    std::size_t sampleCount{0};
};

struct MeasurementShape {
    std::size_t pointCount{0};
    std::size_t excitationOccurrenceCount{0};
    std::size_t sourceCount{0};
    std::size_t excitationCount{0};
    std::size_t dependencyCount{0};
    std::size_t observationCount{0};
    std::size_t pointIndexCount{0};
    std::size_t observationSampleCount{0};
};

struct MeasurementPayload {
    std::size_t preparedBytes{0};
    std::size_t buildBytes{0};
};

struct ExcitationRequirements {
    ExcitationId excitationId{0};
    std::size_t occurrenceCount{0};
    std::vector<VnaWaveKey> waves;
};

struct CompilationModel {
    std::vector<ExcitationRequirements> excitations;
    MeasurementShape shape{};
    bool hasRatio{false};
};

struct CompiledMeasurementData {
    FlatPlanHeader header{};
    std::vector<FlatPointSignature> points;
    std::vector<FlatExcitationSignature> excitations;
    std::vector<FlatSourceSignature> sources;
    std::vector<ObservationDefinition> observations;
    std::vector<CompiledDependency> dependencies;
};

struct PreparedMeasurementData {
    std::vector<PreparedExcitationMapping> excitations;
    std::vector<std::uint32_t> pointIndices;
    std::vector<BoundDependency> dependencies;
    std::vector<BoundObservation> observations;
    std::size_t observationSampleCount{0};
};

struct ObservationFrameData {
    AcquisitionRunId runId{0};
    std::uint64_t sweepIndex{0};
    SweepId hardwareSweepId{0};
    std::vector<std::complex<double>> values;
    std::vector<ObservationSampleStatus> status;
};

bool checkedMeasurementAdd(std::size_t left, std::size_t right, std::size_t &result) noexcept;
bool checkedMeasurementMultiply(std::size_t left, std::size_t right, std::size_t &result) noexcept;
bool checkedMeasurementAccumulate(std::size_t value, std::size_t &total) noexcept;
VnaError measurementError(VnaErrorCode code, std::string description);
Result<MeasurementPayload> inspectMeasurementPayload(const MeasurementShape &shape, MeasurementLimits limits);
Result<bool> preflightMeasurementRequestCapacity(const MeasurementRequest &request, MeasurementLimits limits);
Result<CompilationModel> inspectMeasurementRequest(const MeasurementRequest &request, MeasurementLimits limits);
VnaError validateMeasurementPreparedPlan(
    const CompiledMeasurementData &compiled, const PreparedSweep &hardwarePrepared);
VnaError validateMeasurementLayouts(
    const CompiledMeasurementData &compiled, const std::vector<VnaWaveLayout> &layouts,
    const std::vector<BindingExcitationDescription> &excitations);

}  // namespace vna::detail

#endif
