#include "AcquisitionCapacity.h"

#include <algorithm>
#include <limits>
#include <map>
#include <new>
#include <stdexcept>
#include <utility>

namespace vna::detail {
namespace {

    using LayoutDefinitions = std::map<ExcitationId, const VnaWaveLayout *>;
    using LayoutPositions = std::map<ExcitationId, std::size_t>;

    VnaError protocolError(const char *description)
    {
        return acquisitionError(VnaErrorCode::ProtocolError, description);
    }

    bool sameWaveSequence(const std::vector<VnaWaveKey> &left, const std::vector<VnaWaveKey> &right)
    {
        if (left.size() != right.size()) {
            return false;
        }
        for (std::size_t index = 0; index < left.size(); ++index) {
            if ((left[index].kind != right[index].kind) || (left[index].port != right[index].port)) {
                return false;
            }
        }
        return true;
    }

    Result<AcquisitionShape> describePreparedSweep(const PreparedSweep &prepared)
    {
        if (prepared.effectivePlan.points.size() > std::numeric_limits<std::uint32_t>::max()) {
            return Result<AcquisitionShape>::failure(
                acquisitionError(VnaErrorCode::UnsupportedPlan, "effective point count exceeds uint32 indices"));
        }
        AcquisitionShape shape;
        shape.layoutCount = prepared.layouts.size();
        for (const auto &layout : prepared.layouts) {
            if (!checkedAccumulate(layout.waveKeys.size(), shape.waveMappingCount)) {
                return Result<AcquisitionShape>::failure(
                    acquisitionError(VnaErrorCode::UnsupportedPlan, "effective wave mapping count overflows"));
            }
        }
        for (const auto &point : prepared.effectivePlan.points) {
            if (!checkedAccumulate(point.excitations.size(), shape.pointExcitationRecordCount)) {
                return Result<AcquisitionShape>::failure(
                    acquisitionError(VnaErrorCode::UnsupportedPlan, "effective record count overflows"));
            }
            for (const auto &excitation : point.excitations) {
                if (!checkedAccumulate(excitation.requiredWaves.size(), shape.waveSampleCount)) {
                    return Result<AcquisitionShape>::failure(
                        acquisitionError(VnaErrorCode::UnsupportedPlan, "effective sample count overflows"));
                }
            }
        }
        return Result<AcquisitionShape>::success(shape);
    }

    VnaError registerLayouts(
        const std::vector<VnaWaveLayout> &source, PreparedMapping &mapping, LayoutDefinitions &definitions,
        LayoutPositions &positions)
    {
        mapping.layouts.reserve(source.size());
        for (const auto &layout : source) {
            if ((layout.layoutId == 0) || !definitions.emplace(layout.excitationId, &layout).second) {
                return protocolError("prepared layouts contain an invalid or duplicate identifier");
            }
            mapping.layouts.push_back(
                PreparedLayoutMapping{layout.layoutId, layout.excitationId, 0, 0, 0, layout.waveKeys.size()});
        }
        std::sort(mapping.layouts.begin(), mapping.layouts.end(), [](const auto &left, const auto &right) {
            return left.layoutId < right.layoutId;
        });
        for (std::size_t index = 0; index < mapping.layouts.size(); ++index) {
            if ((index != 0) && (mapping.layouts[index - 1].layoutId == mapping.layouts[index].layoutId)) {
                return protocolError("prepared layouts contain a duplicate LayoutId");
            }
            positions.emplace(mapping.layouts[index].excitationId, index);
        }
        return {};
    }

    VnaError countLayoutPoints(
        const VnaSweepPlan &plan, const LayoutDefinitions &definitions, const LayoutPositions &positions,
        PreparedMapping &mapping)
    {
        std::vector<std::size_t> lastPoint(mapping.layouts.size(), std::numeric_limits<std::size_t>::max());
        for (std::size_t pointIndex = 0; pointIndex < plan.points.size(); ++pointIndex) {
            for (const auto &excitation : plan.points[pointIndex].excitations) {
                const auto definition = definitions.find(excitation.excitationId);
                const auto position = positions.find(excitation.excitationId);
                if ((definition == definitions.end()) || (position == positions.end()) ||
                    !sameWaveSequence(excitation.requiredWaves, definition->second->waveKeys)) {
                    return protocolError("effective plan does not match prepared layouts");
                }
                if (lastPoint[position->second] == pointIndex) {
                    return protocolError("effective point repeats an excitation layout");
                }
                lastPoint[position->second] = pointIndex;
                if (!checkedAccumulate(1, mapping.layouts[position->second].pointCount)) {
                    return protocolError("layout point count overflows");
                }
            }
        }
        return {};
    }

