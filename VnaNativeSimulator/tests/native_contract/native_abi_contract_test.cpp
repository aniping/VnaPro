/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "native_contract_test_support.h"
#include "scoped_simulator_environment.h"

namespace vna_test {
namespace {

const char g_expectedIdn[] = "VnaPro,NativeSimulator,4PORT,1.0";

void UninitializedStateOutputs(const NativeFunctions &functions)
{
    FrameInput frame = MakeNormalFrame(1U, 0U, 1000000000ULL);
    double iq[16] = {0.0};
    char idn[64] = "unchanged";
    char shortIdn[1] = {'#'};
    std::uint32_t checkBitmap = 0xA5A5A5A5U;
    std::uint16_t overFrequency[2] = {17U, 29U};

    vna_test::Expect(functions.getSweepData(1U, iq) == NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.startSweep(1U, 0U) == NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.activateRxLink(1U) == NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.setRfPowerOn(1U) == NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.setFastSweepMode(9U) == NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.getIDN(idn, static_cast<int>(sizeof(idn))) == NOT_INITIALIZED,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.getIDN(shortIdn, 1) == NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.checkSweepPortPower(1U, &checkBitmap, overFrequency, 2U) == NOT_INITIALIZED,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.checkSweepPortPower(0U, &checkBitmap, overFrequency, 2U) == NOT_INITIALIZED,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(DownloadFrame(functions, frame) == NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});

    vna_test::Expect(std::string(idn) == std::string("unchanged"), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(shortIdn[0] == '#', vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(checkBitmap == 0xA5A5A5A5U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(overFrequency[0] == 17U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(overFrequency[1] == 29U, vna_test::TestLocation{__FILE__, __LINE__});
}

void ArgumentValidationBeforeInit(const NativeFunctions &functions)
{
    std::uint32_t parameter = 0U;
    double table = 0.0;
    std::uint16_t index = 99U;

    vna_test::Expect(
        functions.getSweepData(1U, nullptr) == INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.getIDN(nullptr, 8) == INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.getIDN(nullptr, 0) == INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.getIDN(nullptr, -1) == INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.dataFrameDownload(1U, nullptr, 7U, &table, 9U) == INVALID_ARGUMENT,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.dataFrameDownload(1U, &parameter, 7U, nullptr, 9U) == INVALID_ARGUMENT,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.checkSweepPortPower(1U, nullptr, &index, 1U) == INVALID_ARGUMENT,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.checkSweepPortPower(1U, &parameter, nullptr, 1U) == INVALID_ARGUMENT,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.startSweep(0U, 99U) == INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.startSweep(1U, 99U) == NOT_INITIALIZED, vna_test::TestLocation{__FILE__, __LINE__});
}

void IdnCapacityBoundaries(const NativeFunctions &functions)
{
    vna_test::Expect(functions.hwInit() == OK, vna_test::TestLocation{__FILE__, __LINE__});
    const std::size_t textLength = std::strlen(g_expectedIdn);

    std::vector<char> exact(textLength + 1U, '?');
    vna_test::Expect(functions.getIDN(exact.data(), static_cast<int>(exact.size())) == OK,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(
        std::string(exact.data()) == std::string(g_expectedIdn), vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(exact[textLength] == '\0', vna_test::TestLocation{__FILE__, __LINE__});

    char oneByte[1] = {'?'};
    vna_test::Expect(functions.getIDN(oneByte, 1) == BUFFER_TOO_SMALL, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(oneByte[0] == '\0', vna_test::TestLocation{__FILE__, __LINE__});

    std::vector<char> missingTerminator(textLength, '?');
    vna_test::Expect(
        functions.getIDN(missingTerminator.data(), static_cast<int>(missingTerminator.size())) == BUFFER_TOO_SMALL,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(missingTerminator[textLength - 1U] == '\0', vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(std::string(missingTerminator.data()) == std::string(g_expectedIdn, textLength - 1U),
        vna_test::TestLocation{__FILE__, __LINE__});

    char untouched = '#';
    vna_test::Expect(functions.getIDN(&untouched, 0) == INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(untouched == '#', vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.getIDN(&untouched, -1) == INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(untouched == '#', vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.getIDN(nullptr, 1) == INVALID_ARGUMENT, vna_test::TestLocation{__FILE__, __LINE__});
}

void UnsupportedPowerClearsOutputs(const NativeFunctions &functions)
{
    vna_test::Expect(functions.hwInit() == OK, vna_test::TestLocation{__FILE__, __LINE__});
    std::uint32_t checkBitmap = 0xFFFFFFFFU;
    std::uint16_t indexes[3] = {11U, 22U, 33U};

    vna_test::Expect(functions.checkSweepPortPower(0xFU, &checkBitmap, indexes, 3U) == NOT_SUPPORTED,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(checkBitmap == 0U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(indexes[0] == 0U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(indexes[1] == 0U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(indexes[2] == 0U, vna_test::TestLocation{__FILE__, __LINE__});

    checkBitmap = 7U;
    vna_test::Expect(functions.checkSweepPortPower(1U, &checkBitmap, nullptr, 0U) == NOT_SUPPORTED,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(checkBitmap == 0U, vna_test::TestLocation{__FILE__, __LINE__});
}

void PowerArgumentsDoNotWrite(const NativeFunctions &functions)
{
    vna_test::Expect(functions.hwInit() == OK, vna_test::TestLocation{__FILE__, __LINE__});
    std::uint32_t checkBitmap = 0x12345678U;
    std::uint16_t index = 77U;

    vna_test::Expect(functions.checkSweepPortPower(0U, &checkBitmap, &index, 1U) == INVALID_ARGUMENT,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(checkBitmap == 0x12345678U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(index == 77U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.checkSweepPortPower(0x10U, &checkBitmap, &index, 1U) == INVALID_ARGUMENT,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(checkBitmap == 0x12345678U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(index == 77U, vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(functions.checkSweepPortPower(1U, &checkBitmap, nullptr, 1U) == INVALID_ARGUMENT,
        vna_test::TestLocation{__FILE__, __LINE__});
    vna_test::Expect(checkBitmap == 0x12345678U, vna_test::TestLocation{__FILE__, __LINE__});
}

void RunUninitializedTests(TestRunner &runner, NativeLibraryProbe &library)
{
    const NativeFunctions &functions = library.Functions();

    runner.Run("未初始化时八个有状态函数返回 NOT_INITIALIZED", [&functions]() {
        UninitializedStateOutputs(functions);
    });

    runner.Run("内存安全检查优先于初始化状态", [&functions]() {
        ArgumentValidationBeforeInit(functions);
    });
}

void RunIdnTests(TestRunner &runner, NativeLibraryProbe &library)
{
    const NativeFunctions &functions = library.Functions();
    runner.Run("IDN 正常、截断和参数边界", [&functions]() {
        IdnCapacityBoundaries(functions);
    });
}

void RunPowerCheckTests(TestRunner &runner, NativeLibraryProbe &library)
{
    const NativeFunctions &functions = library.Functions();
    runner.Run("功率检查清零有效输出并返回 NOT_SUPPORTED", [&functions]() {
        UnsupportedPowerClearsOutputs(functions);
    });

    runner.Run("功率检查拒绝非法位图和容量组合且不写输出", [&functions]() {
        PowerArgumentsDoNotWrite(functions);
    });
}

}  // namespace
}  // namespace vna_test

int main(int argc, char **argv)
{
    vna_test::ScopedSimulatorEnvironment environment(nullptr, nullptr);
    if (argc != 2) {
        std::cerr << "用法：" << argv[0] << " /absolute/path/libvnb_drv_sim\n";
        return 2;
    }

    vna_test::TestRunner runner;
    vna_test::NativeLibraryProbe library;
    std::string openError;
    runner.Run("拒绝依赖当前工作目录的动态库路径", [&library, &openError]() {
        vna_test::Expect(!(library.Open("libvnb_drv_sim", openError)), vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(!(library.IsOpen()), vna_test::TestLocation{__FILE__, __LINE__});
#if defined(_WIN32)
        vna_test::Expect(
            !(library.Open("C:libvnb_drv_sim.dll", openError)), vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(
            !(library.Open("\\libvnb_drv_sim.dll", openError)), vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(!(library.Open("/libvnb_drv_sim.dll", openError)), vna_test::TestLocation{__FILE__, __LINE__});
#endif
    });
    runner.Run("按绝对路径打开模拟动态库", [&library, &openError, argv]() {
        vna_test::Expect(library.Open(argv[1], openError), vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(library.IsOpen(), vna_test::TestLocation{__FILE__, __LINE__});
    });
    if (!library.IsOpen()) {
        std::cerr << "  " << openError << '\n';
        return runner.Finish();
    }

    std::vector<std::string> missingSymbols;
    std::string resolveError;
    runner.Run("动态解析全部九个原生符号", [&library, &missingSymbols, &resolveError]() {
        vna_test::Expect(library.ResolveAll(missingSymbols, resolveError), vna_test::TestLocation{__FILE__, __LINE__});
        vna_test::Expect(missingSymbols.empty(), vna_test::TestLocation{__FILE__, __LINE__});
    });
    if (!missingSymbols.empty()) {
        std::cerr << "  " << resolveError << '\n';
        return runner.Finish();
    }

    vna_test::RunUninitializedTests(runner, library);
    vna_test::RunIdnTests(runner, library);
    vna_test::RunPowerCheckTests(runner, library);
    vna_test::RunNativeNormalSweepTests(runner, library);
    vna_test::RunNativeNoiseModeTests(runner, library);
    runner.Run("关闭动态库并释放解析后的函数表", [&library]() {
        vna_test::Expect(library.Close(), {__FILE__, __LINE__});
        vna_test::Expect(!(library.IsOpen()), vna_test::TestLocation{__FILE__, __LINE__});
    });
    return runner.Finish();
}
