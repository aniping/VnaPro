#include "AcquisitionCapacity.h"

#include <algorithm>
#include <exception>
#include <limits>
#include <utility>

namespace vna::detail {
namespace {

    struct BlockCopyRange {
        const PreparedLayoutMapping *layout{nullptr};
        std::size_t layoutIndex{0};
        std::size_t localPointOffset{0};
        std::size_t pointCount{0};
    };

    struct PreparedBlockCopy {
        BlockCopyRange range;
        std::size_t nextRecords{0};
        std::size_t nextSamples{0};
    };

    VnaError blockError(VnaErrorCode code, SweepId sweepId, const char *description)
    {
        VnaError error = acquisitionError(code, description);
        error.sweepId = sweepId;
        return error;
    }

    const PreparedLayoutMapping *findLayout(
        const PreparedMapping &mapping, LayoutId layoutId, std::size_t &layoutIndex) noexcept
    {
        std::size_t first = 0;
        std::size_t count = mapping.layouts.size();
        while (count != 0) {
            const std::size_t step = count / 2;
            const std::size_t index = first + step;
            if (mapping.layouts[index].layoutId < layoutId) {
                first = index + 1;
                count -= step + 1;
            } else {
                count = step;
            }
        }
        if ((first == mapping.layouts.size()) || (mapping.layouts[first].layoutId != layoutId)) {
            return nullptr;
        }
        layoutIndex = first;
        return &mapping.layouts[first];
    }

    VnaError validatePointRange(const PreparedMapping &mapping, const VnaWaveBlock &block, BlockCopyRange &range)
    {
        const auto &layout = *range.layout;
        if ((layout.pointIndexOffset > mapping.pointIndices.size()) ||
            (layout.pointCount > (mapping.pointIndices.size() - layout.pointIndexOffset))) {
            return blockError(VnaErrorCode::InternalError, block.sweepId(), "prepared point range is invalid");
        }
        const auto first = mapping.pointIndices.begin() + layout.pointIndexOffset;
        const auto last = first + layout.pointCount;
        const auto found = std::lower_bound(first, last, block.firstPointIndex());
        if (found == last || *found != block.firstPointIndex()) {
            return blockError(VnaErrorCode::ProtocolError, block.sweepId(), "wave block starts outside its layout");
        }
        range.localPointOffset = static_cast<std::size_t>(found - first);
        if (range.pointCount > (layout.pointCount - range.localPointOffset)) {
            return blockError(VnaErrorCode::ProtocolError, block.sweepId(), "wave block exceeds its layout range");
        }
        for (std::size_t index = 0; index < range.pointCount; ++index) {
            const std::uint64_t expected = static_cast<std::uint64_t>(block.firstPointIndex()) + index;
            if ((expected > std::numeric_limits<std::uint32_t>::max()) ||
                (first[range.localPointOffset + index] != expected)) {
                return blockError(
                    VnaErrorCode::ProtocolError, block.sweepId(), "wave block crosses a sparse layout hole");
            }
        }
        return {};
    }

    VnaError locateBlock(const PreparedMapping &mapping, const VnaWaveBlock &block, BlockCopyRange &range)
    {
        range.layout = findLayout(mapping, block.layoutId(), range.layoutIndex);
        range.pointCount = block.pointCount();
        if (range.layout == nullptr) {
            return blockError(VnaErrorCode::ProtocolError, block.sweepId(), "wave block has an unknown LayoutId");
        }
        if ((range.pointCount == 0) || (block.waveCount() != range.layout->waveCount)) {
            return blockError(VnaErrorCode::ProtocolError, block.sweepId(), "wave block shape does not match layout");
        }
        if ((range.layout->waveMappingOffset > mapping.waves.size()) ||
            (range.layout->waveCount > (mapping.waves.size() - range.layout->waveMappingOffset))) {
            return blockError(VnaErrorCode::InternalError, block.sweepId(), "prepared wave range is invalid");
        }
        return validatePointRange(mapping, block, range);
    }

