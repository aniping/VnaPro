/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <cstdint>
#include <cstring>
#include <iostream>
#include <type_traits>

#include "vna_board/board_api_contract.h"

// 供应头由使用方提供整数类型与导出宏；隔离包含，避免与自家导出声明互证。
// 仅在 unevaluated context 检查函数类型，不调用或链接硬件驱动。
namespace ProviderBoard {
using std::uint32_t;
using std::uint64_t;

#define EXPORT_API extern "C"
#include "../../demo/DrvDeviceBoardApi.h"
#undef EXPORT_API
}  // 命名空间 ProviderBoard

static_assert(std::is_same<decltype(&ProviderBoard::DrvSetPowerList), VnaBoard::DrvSetPowerListFn>::value,
    "供应头功率 ABI 不符：DrvSetPowerList 必须接收 double*，不得按 uint64_t* 解释");
static_assert(std::is_same<decltype(&ProviderBoard::DrvHwInit), VnaBoard::DrvHwInitFn>::value, "供应头初始化 ABI 不符");
static_assert(
    std::is_same<decltype(&ProviderBoard::DrvSetFreqList), VnaBoard::DrvSetFreqListFn>::value, "供应头频点 ABI 不符");
static_assert(
    std::is_same<decltype(&ProviderBoard::DrvSetIfBwList), VnaBoard::DrvSetIfBwListFn>::value, "供应头中频 ABI 不符");
static_assert(std::is_same<decltype(&ProviderBoard::DrvSetWorkPorts), VnaBoard::DrvSetWorkPortsFn>::value,
    "供应头工作端口 ABI 不符");
static_assert(std::is_same<decltype(&ProviderBoard::DrvDataFrameDownload), VnaBoard::DrvDataFrameDownloadFn>::value,
    "供应头组帧 ABI 不符");
static_assert(std::is_same<decltype(&ProviderBoard::DrvSetRfPowerOn), VnaBoard::DrvSetRfPowerOnFn>::value,
    "供应头射频开关 ABI 不符");
static_assert(
    std::is_same<decltype(&ProviderBoard::DrvStartSweep), VnaBoard::DrvStartSweepFn>::value, "供应头扫描启动 ABI 不符");
static_assert(std::is_same<decltype(&ProviderBoard::DrvGetSweepData), VnaBoard::DrvGetSweepDataFn>::value,
    "供应头分段数据读取 ABI 不符");

namespace {

struct SymbolContract final {
    const char *actual;
    const char *expected;
};

}  // 匿名命名空间

int main()
{
    // 期望符号使用供应头中的原始拼写，不从自家 exports 或函数表生成。
    const SymbolContract symbols[] = {
        {VnaBoard::g_drvHwInitSymbol, "DrvHwInit"},
        {VnaBoard::g_drvSetFreqListSymbol, "DrvSetFreqList"},
        {VnaBoard::g_drvSetPowerListSymbol, "DrvSetPowerList"},
        {VnaBoard::g_drvSetIfBwListSymbol, "DrvSetIfBwList"},
        {VnaBoard::g_drvSetWorkPortsSymbol, "DrvSetWorkPorts"},
        {VnaBoard::g_drvDataFrameDownloadSymbol, "DrvDataFrameDownload"},
        {VnaBoard::g_drvSetRfPowerOnSymbol, "DrvSetRfPowerOn"},
        {VnaBoard::g_drvStartSweepSymbol, "DrvStartSweep"},
        {VnaBoard::g_drvGetSweepDataSymbol, "DrvGetSweepData"},
    };
    for (const auto &symbol : symbols) {
        if (std::strcmp(symbol.actual, symbol.expected) != 0) {
            std::cerr << "供应头要求精确符号 " << symbol.expected << "，内部合同却使用 " << symbol.actual << '\n';
            return 1;
        }
    }
    std::cout << "TDC1VN10 provider header contract passed\n";
    return 0;
}
