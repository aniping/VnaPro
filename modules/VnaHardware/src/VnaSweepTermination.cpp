#include "VnaSweepTermination.h"

#include <algorithm>
#include <array>
#include <utility>

namespace vna::detail {
namespace {

    bool ordinaryResult(VnaErrorCode code)
    {
        return (code == VnaErrorCode::None) || (code == VnaErrorCode::Cancelled);
    }

    VnaErrorCode effectiveError(VnaErrorCode workerError, VnaErrorCode recordedError)
    {
        // Worker 的明确故障优先；普通返回才由数据通路或中止请求错误补充终态。
        return ordinaryResult(workerError) && (recordedError != VnaErrorCode::None) ? recordedError : workerError;
    }

    SweepTerminationReason reasonForError(VnaErrorCode code)
    {
        using Mapping = std::pair<VnaErrorCode, SweepTerminationReason>;
        // 只列需要保留独立终态语义的错误，其余错误统一归为 HardwareFault。
        static constexpr std::array<Mapping, 5> mappings{
            Mapping{VnaErrorCode::None, SweepTerminationReason::Completed},
            Mapping{VnaErrorCode::Cancelled, SweepTerminationReason::Aborted},
            Mapping{VnaErrorCode::ConnectionLost, SweepTerminationReason::ConnectionLost},
            Mapping{VnaErrorCode::Timeout, SweepTerminationReason::Timeout},
            Mapping{VnaErrorCode::DataLoss, SweepTerminationReason::DataLoss}};
        const auto match =
            std::find_if(mappings.begin(), mappings.end(), [code](const auto &item) { return item.first == code; });
        return match == mappings.end() ? SweepTerminationReason::HardwareFault : match->second;
    }

}  // namespace

bool abortRequestAwaitsSweep(VnaErrorCode requestError)
{
    return (requestError == VnaErrorCode::None) || (requestError == VnaErrorCode::InvalidState);
}

SweepTerminationReason normalizeSweepTermination(
    VnaErrorCode workerError, VnaErrorCode recordedError, std::size_t received, std::size_t expected)
{
    const VnaErrorCode code = effectiveError(workerError, recordedError);
    SweepTerminationReason reason = reasonForError(code);
    // Execute 返回 OK 但覆盖不完整仍属于数据丢失，不能报告成功。
    if ((reason == SweepTerminationReason::Completed) && (received != expected)) {
        reason = SweepTerminationReason::DataLoss;
    }
    return reason;
}

}  // namespace vna::detail
