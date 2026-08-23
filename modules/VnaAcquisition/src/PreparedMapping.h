#ifndef VNA_ACQUISITION_PREPARED_MAPPING_H
#define VNA_ACQUISITION_PREPARED_MAPPING_H

#include <complex>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

#include "VnaAcquisition.h"

namespace vna::detail {

struct PreparedLayoutMapping {
    LayoutId layoutId{0};
    ExcitationId excitationId{0};
    std::size_t pointIndexOffset{0};
    std::size_t pointCount{0};
    std::size_t waveMappingOffset{0};
    std::size_t waveCount{0};
};

struct PreparedWaveMapping {
    VnaWaveKey wave;
    std::size_t destinationOffset{0};
};

// 这三个扁平数组是全部持久复制索引；同一 layout 的波共享 pointIndices。
struct PreparedMapping {
    std::vector<std::uint32_t> pointIndices;
    std::vector<PreparedLayoutMapping> layouts;
    std::vector<PreparedWaveMapping> waves;
};

struct AcquisitionPlanInspection {
    std::size_t pointExcitationRecordCount{0};
    std::size_t waveSampleCount{0};
    std::size_t rawFramePayloadBytes{0};
    std::size_t preparedMappingPayloadBytes{0};
};

struct RawFrameAssembly {
    std::vector<std::complex<double>> values;
    std::vector<std::uint32_t> quality;
    std::vector<std::size_t> nextExpectedPointOffsets;
    std::uint64_t nextBlockSequence{0};
    std::size_t copiedPointExcitationRecordCount{0};
    std::size_t copiedWaveSampleCount{0};
};

Result<AcquisitionPlanInspection> inspectRequestedPlan(const VnaSweepPlan &plan, const AcquisitionLimits &limits);
Result<std::shared_ptr<const PreparedAcquisition>> compilePreparedAcquisition(
    std::shared_ptr<const PreparedSweep> hardwarePrepared, const AcquisitionLimits &limits);
Result<RawFrameAssembly> allocateRawFrameAssembly(const PreparedAcquisition &prepared);
VnaError copyBlockIntoFrame(const PreparedAcquisition &prepared, const VnaWaveBlock &block, RawFrameAssembly &assembly);
bool rawFrameAssemblyComplete(const PreparedAcquisition &prepared, const RawFrameAssembly &assembly) noexcept;
Result<std::shared_ptr<const RawSweepFrame>> freezeRawSweepFrame(
    AcquisitionRunId runId, std::uint64_t sweepIndex, SweepId hardwareSweepId,
    std::shared_ptr<const PreparedAcquisition> prepared, RawFrameAssembly assembly);

}  // namespace vna::detail

#endif
