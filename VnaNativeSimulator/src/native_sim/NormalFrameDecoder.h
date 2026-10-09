#ifndef VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H
#define VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H

#include <cstdint>
#include <vector>

#include "vna_native/NativeApiContract.h"

namespace vna_native {

struct VNA_NATIVE_HIDDEN NormalFramePoint {
    std::uint64_t frequencyRaw;
    std::uint32_t sourcePort;
    std::uint32_t ifBandwidthRaw;

    NormalFramePoint();
};

struct VNA_NATIVE_HIDDEN NormalFrameSnapshot {
    std::uint32_t sourcePort;
    std::vector<NormalFramePoint> points;
    std::vector<double> tableData;

    NormalFrameSnapshot();
    void Swap(NormalFrameSnapshot& other) noexcept;
};

class VNA_NATIVE_HIDDEN NormalFrameDecoder {
public:
    // 只检查指针和声明长度，不读取调用方数组。
    static std::uint32_t ValidateInputShape(std::uint32_t pointCount, const std::uint32_t* paramData,
        std::uint32_t paramDataLen, const double* tableData, std::uint32_t tableDataLen);

    // 成功时一次性替换 output；失败时 output 保持不变。
    static std::uint32_t Decode(std::uint32_t pointCount, const std::uint32_t* paramData, std::uint32_t paramDataLen,
        const double* tableData, std::uint32_t tableDataLen, NormalFrameSnapshot* output);
};

}  // namespace vna_native

#endif  // VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H
