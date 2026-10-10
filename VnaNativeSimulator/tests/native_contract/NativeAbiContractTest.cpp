#include <algorithm>
#include <cstring>
#include <iostream>
#include <limits>
#include <string>
#include <vector>

#include "NativeContractTestSupport.h"
#include "SimulatorEnvironment.h"

namespace vna_test {
namespace {

const char kExpectedIdn[] = "VnaPro,NativeSimulator,4PORT,1.0";

void RunUninitializedTests(TestRunner* runner, NativeLibraryProbe* library)
{
    const NativeFunctions& functions = library->Functions();

    runner->Run("未初始化时八个有状态函数返回 NOT_INITIALIZED", [&functions]() {
        FrameInput frame = MakeNormalFrame(1U, 0U, UINT64_C(1000000000));
        double iq[16] = {0.0};
        char idn[64] = "unchanged";
        char shortIdn[1] = {'#'};
        std::uint32_t checkBitmap = 0xA5A5A5A5U;
        std::uint16_t overFrequency[2] = {17U, 29U};

        VNA_EXPECT_EQ(kNotInitialized, functions.getSweepData(1U, iq));
        VNA_EXPECT_EQ(kNotInitialized, functions.startSweep(1U, 0U));
        VNA_EXPECT_EQ(kNotInitialized, functions.activateRxLink(1U));
        VNA_EXPECT_EQ(kNotInitialized, functions.setRfPowerOn(1U));
        VNA_EXPECT_EQ(kNotInitialized, functions.setFastSweepMode(9U));
        VNA_EXPECT_EQ(kNotInitialized, functions.getIDN(idn, static_cast<int>(sizeof(idn))));
        VNA_EXPECT_EQ(kNotInitialized, functions.getIDN(shortIdn, 1));
        VNA_EXPECT_EQ(kNotInitialized, functions.checkSweepPortPower(1U, &checkBitmap, overFrequency, 2U));
        VNA_EXPECT_EQ(kNotInitialized, functions.checkSweepPortPower(0U, &checkBitmap, overFrequency, 2U));
        VNA_EXPECT_EQ(kNotInitialized, DownloadFrame(functions, &frame));

        VNA_EXPECT_EQ(std::string("unchanged"), std::string(idn));
        VNA_EXPECT_EQ('#', shortIdn[0]);
        VNA_EXPECT_EQ(0xA5A5A5A5U, checkBitmap);
        VNA_EXPECT_EQ(17U, overFrequency[0]);
        VNA_EXPECT_EQ(29U, overFrequency[1]);
    });

    runner->Run("内存安全检查优先于初始化状态", [&functions]() {
        std::uint32_t parameter = 0U;
        double table = 0.0;
        std::uint16_t index = 99U;

        VNA_EXPECT_EQ(kInvalidArgument, functions.getSweepData(1U, NULL));
        VNA_EXPECT_EQ(kInvalidArgument, functions.getIDN(NULL, 8));
        VNA_EXPECT_EQ(kInvalidArgument, functions.getIDN(NULL, 0));
        VNA_EXPECT_EQ(kInvalidArgument, functions.getIDN(NULL, -1));
        VNA_EXPECT_EQ(kInvalidArgument, functions.dataFrameDownload(1U, NULL, 7U, &table, 9U));
        VNA_EXPECT_EQ(kInvalidArgument, functions.dataFrameDownload(1U, &parameter, 7U, NULL, 9U));
        VNA_EXPECT_EQ(kInvalidArgument, functions.checkSweepPortPower(1U, NULL, &index, 1U));
        VNA_EXPECT_EQ(kInvalidArgument, functions.checkSweepPortPower(1U, &parameter, NULL, 1U));
        VNA_EXPECT_EQ(kInvalidArgument, functions.startSweep(0U, 99U));
        VNA_EXPECT_EQ(kNotInitialized, functions.startSweep(1U, 99U));
    });
}

void RunIdnTests(TestRunner* runner, NativeLibraryProbe* library)
{
    const NativeFunctions& functions = library->Functions();
    runner->Run("IDN 正常、截断和参数边界", [&functions]() {
        VNA_EXPECT_EQ(kOk, functions.hwInit());
        const std::size_t textLength = std::strlen(kExpectedIdn);

        std::vector<char> exact(textLength + 1U, '?');
        VNA_EXPECT_EQ(kOk, functions.getIDN(exact.data(), static_cast<int>(exact.size())));
        VNA_EXPECT_EQ(std::string(kExpectedIdn), std::string(exact.data()));
        VNA_EXPECT_EQ('\0', exact[textLength]);

        char oneByte[1] = {'?'};
        VNA_EXPECT_EQ(kBufferTooSmall, functions.getIDN(oneByte, 1));
        VNA_EXPECT_EQ('\0', oneByte[0]);

        std::vector<char> missingTerminator(textLength, '?');
        VNA_EXPECT_EQ(
            kBufferTooSmall, functions.getIDN(missingTerminator.data(), static_cast<int>(missingTerminator.size())));
        VNA_EXPECT_EQ('\0', missingTerminator[textLength - 1U]);
        VNA_EXPECT_EQ(std::string(kExpectedIdn, textLength - 1U), std::string(missingTerminator.data()));

        char untouched = '#';
        VNA_EXPECT_EQ(kInvalidArgument, functions.getIDN(&untouched, 0));
        VNA_EXPECT_EQ('#', untouched);
        VNA_EXPECT_EQ(kInvalidArgument, functions.getIDN(&untouched, -1));
        VNA_EXPECT_EQ('#', untouched);
        VNA_EXPECT_EQ(kInvalidArgument, functions.getIDN(NULL, 1));
    });
}

void RunPowerCheckTests(TestRunner* runner, NativeLibraryProbe* library)
{
    const NativeFunctions& functions = library->Functions();
    runner->Run("功率检查清零有效输出并返回 NOT_SUPPORTED", [&functions]() {
        VNA_EXPECT_EQ(kOk, functions.hwInit());
        std::uint32_t checkBitmap = 0xFFFFFFFFU;
        std::uint16_t indexes[3] = {11U, 22U, 33U};

        VNA_EXPECT_EQ(kNotSupported, functions.checkSweepPortPower(0xFU, &checkBitmap, indexes, 3U));
        VNA_EXPECT_EQ(0U, checkBitmap);
        VNA_EXPECT_EQ(0U, indexes[0]);
        VNA_EXPECT_EQ(0U, indexes[1]);
        VNA_EXPECT_EQ(0U, indexes[2]);

        checkBitmap = 7U;
        VNA_EXPECT_EQ(kNotSupported, functions.checkSweepPortPower(1U, &checkBitmap, NULL, 0U));
        VNA_EXPECT_EQ(0U, checkBitmap);
    });

    runner->Run("功率检查拒绝非法位图和容量组合且不写输出", [&functions]() {
        VNA_EXPECT_EQ(kOk, functions.hwInit());
        std::uint32_t checkBitmap = 0x12345678U;
        std::uint16_t index = 77U;

        VNA_EXPECT_EQ(kInvalidArgument, functions.checkSweepPortPower(0U, &checkBitmap, &index, 1U));
        VNA_EXPECT_EQ(0x12345678U, checkBitmap);
        VNA_EXPECT_EQ(77U, index);
        VNA_EXPECT_EQ(kInvalidArgument, functions.checkSweepPortPower(0x10U, &checkBitmap, &index, 1U));
        VNA_EXPECT_EQ(0x12345678U, checkBitmap);
        VNA_EXPECT_EQ(77U, index);
        VNA_EXPECT_EQ(kInvalidArgument, functions.checkSweepPortPower(1U, &checkBitmap, NULL, 1U));
        VNA_EXPECT_EQ(0x12345678U, checkBitmap);
    });
}

}  // namespace
}  // namespace vna_test

