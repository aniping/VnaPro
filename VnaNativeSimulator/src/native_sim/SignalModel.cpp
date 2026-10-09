#include "SignalModel.h"

#include <cstddef>
#include <limits>

namespace vna_native {
namespace {

// 将原始频率位混合成确定性因子，使高 32 位变化也能影响输出。
// 这只是测试数据的扰动，不需要已确认的频率单位，也不是物理频响计算。
double FrequencyFactor(std::uint64_t frequencyRaw)
{
    std::uint64_t mixed = frequencyRaw;
    // uint64 乘法按模 2^64 回绕，这是混合算法的预期行为。
    mixed = (mixed ^ (mixed >> 33U)) * UINT64_C(0xff51afd7ed558ccd);
    mixed = (mixed ^ (mixed >> 33U)) * UINT64_C(0xc4ceb9fe1a85ec53);
    mixed ^= mixed >> 33U;
    // 取高 53 位后除以 2^53，得到 [0,1)；该整数范围可由 double 精确表示。
    // 截位后不同频率可能得到相同因子，因此它不能用作唯一频率标识。
    return static_cast<double>(mixed >> 11U) / 9007199254740992.0;
}

bool CheckedOutputValueCount(std::size_t pointCount, std::size_t* valueCount)
{
    if (valueCount == NULL || pointCount == 0U) {
        return false;
    }
    // 分别检查 N * 16 的元素数及随后乘 sizeof(double) 的字节数，避免两级溢出。
    const std::size_t maxSize = (std::numeric_limits<std::size_t>::max)();
    if (pointCount > maxSize / static_cast<std::size_t>(kIqValuesPerPoint)) {
        return false;
    }
    const std::size_t count = pointCount * static_cast<std::size_t>(kIqValuesPerPoint);
    if (count > maxSize / sizeof(double)) {
        return false;
    }
    *valueCount = count;
    return true;
}

}  // namespace

std::uint32_t SignalModel::Generate(const NormalFrameSnapshot& frame, std::vector<double>* output)
{
    if (output == NULL || frame.points.empty()) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    if (frame.sourcePort >= kNativePortCount) {
        return DRV_SIM_INVALID_ARGUMENT;
    }
    // Generate 也检查源端口一致性，避免直接构造快照的调用方绕过解码约束。
    for (std::size_t pointIndex = 0U; pointIndex < frame.points.size(); ++pointIndex) {
        if (frame.points[pointIndex].sourcePort != frame.sourcePort) {
            return DRV_SIM_INVALID_ARGUMENT;
        }
    }

    std::size_t outputValueCount = 0U;
    if (!CheckedOutputValueCount(frame.points.size(), &outputValueCount)) {
        return DRV_SIM_SIZE_OVERFLOW;
    }

    try {
        // 先在独立缓冲区生成全部结果，分配失败或中途异常不会覆盖已有 output。
        std::vector<double> candidate(outputValueCount, 0.0);
        // ABI 端口索引为 0~3；公式使用标签 1~4，使不同端口产生可辨认的非零系数。
        const double sourceLabel = static_cast<double>(frame.sourcePort + 1U);

        for (std::size_t pointIndex = 0U; pointIndex < frame.points.size(); ++pointIndex) {
            const double frequencyFactor = FrequencyFactor(frame.points[pointIndex].frequencyRaw);
            // A_source 是源端口的入射波，所有接收端口的 B 都以它为激励。
            const double sourceAReal = 1.0 + 0.1 * sourceLabel + 0.01 * sourceLabel;
            const double sourceAImag = 0.001 * frequencyFactor;
            const std::size_t pointBase = pointIndex * static_cast<std::size_t>(kIqValuesPerPoint);

            for (std::uint32_t receiverIndex = 0U; receiverIndex < kNativePortCount; ++receiverIndex) {
                const double receiverLabel = static_cast<double>(receiverIndex + 1U);
                const double aReal = 1.0 + 0.1 * receiverLabel + 0.01 * sourceLabel;
                const double aImag = 0.001 * frequencyFactor;
                // H 的对角项模拟反射，非对角项模拟传输；频率因子只提供轻微扰动。
                const double hReal = (receiverIndex == frame.sourcePort ? 0.20 : 0.02 * receiverLabel * sourceLabel) +
                                     0.001 * frequencyFactor;
                const double hImag = 0.005 * (receiverLabel - sourceLabel);
                // 复数乘法 B_receiver = H_receiver,source * A_source，不能改用当前接收端口的 A。
                const double bReal = hReal * sourceAReal - hImag * sourceAImag;
                const double bImag = hReal * sourceAImag + hImag * sourceAReal;

                // 固定 RI 顺序：每点四个端口，每端口连续写 A.re、A.im、B.re、B.im。
                const std::size_t receiverBase = pointBase + static_cast<std::size_t>(receiverIndex) * 4U;
                candidate[receiverBase] = aReal;
                candidate[receiverBase + 1U] = aImag;
                candidate[receiverBase + 2U] = bReal;
                candidate[receiverBase + 3U] = bImag;
            }
        }

        // 全部点生成完成后统一提交；此交换不承担线程同步。
        output->swap(candidate);
        return DRV_SIM_OK;
    } catch (...) {
        return DRV_SIM_INTERNAL_ERROR;
    }
}

}  // namespace vna_native
