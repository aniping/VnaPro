#include "SimulatorDevice.h"

#include <string>

namespace vna::simulator {

VnaStatusV1 simulatorStatus(std::uint32_t code, const char *message)
{
    // 当前消息均为静态字面量，返回视图不会引用栈内存。
    VnaStatusV1 result{};
    result.code = code;
    result.message = VnaStringViewV1{message, std::char_traits<char>::length(message)};
    return result;
}

}  // namespace vna::simulator