    VnaError validateBlockProgress(
        const PreparedMapping &mapping, const VnaWaveBlock &block, const BlockCopyRange &range,
        const RawFrameAssembly &assembly)
    {
        if ((assembly.nextExpectedPointOffsets.size() != mapping.layouts.size()) ||
            (range.layoutIndex >= assembly.nextExpectedPointOffsets.size())) {
            return blockError(VnaErrorCode::InternalError, block.sweepId(), "raw frame progress storage is invalid");
        }
        if ((block.sequence() != assembly.nextBlockSequence) ||
            (assembly.nextBlockSequence == std::numeric_limits<std::uint64_t>::max())) {
            return blockError(VnaErrorCode::ProtocolError, block.sweepId(), "wave block sequence is not contiguous");
        }
        if (range.localPointOffset != assembly.nextExpectedPointOffsets[range.layoutIndex]) {
            return blockError(
                VnaErrorCode::ProtocolError, block.sweepId(), "wave block is repeated, overlapping, or out of order");
        }
        return {};
    }

    VnaError validateTargets(
        const PreparedMapping &mapping, const BlockCopyRange &range, const RawFrameAssembly &assembly, SweepId sweepId)
    {
        for (std::size_t index = 0; index < range.layout->waveCount; ++index) {
            const auto &wave = mapping.waves[range.layout->waveMappingOffset + index];
            if ((wave.destinationOffset > assembly.values.size()) ||
                (range.localPointOffset > (assembly.values.size() - wave.destinationOffset))) {
                return blockError(VnaErrorCode::InternalError, sweepId, "wave destination offset is invalid");
            }
            const std::size_t destination = wave.destinationOffset + range.localPointOffset;
            if ((range.pointCount > (assembly.values.size() - destination)) ||
                (destination > assembly.quality.size()) ||
                (range.pointCount > (assembly.quality.size() - destination))) {
                return blockError(VnaErrorCode::InternalError, sweepId, "wave destination range is invalid");
            }
        }
        return {};
    }

    VnaError checkedCopyTotals(
        const PreparedAcquisition &prepared, const BlockCopyRange &range, const RawFrameAssembly &assembly,
        std::size_t &nextRecords, std::size_t &nextSamples)
    {
        std::size_t sampleDelta = 0;
        if (!checkedMultiply(range.pointCount, range.layout->waveCount, sampleDelta) ||
            !checkedAdd(assembly.copiedPointExcitationRecordCount, range.pointCount, nextRecords) ||
            !checkedAdd(assembly.copiedWaveSampleCount, sampleDelta, nextSamples) ||
            (nextRecords > prepared.pointExcitationRecordCount()) ||
            (nextSamples > prepared.waveSampleCountPerSweep())) {
            return acquisitionError(VnaErrorCode::ProtocolError, "wave block copy totals exceed the prepared frame");
        }
        return {};
    }

    void copyWaves(
        const PreparedMapping &mapping, const VnaWaveBlock &block, const BlockCopyRange &range,
        RawFrameAssembly &assembly)
    {
        for (std::size_t index = 0; index < range.layout->waveCount; ++index) {
            const auto &wave = mapping.waves[range.layout->waveMappingOffset + index];
            const std::size_t destination = wave.destinationOffset + range.localPointOffset;
            block.copyWaveTo(
                index, assembly.values.data() + destination, assembly.quality.data() + destination, range.pointCount);
        }
    }

