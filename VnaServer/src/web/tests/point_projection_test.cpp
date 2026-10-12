/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cmath>
#include <exception>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>

#include "web/display_json.h"

namespace {

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const VnaWeb::Json &Field(const VnaWeb::Json &value, const std::string &field)
{
    const auto item = value.Find(field);
    Require(item != nullptr, "投影缺少字段：" + field);
    return *item;
}

VnaMeasurement::TraceResult Trace()
{
    VnaMeasurement::TraceResult trace;
    trace.available = true;
    trace.points = {{0.0, 0.0, true}, {-1.0, 0.0, true}, {0.0, 1.0, true}, {1.0, 0.0, true}, {0.5, 0.0, false},
        {std::numeric_limits<double>::quiet_NaN(), 0.0, true}, {1.0, 1e-310, true}};
    return trace;
}

void SmithPointReadoutsFollowTransportIndexes()
{
    VnaDisplay::TracePresentation presentation;
    presentation.format = VnaDisplay::DisplayFormat::SMITH;
    const auto projected = VnaDisplay::TraceProjection::Calculate(presentation, Trace(), {1, 2, 3, 4, 5, 6, 7});
    const auto json = VnaWeb::DisplayJson::Projection(projected, presentation.format, {2U, 3U, 4U, 0U, 5U, 6U});
    const auto &real = Field(json, "impedanceRealValues").array;
    const auto &imag = Field(json, "impedanceImagValues").array;
    const auto &states = Field(json, "impedanceStates").array;
    Require(real.size() == 6U && imag.size() == real.size() && states.size() == real.size(),
        "阻抗数组必须与传输抽点的投影数组同长");
    Require(states[0].string == "finite" && std::abs(real[0].number) < 1e-12 &&
                std::abs(imag[0].number - 1.0) < 1e-12 && real[3].number == 1.0 && imag[3].number == 0.0,
        "按原始下标抽点，不能把阻抗错配到另一频点");
    Require(states[1].string == "open" && real[1].type == VnaWeb::Json::Type::NULL_VALUE &&
                imag[1].type == VnaWeb::Json::Type::NULL_VALUE,
        "开路必须保留显式状态，禁止以有限值替代");
    Require(states[2].string == "invalid" && states[4].string == "invalid" &&
                real[2].type == VnaWeb::Json::Type::NULL_VALUE && imag[4].type == VnaWeb::Json::Type::NULL_VALUE,
        "无效采样与非有限输入不能伪造有效阻抗");
    Require(states[5].string == "overflow" && real[5].type == VnaWeb::Json::Type::NULL_VALUE &&
                imag[5].type == VnaWeb::Json::Type::NULL_VALUE,
        "极接近开路的溢出阻抗以显式状态下发，禁止输出非标准 JSON 数值");
    Require(!Field(Field(json, "marker"), "available").boolean, "只读逐点投影不能开启或移动正式 Marker");
}

void NonSmithAndUnavailableTraceHaveNoPointImpedance()
{
    for (const auto format : {VnaDisplay::DisplayFormat::DB, VnaDisplay::DisplayFormat::REALIMAG}) {
        VnaDisplay::TracePresentation presentation;
        presentation.format = format;
        const auto projected = VnaDisplay::TraceProjection::Calculate(presentation, Trace(), {1, 2, 3, 4, 5, 6, 7});
        const auto json = VnaWeb::DisplayJson::Projection(projected, format, {0U, 2U});
        Require(Field(json, "impedanceRealValues").array.empty() && Field(json, "impedanceImagValues").array.empty() &&
                    Field(json, "impedanceStates").array.empty(),
            "非 Smith 格式不能额外计算或传输逐点阻抗");
    }
    const auto json = VnaWeb::DisplayJson::Projection({}, VnaDisplay::DisplayFormat::SMITH, {0U, 2U});
    Require(Field(json, "impedanceStates").array.size() == 2U &&
                Field(json, "impedanceStates").array[0].string == "invalid" &&
                Field(json, "impedanceRealValues").array[0].type == VnaWeb::Json::Type::NULL_VALUE,
        "不可用的 Smith 数据保留同长的无效占位，不冒用旧帧");
}

}  // 匿名命名空间

int main()
{
    try {
        SmithPointReadoutsFollowTransportIndexes();
        NonSmithAndUnavailableTraceHaveNoPointImpedance();
        std::cout << "PASS 同帧逐点阻抗、抽点映射、特殊状态与只读语义\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
