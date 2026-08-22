#include <cstddef>
#include <type_traits>

#include <gtest/gtest.h>

#include "VnaDriverSdk/VnaDriverApi.h"

namespace {

TEST(DriverAbiLayoutTest, UsesStableStandardLayoutTypes)
{
    // 动态库边界只允许标准布局和可平凡复制的数据，不能依赖 C++ 对象模型细节。
    static_assert(std::is_standard_layout<VnaStatusV1>::value, "status must be standard layout");
    static_assert(std::is_standard_layout<VnaDriverApiV1>::value, "driver API must be standard layout");
    static_assert(std::is_trivially_copyable<VnaComplex64V1>::value, "wave values must be trivially copyable");
    EXPECT_EQ(sizeof(VnaComplex64V1), 16U);
    EXPECT_EQ(alignof(VnaComplex64V1), alignof(double));
    // 除总尺寸外同时固定关键字段偏移，防止插入字段导致旧插件误读后续成员。
    EXPECT_EQ(offsetof(VnaStringViewV1, size), sizeof(const char *));
    EXPECT_EQ(offsetof(VnaDriverApiV1, metadata), 16U);
    EXPECT_EQ(sizeof(VnaDriverApiV1), 144U);
}

}  // namespace