    VnaError prepareBlockCopy(
        const PreparedMapping &mapping, const PreparedAcquisition &prepared, const VnaWaveBlock &block,
        const RawFrameAssembly &assembly, PreparedBlockCopy &copy)
    {
        VnaError error = locateBlock(mapping, block, copy.range);
        if (error.code != VnaErrorCode::None) {
            return error;
        }
        error = validateTargets(mapping, copy.range, assembly, block.sweepId());
        if (error.code != VnaErrorCode::None) {
            return error;
        }
        error = validateBlockProgress(mapping, block, copy.range, assembly);
        if (error.code != VnaErrorCode::None) {
            return error;
        }
        return checkedCopyTotals(prepared, copy.range, assembly, copy.nextRecords, copy.nextSamples);
    }

}  // namespace

Result<RawFrameAssembly> allocateRawFrameAssembly(const PreparedAcquisition &prepared)
{
    try {
        RawFrameAssembly assembly;
        assembly.values.resize(prepared.waveSampleCountPerSweep());
        assembly.quality.resize(prepared.waveSampleCountPerSweep());
        assembly.nextExpectedPointOffsets.resize(prepared.hardwarePrepared().layouts.size());
        return Result<RawFrameAssembly>::success(std::move(assembly));
    } catch (const std::exception &) {
        return Result<RawFrameAssembly>::failure(
            acquisitionError(VnaErrorCode::InternalError, "raw frame allocation failed"));
    } catch (...) {
        return Result<RawFrameAssembly>::failure(
            acquisitionError(VnaErrorCode::InternalError, "raw frame allocation failed"));
    }
}

VnaError copyBlockIntoFrame(const PreparedAcquisition &prepared, const VnaWaveBlock &block, RawFrameAssembly &assembly)
{
    try {
        if (!prepared.mapping_ || (assembly.values.size() != prepared.waveSampleCountPerSweep()) ||
            (assembly.quality.size() != prepared.waveSampleCountPerSweep())) {
            return blockError(VnaErrorCode::InternalError, block.sweepId(), "raw frame storage is invalid");
        }
        PreparedBlockCopy copy;
        VnaError error = prepareBlockCopy(*prepared.mapping_, prepared, block, assembly, copy);
        if (error.code != VnaErrorCode::None) {
            if (error.sweepId == 0) {
                error.sweepId = block.sweepId();
            }
            return error;
        }
        copyWaves(*prepared.mapping_, block, copy.range, assembly);
        assembly.nextExpectedPointOffsets[copy.range.layoutIndex] += copy.range.pointCount;
        ++assembly.nextBlockSequence;
        assembly.copiedPointExcitationRecordCount = copy.nextRecords;
        assembly.copiedWaveSampleCount = copy.nextSamples;
        return {};
    } catch (const std::exception &) {
        return blockError(VnaErrorCode::InternalError, block.sweepId(), "wave block copy failed");
    } catch (...) {
        return blockError(VnaErrorCode::InternalError, block.sweepId(), "wave block copy failed");
    }
}

bool rawFrameAssemblyComplete(const PreparedAcquisition &prepared, const RawFrameAssembly &assembly) noexcept
{
    if (!prepared.mapping_ || (assembly.nextExpectedPointOffsets.size() != prepared.mapping_->layouts.size())) {
        return false;
    }
    for (std::size_t index = 0; index < prepared.mapping_->layouts.size(); ++index) {
        if (assembly.nextExpectedPointOffsets[index] != prepared.mapping_->layouts[index].pointCount) {
            return false;
        }
    }
    return true;
}

Result<std::shared_ptr<const RawSweepFrame>> freezeRawSweepFrame(
    AcquisitionRunId runId, std::uint64_t sweepIndex, SweepId hardwareSweepId,
    std::shared_ptr<const PreparedAcquisition> prepared, RawFrameAssembly assembly)
{
    if (!prepared || (assembly.values.size() != prepared->waveSampleCountPerSweep()) ||
        (assembly.quality.size() != prepared->waveSampleCountPerSweep()) ||
        (assembly.copiedPointExcitationRecordCount != prepared->pointExcitationRecordCount()) ||
        (assembly.copiedWaveSampleCount != prepared->waveSampleCountPerSweep()) ||
        !rawFrameAssemblyComplete(*prepared, assembly)) {
        return Result<std::shared_ptr<const RawSweepFrame>>::failure(
            acquisitionError(VnaErrorCode::InternalError, "raw frame is incomplete"));
    }
    try {
        auto *raw = new RawSweepFrame(runId, sweepIndex, hardwareSweepId, std::move(prepared), std::move(assembly));
        return Result<std::shared_ptr<const RawSweepFrame>>::success(std::shared_ptr<const RawSweepFrame>(raw));
    } catch (...) {
        return Result<std::shared_ptr<const RawSweepFrame>>::failure(
            acquisitionError(VnaErrorCode::InternalError, "raw frame freeze failed"));
    }
}

}  // namespace vna::detail
