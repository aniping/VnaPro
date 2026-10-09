#ifndef VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H
#define VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H

#include <cstdint>
#include <vector>

#include "vna_native/NativeApiContract.h"

namespace vna_native {

// 解码后保留协议原值；frequencyRaw 的物理单位尚未确认，不在这里按 Hz 换算。
// IFBW 也仅保存原值，当前信号模型不会据此计算带宽或噪声。
struct VNA_NATIVE_HIDDEN NormalFramePoint {
    std::uint64_t frequencyRaw;
    std::uint32_t sourcePort;
    std::uint32_t ifBandwidthRaw;

    NormalFramePoint();
};

// 拥有各点及功率表的深复制数据，调用方在下载成功后可以释放或修改输入数组。
// 一帧只允许同一源端口；tableData 每点 9 个值，布局未解释，按不透明数据保存。
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
    // 声明长度必须与帧布局精确匹配；实际数组容量与生命周期由调用方保证。
    static std::uint32_t ValidateInputShape(std::uint32_t pointCount, const std::uint32_t* paramData,
        std::uint32_t paramDataLen, const double* tableData, std::uint32_t tableDataLen);

    // 成功时一次性替换 output；失败时 output 保持不变。
    // 替换保证不会发布半帧数据；线程同步由调用层负责，并非无锁原子操作。
    static std::uint32_t Decode(std::uint32_t pointCount, const std::uint32_t* paramData, std::uint32_t paramDataLen,
        const double* tableData, std::uint32_t tableDataLen, NormalFrameSnapshot* output);
};

}  // namespace vna_native

#endif  // VNAPRO_NATIVE_SIM_NORMAL_FRAME_DECODER_H
