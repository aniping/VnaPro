#include "VnaCheckedSize.h"

#include <limits>

namespace vna::detail {

bool checkedSizeAdd(std::size_t left, std::size_t right, std::size_t &result) noexcept
{
    if (right > (std::numeric_limits<std::size_t>::max() - left)) {
        return false;
    }
    result = left + right;
    return true;
}

bool checkedSizeMultiply(std::size_t left, std::size_t right, std::size_t &result) noexcept
{
    if ((left != 0) && (right > (std::numeric_limits<std::size_t>::max() / left))) {
        return false;
    }
    result = left * right;
    return true;
}

}  // namespace vna::detail
