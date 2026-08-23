#include "VnaHardwareResourceLimits.h"

#include <array>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

#include "VnaConfigurationFile.h"

namespace vna::detail {
namespace {

    constexpr std::size_t maximumPendingCommands = 1024;

    enum class LimitField : std::uint8_t {
        WavePoolSlots,
        WavePoolPayload,
        RequestedPlanPayload,
        PreparedSweepPayload,
        PendingCommands,
        PendingPreparePayload
    };

    struct LimitSpecification {
        const char *key;
        std::size_t minimum;
        std::size_t maximum;
        LimitField field;
        const char *description;
    };

    VnaError configurationError(std::string description)
    {
        VnaError error;
        error.code = VnaErrorCode::ConfigurationRejected;
        error.description = std::move(description);
        return error;
    }

    void setLimit(VnaHardwareResourceLimits &limits, LimitField field, std::size_t value)
    {
        switch (field) {
            case LimitField::WavePoolSlots:
                limits.wavePoolSlotCount = value;
                break;
            case LimitField::WavePoolPayload:
                limits.maximumWavePoolPayloadBytes = value;
                break;
            case LimitField::RequestedPlanPayload:
                limits.maximumRequestedPlanPayloadBytes = value;
                break;
            case LimitField::PreparedSweepPayload:
                limits.maximumPreparedSweepPayloadBytes = value;
                break;
            case LimitField::PendingCommands:
                limits.maximumPendingCommandsPerDevice = value;
                break;
            case LimitField::PendingPreparePayload:
                limits.maximumPendingPreparePayloadBytesPerDevice = value;
                break;
        }
    }

    const std::array<LimitSpecification, 6> &limitSpecifications()
    {
        static constexpr std::size_t sizeMaximum = std::numeric_limits<std::size_t>::max();
        static const std::array<LimitSpecification, 6> specifications{{
            {"wave_pool_slot_count", 1, maximumWavePoolSlotCount, LimitField::WavePoolSlots, "slot count"},
            {"maximum_wave_pool_payload_bytes", 1, sizeMaximum, LimitField::WavePoolPayload, "wave pool payload limit"},
            {"maximum_requested_plan_payload_bytes", 1, sizeMaximum, LimitField::RequestedPlanPayload,
             "requested plan payload limit"},
            {"maximum_prepared_sweep_payload_bytes", 1, sizeMaximum, LimitField::PreparedSweepPayload,
             "prepared sweep payload limit"},
            {"maximum_pending_commands_per_device", 2, maximumPendingCommands, LimitField::PendingCommands,
             "pending command limit"},
            {"maximum_pending_prepare_payload_bytes_per_device", 1, sizeMaximum, LimitField::PendingPreparePayload,
             "pending prepare payload limit"},
        }};
        return specifications;
    }

    Result<VnaHardwareResourceLimits> buildLimits(const ConfigurationValues &values)
    {
        const auto &specifications = limitSpecifications();
        if (values.size() != specifications.size()) {
            return Result<VnaHardwareResourceLimits>::failure(
                configurationError("hardware resource configuration keys are incomplete or unknown"));
        }
        VnaHardwareResourceLimits limits;
        for (const auto &specification : specifications) {
            const auto entry = values.find(specification.key);
            if (entry == values.end()) {
                return Result<VnaHardwareResourceLimits>::failure(
                    configurationError("hardware resource configuration keys are incomplete or unknown"));
            }
            auto parsed = parseConfigurationSize(
                entry->second, specification.minimum, specification.maximum, specification.description);
            if (!parsed.hasValue()) {
                return Result<VnaHardwareResourceLimits>::failure(parsed.error());
            }
            setLimit(limits, specification.field, parsed.value());
        }
        if ((limits.maximumPreparedSweepPayloadBytes < limits.maximumRequestedPlanPayloadBytes) ||
            (limits.maximumPendingPreparePayloadBytesPerDevice < limits.maximumRequestedPlanPayloadBytes)) {
            return Result<VnaHardwareResourceLimits>::failure(
                configurationError("hardware resource payload limits have an invalid relationship"));
        }
        return Result<VnaHardwareResourceLimits>::success(limits);
    }

}  // namespace

Result<VnaHardwareResourceLimits> loadVnaHardwareResourceLimits(const std::string &configurationFile)
{
    auto trusted = readTrustedConfigurationFile(configurationFile);
    if (!trusted.hasValue()) {
        return Result<VnaHardwareResourceLimits>::failure(trusted.error());
    }
    return loadVnaHardwareResourceLimits(trusted.value());
}

Result<VnaHardwareResourceLimits> loadVnaHardwareResourceLimits(const TrustedConfigurationFile &configurationFile)
{
    auto values = readConfigurationSection(configurationFile.content, "hardware_resources");
    if (!values.hasValue()) {
        return Result<VnaHardwareResourceLimits>::failure(values.error());
    }
    return buildLimits(values.value());
}

}  // namespace vna::detail
