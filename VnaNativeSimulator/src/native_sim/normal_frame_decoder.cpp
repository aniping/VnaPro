#include "normal_frame_decoder.h"

#include <algorithm>
#include <cstddef>
#include <limits>
#include <utility>

namespace vna_native {
namespace {

// 先用除法检查容量，避免 elementCount * elementSize 本身溢出；兼容 32 位 size_t。
bool CheckedElementBytes(std::uint64_t elementCount, std::size_t elementSize)
{
    if (elementSize == 0U) {
        return false;
    }
    const std::uint64_t maxSize = static_cast<std::uint64_t>((std::numeric_limits<std::size_t>::max)());
    return elementCount <= maxSize / elementSize;
}

}  // namespace

NormalFramePoint::NormalFramePoint() : frequencyRaw(0U), sourcePort(0U), ifBandwidthRaw(0U) {}

NormalFrameSnapshot::NormalFrameSnapshot() : sourcePort(0U) {}

void NormalFrameSnapshot::Swap(NormalFrameSnapshot& other) noexcept
{
    using std::swap;
    swap(sourcePort, other.sourcePort);
    points.swap(other.points);
    tableData.swap(other.tableData);
}

std::uint32_t NormalFrameDecoder::ValidateInputShape(std::uint32_t pointCount, const std::uint32_t* paramData,
    std::uint32_t paramDataLen, const double* tableData, std::uint32_t tableDataLen)
{
    if (paramData == NULL || tableData == NULL) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    if (pointCount == 0U) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    // 长度参数按元素计数而非字节计数；先升到 64 位，避免 6 * N / 9 * N 在 32 位回绕。
    // 参数帧为一个 quick 头字段 + N 组参数，功率表为 N 组、每组 9 个 double。
    const std::uint64_t points = static_cast<std::uint64_t>(pointCount);
    const std::uint64_t expectedParamCount = 1U + static_cast<std::uint64_t>(kParamValuesPerPoint) * points;
    const std::uint64_t expectedTableCount = static_cast<std::uint64_t>(kTableValuesPerPoint) * points;

    // 同时检查 ABI 的 uint32 长度上限和实际分配时的 size_t 字节上限。
    if (expectedParamCount > static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) ||
        expectedTableCount > static_cast<std::uint64_t>((std::numeric_limits<std::uint32_t>::max)()) ||
        !CheckedElementBytes(expectedParamCount, sizeof(std::uint32_t)) ||
        !CheckedElementBytes(expectedTableCount, sizeof(double)) ||
        !CheckedElementBytes(points, sizeof(NormalFramePoint))) {
        return DRV_SIM_SIZE_OVERFLOW;
    }

    if (paramDataLen != static_cast<std::uint32_t>(expectedParamCount) ||
        tableDataLen != static_cast<std::uint32_t>(expectedTableCount)) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    return DRV_SIM_OK;
}

std::uint32_t NormalFrameDecoder::Decode(std::uint32_t pointCount, const std::uint32_t* paramData,
    std::uint32_t paramDataLen, const double* tableData, std::uint32_t tableDataLen, NormalFrameSnapshot* output)
{
    if (output == NULL) {
        return DRV_SIM_INVALID_ARGUMENT;
    }

    const std::uint32_t shapeStatus = ValidateInputShape(pointCount, paramData, paramDataLen, tableData, tableDataLen);
    if (shapeStatus != DRV_SIM_OK) {
        return shapeStatus;
    }

    try {
        // paramData[0] 是 quick；第 i 点的 base = 1 + 6 * i：
        // base+0/+1 = 频率高/低 32 位，base+2 = sourcePort，base+3 = dynamicLink，
        // base+4 = IFBW 原值，base+5 = dynamicEnabled。首点 sourcePort 因而在下标 3。
        // 结构和值域错误优先于“不支持”字段，保证复合错误结果稳定。
        const std::uint32_t firstSourcePort = paramData[3U];
        if (firstSourcePort >= kNativePortCount) {
            return DRV_SIM_INVALID_ARGUMENT;
        }
        for (std::uint32_t pointIndex = 1U; pointIndex < pointCount; ++pointIndex) {
            const std::size_t base =
                1U + static_cast<std::size_t>(kParamValuesPerPoint) * static_cast<std::size_t>(pointIndex);
            const std::uint32_t sourcePort = paramData[base + 2U];
            if (sourcePort >= kNativePortCount || sourcePort != firstSourcePort) {
                return DRV_SIM_INVALID_ARGUMENT;
            }
        }

        // 普通帧只支持 quick、dynamicLink、dynamicEnabled 均为 0；不推断动态链路语义。
        if (paramData[0U] != 0U) {
            return DRV_SIM_NOT_SUPPORTED;
        }
        for (std::uint32_t pointIndex = 0U; pointIndex < pointCount; ++pointIndex) {
            const std::size_t base =
                1U + static_cast<std::size_t>(kParamValuesPerPoint) * static_cast<std::size_t>(pointIndex);
            if (paramData[base + 3U] != 0U || paramData[base + 5U] != 0U) {
                return DRV_SIM_NOT_SUPPORTED;
            }
        }

        // 全部校验完成后构建候选快照；分配或复制抛异常时，旧 output 仍然完整可用。
        NormalFrameSnapshot candidate;
        candidate.sourcePort = firstSourcePort;
        candidate.points.resize(static_cast<std::size_t>(pointCount));

        for (std::uint32_t pointIndex = 0U; pointIndex < pointCount; ++pointIndex) {
            const std::size_t base =
                1U + static_cast<std::size_t>(kParamValuesPerPoint) * static_cast<std::size_t>(pointIndex);
            NormalFramePoint& point = candidate.points[static_cast<std::size_t>(pointIndex)];
            // 先转为 64 位再左移，按高字在前的协议顺序拼接，保留全部原始频率位。
            point.frequencyRaw =
                (static_cast<std::uint64_t>(paramData[base]) << 32U) | static_cast<std::uint64_t>(paramData[base + 1U]);
            point.sourcePort = paramData[base + 2U];
            point.ifBandwidthRaw = paramData[base + 4U];
        }

        // 功率表仅深复制，不验证或解释各值；当前模拟 IQ 不读取该表。
        candidate.tableData.assign(tableData, tableData + static_cast<std::size_t>(tableDataLen));
        // noexcept 交换是唯一提交点；此前任何错误都不会部分修改调用方快照。
        output->Swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

}  // namespace vna_native