    VnaError assignRanges(const LayoutDefinitions &definitions, std::size_t expectedSamples, PreparedMapping &mapping)
    {
        std::size_t pointOffset = 0;
        std::size_t destinationOffset = 0;
        for (auto &layout : mapping.layouts) {
            if ((layout.pointCount == 0) || !checkedAccumulate(layout.pointCount, pointOffset)) {
                return protocolError("prepared layout is unused or its point range overflows");
            }
            layout.pointIndexOffset = pointOffset - layout.pointCount;
            layout.waveMappingOffset = mapping.waves.size();
            const auto definition = definitions.find(layout.excitationId);
            for (const auto &wave : definition->second->waveKeys) {
                if ((destinationOffset > expectedSamples) ||
                    (layout.pointCount > (expectedSamples - destinationOffset))) {
                    return protocolError("wave destination range exceeds the frame");
                }
                mapping.waves.push_back(PreparedWaveMapping{wave, destinationOffset});
                destinationOffset += layout.pointCount;
            }
        }
        if ((pointOffset != mapping.pointIndices.size()) || (destinationOffset != expectedSamples)) {
            return protocolError("prepared mapping totals do not match the effective plan");
        }
        return {};
    }

    VnaError fillPointIndices(const VnaSweepPlan &plan, const LayoutPositions &positions, PreparedMapping &mapping)
    {
        std::vector<std::size_t> filled(mapping.layouts.size(), 0);
        for (std::size_t pointIndex = 0; pointIndex < plan.points.size(); ++pointIndex) {
            for (const auto &excitation : plan.points[pointIndex].excitations) {
                const std::size_t position = positions.find(excitation.excitationId)->second;
                const auto &layout = mapping.layouts[position];
                if (filled[position] >= layout.pointCount) {
                    return protocolError("layout point range was overfilled");
                }
                const std::size_t target = layout.pointIndexOffset + filled[position];
                mapping.pointIndices[target] = static_cast<std::uint32_t>(pointIndex);
                ++filled[position];
            }
        }
        for (std::size_t index = 0; index < filled.size(); ++index) {
            if (filled[index] != mapping.layouts[index].pointCount) {
                return protocolError("layout point range was not completely filled");
            }
        }
        return {};
    }

    Result<std::unique_ptr<const PreparedMapping>> buildMapping(
        const PreparedSweep &prepared, const AcquisitionShape &shape)
    {
        auto mapping = std::make_unique<PreparedMapping>();
        mapping->pointIndices.resize(shape.pointExcitationRecordCount);
        mapping->waves.reserve(shape.waveMappingCount);
        LayoutDefinitions definitions;
        LayoutPositions positions;
        VnaError error = registerLayouts(prepared.layouts, *mapping, definitions, positions);
        if (error.code == VnaErrorCode::None) {
            error = countLayoutPoints(prepared.effectivePlan, definitions, positions, *mapping);
        }
        if (error.code == VnaErrorCode::None) {
            error = assignRanges(definitions, shape.waveSampleCount, *mapping);
        }
        if (error.code == VnaErrorCode::None) {
            error = fillPointIndices(prepared.effectivePlan, positions, *mapping);
        }
        if (error.code != VnaErrorCode::None) {
            return Result<std::unique_ptr<const PreparedMapping>>::failure(std::move(error));
        }
        return Result<std::unique_ptr<const PreparedMapping>>::success(std::move(mapping));
    }

}  // namespace

Result<std::shared_ptr<const PreparedAcquisition>> compilePreparedAcquisition(
    std::shared_ptr<const PreparedSweep> hardwarePrepared, const AcquisitionLimits &limits)
{
    try {
        if (!hardwarePrepared) {
            return Result<std::shared_ptr<const PreparedAcquisition>>::failure(
                acquisitionError(VnaErrorCode::ProtocolError, "prepared sweep is empty"));
        }
        auto shape = describePreparedSweep(*hardwarePrepared);
        if (!shape.hasValue()) {
            return Result<std::shared_ptr<const PreparedAcquisition>>::failure(shape.error());
        }
        auto inspection = inspectAcquisitionShape(shape.value(), limits);
        if (!inspection.hasValue()) {
            return Result<std::shared_ptr<const PreparedAcquisition>>::failure(inspection.error());
        }
        auto mapping = buildMapping(*hardwarePrepared, shape.value());
        if (!mapping.hasValue()) {
            return Result<std::shared_ptr<const PreparedAcquisition>>::failure(mapping.error());
        }
        const auto records = inspection.value().pointExcitationRecordCount;
        const auto samples = inspection.value().waveSampleCount;
        auto *raw = new PreparedAcquisition(std::move(hardwarePrepared), records, samples, mapping.takeValue());
        return Result<std::shared_ptr<const PreparedAcquisition>>::success(
            std::shared_ptr<const PreparedAcquisition>(raw));
    } catch (const std::bad_alloc &) {
        return Result<std::shared_ptr<const PreparedAcquisition>>::failure(
            acquisitionError(VnaErrorCode::InternalError, "prepared mapping allocation failed"));
    } catch (const std::length_error &) {
        return Result<std::shared_ptr<const PreparedAcquisition>>::failure(
            acquisitionError(VnaErrorCode::InternalError, "prepared mapping allocation exceeded max_size"));
    } catch (...) {
        return Result<std::shared_ptr<const PreparedAcquisition>>::failure(
            acquisitionError(VnaErrorCode::InternalError, "prepared mapping compilation failed"));
    }
}

}  // namespace vna::detail