int main(int argc, char** argv)
{
    vna_test::ScopedSimulatorEnvironment environment(NULL, NULL);
    if (argc != 2) {
        std::cerr << "用法：" << argv[0] << " /absolute/path/libvnb_drv_sim\n";
        return 2;
    }

    vna_test::TestRunner runner;
    vna_test::NativeLibraryProbe library;
    std::string openError;
    runner.Run("拒绝依赖当前工作目录的动态库路径", [&library, &openError]() {
        VNA_EXPECT_FALSE(library.Open("libvnb_drv_sim", &openError));
        VNA_EXPECT_FALSE(library.IsOpen());
#if defined(_WIN32)
        VNA_EXPECT_FALSE(library.Open("C:libvnb_drv_sim.dll", &openError));
        VNA_EXPECT_FALSE(library.Open("\\libvnb_drv_sim.dll", &openError));
        VNA_EXPECT_FALSE(library.Open("/libvnb_drv_sim.dll", &openError));
#endif
    });
    runner.Run("按绝对路径打开模拟动态库", [&library, &openError, argv]() {
        VNA_EXPECT_TRUE(library.Open(argv[1], &openError));
        VNA_EXPECT_TRUE(library.IsOpen());
    });
    if (!library.IsOpen()) {
        std::cerr << "  " << openError << '\n';
        return runner.Finish();
    }

    std::vector<std::string> missingSymbols;
    std::string resolveError;
    runner.Run("动态解析全部九个原生符号", [&library, &missingSymbols, &resolveError]() {
        VNA_EXPECT_TRUE(library.ResolveAll(&missingSymbols, &resolveError));
        VNA_EXPECT_TRUE(missingSymbols.empty());
    });
    if (!missingSymbols.empty()) {
        std::cerr << "  " << resolveError << '\n';
        return runner.Finish();
    }

    vna_test::RunUninitializedTests(&runner, &library);
    vna_test::RunIdnTests(&runner, &library);
    vna_test::RunPowerCheckTests(&runner, &library);
    vna_test::RunNativeNormalSweepTests(&runner, &library);
    vna_test::RunNativeNoiseModeTests(&runner, &library);
    runner.Run("关闭动态库并释放解析后的函数表", [&library]() {
        library.Close();
        VNA_EXPECT_FALSE(library.IsOpen());
    });
    return runner.Finish();
}
