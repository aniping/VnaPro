/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <cmath>
#include <iostream>
#include <limits>

#include "board_library_probe.h"
#include "scoped_simulator_environment.h"
#include "test_support.h"

namespace BoardTest {
namespace {

void Stage(const BoardFunctions &api, std::uint32_t pointCount, const std::vector<std::uint32_t> &workPorts)
{
    std::vector<std::uint64_t> frequencies(pointCount, 1000000000ULL);
    std::vector<std::uint64_t> powers(pointCount, std::numeric_limits<std::uint64_t>::max());
    std::vector<std::uint64_t> bandwidths(pointCount, 1000U);
    auto ports = workPorts;
    api.frequencies(frequencies.data(), pointCount);
    api.powers(powers.data(), pointCount);
    api.bandwidths(bandwidths.data(), pointCount);
    api.ports(ports.data(), static_cast<std::uint32_t>(ports.size()));
    Require(api.download() == VnaBoard::DRV_SIM_OK, "动态库组帧失败");
    Require(api.rfPower(1U) == VnaBoard::DRV_SIM_OK, "动态库射频打开失败");
}

void VerifyRanges(const std::vector<double> &full, const std::vector<double> &prefix, const std::vector<double> &suffix)
{
    for (std::size_t source = 0U; source < 4U; ++source) {
        const auto fullStart = full.begin() + static_cast<std::ptrdiff_t>(source * 201U * 16U);
        const auto prefixStart = prefix.begin() + static_cast<std::ptrdiff_t>(source * 64U * 16U);
        const auto suffixStart = suffix.begin() + static_cast<std::ptrdiff_t>(source * 137U * 16U);
        Require(std::equal(prefixStart, prefixStart + 64U * 16U, fullStart) &&
                    std::equal(suffixStart, suffixStart + 137U * 16U, fullStart + 64U * 16U),
            "动态库分段读取混淆频点与源端口排列");
    }
}

void TestRanges(const std::string &path)
{
    BoardLibraryProbe library(path);
    const auto &api = library.Functions();
    Require(api.initialize() == VnaBoard::DRV_SIM_OK, "动态库初始化失败");
    Stage(api, 201U, {3U, 0U, 1U, 2U});
    Require(api.start() == VnaBoard::DRV_SIM_OK, "动态库异步扫描失败");
    std::vector<double> full(201U * 64U, -123.0);
    std::vector<double> prefix(64U * 64U, -123.0);
    std::vector<double> suffix(137U * 64U, -123.0);
    Require(
        api.get(0U, 64U, prefix.data(), static_cast<int>(prefix.size())) == VnaBoard::DRV_SIM_OK, "动态库前段读取失败");
    Require(api.get(64U, 137U, suffix.data(), static_cast<int>(suffix.size())) == VnaBoard::DRV_SIM_OK,
        "动态库后段读取失败");
    Require(api.get(0U, 201U, full.data(), static_cast<int>(full.size())) == VnaBoard::DRV_SIM_OK,
        "动态库完整重复读取失败");
    VerifyRanges(full, prefix, suffix);
    Require(std::abs(full[3216U] - 0.7289686274214116) < 1e-14, "动态库 golden 固定值不符");
    std::vector<double> untouched(64U, -123.0);
    Require(api.get(0U, 1U, untouched.data(), 63) == VnaBoard::DRV_SIM_BUFFER_TOO_SMALL, "容量校验失败");
    Require(std::all_of(untouched.begin(), untouched.end(),
                [](double value) {
                    return value == -123.0;
                }),
        "动态库失败时修改输出");
    // 保留一个进行中的扫描，由探针析构先 RF_OFF、再卸载，验证退出路径不依赖 DLL 静态析构。
    Require(api.start() == VnaBoard::DRV_SIM_OK, "卸载验证扫描启动失败");
}

void TestLayoutLiteral(const std::string &path)
{
    BoardLibraryProbe library(path);
    const auto &api = library.Functions();
    Require(api.initialize() == VnaBoard::DRV_SIM_OK, "布局测试初始化失败");
    std::uint64_t frequencies[] = {1000000000ULL, 2000000000ULL};
    std::uint64_t powers[] = {0U, 0U};
    std::uint64_t bandwidths[] = {1000U, 1000U};
    std::uint32_t ports[] = {1U, 0U};
    api.frequencies(frequencies, 2U);
    api.powers(powers, 2U);
    api.bandwidths(bandwidths, 2U);
    api.ports(ports, 2U);
    Require(api.download() == VnaBoard::DRV_SIM_OK && api.rfPower(1U) == VnaBoard::DRV_SIM_OK &&
                api.start() == VnaBoard::DRV_SIM_OK,
        "布局测试扫描失败");
    std::vector<double> values(64U);
    Require(api.get(0U, 2U, values.data(), 64) == VnaBoard::DRV_SIM_OK, "布局测试读取失败");
    // 独立字面量验证反序工作端口和两个不同频点，不能由模拟器本身重新计算期望值。
    Require(std::abs(values[4U] - 0.6569861442007506) < 1e-14 && std::abs(values[5U] - 0.7539026504318913) < 1e-14,
        "源1首频点排列不符");
    Require(std::abs(values[20U] + 0.03715958973221235) < 1e-14 && std::abs(values[21U] - 0.999309343942572) < 1e-14,
        "源1次频点排列不符");
    Require(std::abs(values[32U] - 0.7289686274214116) < 1e-14 && std::abs(values[33U] - 0.6845471059286886) < 1e-14,
        "源0首频点排列不符");
    Require(std::abs(values[48U] - 0.06279051952931353) < 1e-14 && std::abs(values[49U] - 0.9980267284282716) < 1e-14,
        "源0次频点排列不符");
}

void TestReloadAndNoise(const std::string &path)
{
    SetEnvironment("VNAPRO_SIM_MODE", "noise");
    SetEnvironment("VNAPRO_SIM_SEED", "42");
    std::vector<double> original;
    for (std::size_t reload = 0U; reload < 4U; ++reload) {
        BoardLibraryProbe library(path);
        const auto &api = library.Functions();
        Require(api.initialize() == VnaBoard::DRV_SIM_OK, "重复加载初始化失败");
        Stage(api, 3U, {0U, 1U});
        Require(api.start() == VnaBoard::DRV_SIM_OK, "噪声扫描失败");
        std::vector<double> values(3U * 32U);
        Require(
            api.get(0U, 3U, values.data(), static_cast<int>(values.size())) == VnaBoard::DRV_SIM_OK, "噪声读取失败");
        if (reload == 0U) {
            original = values;
        } else {
            Require(values == original, "动态库重新加载不能破坏种子复现");
        }
        Require(api.start() == VnaBoard::DRV_SIM_OK, "下一帧噪声启动失败");
        Require(api.get(0U, 3U, values.data(), static_cast<int>(values.size())) == VnaBoard::DRV_SIM_OK,
            "下一帧噪声读取失败");
        Require(values != original, "动态库噪声未随扫描改变");
    }
}

}  // 匿名命名空间
}  // 命名空间 BoardTest

int main(int argc, char *argv[])
{
    try {
        BoardTest::ScopedSimulatorEnvironment environment;
        BoardTest::Require(argc == 2, "需要动态库绝对路径");
        BoardTest::SetEnvironment("VNAPRO_SIM_MODE", "golden");
        BoardTest::SetEnvironment("VNAPRO_SIM_SEED", "42");
        BoardTest::TestRanges(argv[1]);
        BoardTest::TestLayoutLiteral(argv[1]);
        BoardTest::TestReloadAndNoise(argv[1]);
        std::cout << "TDC1VN10 simulator ABI tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
