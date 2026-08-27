#ifndef VNA_TESTS_MEASUREMENT_MEASUREMENT_TEST_SUPPORT_H
#define VNA_TESTS_MEASUREMENT_MEASUREMENT_TEST_SUPPORT_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>

#include "VnaMeasurement.h"

namespace vna::test {

struct MeasurementWaveSample {
    std::complex<double> value{};
    std::uint32_t quality{0};
};

using MeasurementSampleProvider = std::function<MeasurementWaveSample(ExcitationId, VnaWaveKey, std::uint32_t)>;

VnaWaveKey incidentWave(PortId port);
VnaWaveKey outgoingWave(PortId port);
MeasurementLimits spaciousMeasurementLimits();
AcquisitionLimits spaciousAcquisitionLimits();
MeasurementRequest makeSparseMeasurementRequest();
MeasurementRequest makeQualityMeasurementRequest(std::size_t pointCount);
PreparedSweep makeMeasurementPreparedSweep(const VnaSweepPlan &plan, ConfigurationId configurationId);
Result<std::shared_ptr<const PreparedAcquisition>> makePreparedAcquisition(
    const PreparedSweep &prepared, AcquisitionLimits limits = spaciousAcquisitionLimits());
Result<std::shared_ptr<const RawSweepFrame>> makeRawSweepFrame(
    std::shared_ptr<const PreparedAcquisition> prepared, AcquisitionRunId runId, std::uint64_t sweepIndex,
    SweepId sweepId, const MeasurementSampleProvider &provider);

}  // namespace vna::test

#endif
