#ifndef VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H
#define VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H

#include "NormalFrameDecoder.h"

#include <cstdint>
#include <vector>

namespace vna_native {

class VNA_NATIVE_HIDDEN SignalModel {
public:
    // 成功时一次性替换 output；失败时 output 保持不变。
    static std::uint32_t Generate(const NormalFrameSnapshot& frame,
                                  std::vector<double>* output);
};

}  // namespace vna_native

#endif  // VNAPRO_NATIVE_SIM_SIGNAL_MODEL_H
