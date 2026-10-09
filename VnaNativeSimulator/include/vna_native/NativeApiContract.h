#ifndef VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H
#define VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H

#include <cstdint>

// 共用的 native 接口合同：让运行时加载器和模拟器使用相同的九个入口名称与函数签名。
// 这里集中声明 ABI；模拟器的行为和错误码不能据此推定为厂商驱动的完整合同。
// 本头文件使用 C++ 类型，extern "C" 只约束导出函数的链接名称。
// EXPORT 公开入口，HIDDEN 隐藏内部实现；CALL 预留调用约定，目前未指定额外约定。
// 当前开发与验收覆盖 Linux/macOS；Windows 宏分支不代表已验证厂商的调用约定。
#if defined(_WIN32)
#if defined(VNA_NATIVE_BUILD_SHARED)
#define VNA_NATIVE_EXPORT __declspec(dllexport)
#else
#define VNA_NATIVE_EXPORT
#endif
#define VNA_NATIVE_HIDDEN
#define VNA_NATIVE_CALL
#elif defined(__GNUC__) || defined(__clang__)
#define VNA_NATIVE_EXPORT __attribute__((visibility("default")))
#define VNA_NATIVE_HIDDEN __attribute__((visibility("hidden")))
#define VNA_NATIVE_CALL
#else
#define VNA_NATIVE_EXPORT
#define VNA_NATIVE_HIDDEN
#define VNA_NATIVE_CALL
#endif

namespace vna_native {

// 以下数值仅由模拟器定义；真实驱动返回值应按厂商文档解释。
enum DrvSimErrorCode : std::uint32_t {
    DRV_SIM_OK = 0U,
    DRV_SIM_INVALID_ARGUMENT = 1U,
    DRV_SIM_NOT_INITIALIZED = 2U,
    DRV_SIM_RF_DISABLED = 3U,
    DRV_SIM_INVALID_STATE = 4U,
    DRV_SIM_NOT_SUPPORTED = 5U,
    DRV_SIM_SIZE_OVERFLOW = 6U,
    DRV_SIM_BUFFER_TOO_SMALL = 7U,
    DRV_SIM_INTERNAL_ERROR = 8U
};

// 四个端口在 native 接口中编号为 0..3，位图的低四位分别代表这些端口。
// 下载帧每点含 6 个 uint32 参数和 9 个 double；参数帧另有一个 quick 头字段。
// IQ 输出每点占 16 个 double；所有长度单位均为元素个数，而非字节数。
enum : std::uint32_t {
    kNativePortCount = 4U,
    kParamValuesPerPoint = 6U,
    kTableValuesPerPoint = 9U,
    kIqValuesPerPoint = 16U
};

// 加载器按这些字符串查找九个符号；extern "C" 避免 C++ 名称改编。
static constexpr const char kDrvHwInitSymbol[] = "DrvHwInit";
static constexpr const char kDrvGetSweepDataSymbol[] = "DrvGetSweepData";
static constexpr const char kDrvStartSweepSymbol[] = "DrvStartSweep";
static constexpr const char kDrvActivateRxLinkSymbol[] = "DrvActivateRxLink";
static constexpr const char kDrvSetRfPowerOnSymbol[] = "DrvSetRfPowerOn";
static constexpr const char kDrvSetFastSweepModeSymbol[] = "DrvSetFastSweepMode";
static constexpr const char kDrvGetIDNSymbol[] = "DrvGetIDN";
static constexpr const char kDrvCheckSweepPortPowerSymbol[] = "DrvCheckSweepPortPower";
static constexpr const char kDrvDataFrameDownloadSymbol[] = "DrvDataFrameDownload";

// typedef 用于将动态加载的符号转换为可调用的函数指针。
// 导出实现中的 static_assert 再校验声明与指针类型一致；它不验证运行时库的真实行为。
typedef std::uint32_t(VNA_NATIVE_CALL* DrvHwInitFn)();
typedef std::uint32_t(VNA_NATIVE_CALL* DrvGetSweepDataFn)(std::uint32_t, double*);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvStartSweepFn)(std::uint32_t, std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvActivateRxLinkFn)(std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvSetRfPowerOnFn)(std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvSetFastSweepModeFn)(std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvGetIDNFn)(char*, int);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvCheckSweepPortPowerFn)(
    std::uint32_t, std::uint32_t*, std::uint16_t*, std::uint32_t);
typedef std::uint32_t(VNA_NATIVE_CALL* DrvDataFrameDownloadFn)(
    std::uint32_t, std::uint32_t*, std::uint32_t, double*, std::uint32_t);

}  // namespace vna_native

extern "C" {

// 重置整个模拟器：清除已下载帧和待取结果，并关闭 RF、清除接收链路及模式配置。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvHwInit();
// 读取并消费一次待取结果；pointCount 必须与上次成功启动的扫描一致。
// iqData 必须容纳 16 * pointCount 个 double；此 ABI 没有容量参数，调用方负责分配。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetSweepData(std::uint32_t pointCount, double* iqData);
// 从指定源端口（0..3）的下载帧同步生成待取 IQ；初始化、RF、接收链路和模式须已配置。
// 帧点数须匹配 pointCount，已有待取结果时拒绝新扫描，直到读取或关闭 RF。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvStartSweep(std::uint32_t pointCount, std::uint32_t sourcePort);
// 配置非零接收端口位图（1..0x0f）；当前信号模型仍生成完整的固定 IQ 布局。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvActivateRxLink(std::uint32_t rxPortBitmap);
// enabled 只接受 0/1；关闭 RF 会丢弃待取结果，已下载帧保留。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetRfPowerOn(std::uint32_t enabled);
// 当前只支持普通扫描 mode == 0；仍需显式调用以完成扫描前的模式配置。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvSetFastSweepMode(std::uint32_t mode);
// buffer 的 capacity 单位为 char，须大于 0；容量不足时返回截断字符串并以 NUL 结尾。
// 截断返回 DRV_SIM_BUFFER_TOO_SMALL；完整输出需要同时容纳字符串和末尾 NUL。
// 初始化后返回标识 VnaPro,NativeSimulator,4PORT,1.0。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvGetIDN(char* buffer, int capacity);
// 功率检查尚未模拟：通过初始化与参数校验后，清零有效输出并返回 NOT_SUPPORTED。
// checkBitmap 指向一个 uint32；overFreqIndex 容纳 indexCapacity 个 uint16，容量为 0 时可为空。
// 清零用于提供确定的输出，不能将 NOT_SUPPORTED 当作功率检查通过。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvCheckSweepPortPower(
    std::uint32_t rfPortBitmap, std::uint32_t* checkBitmap, std::uint16_t* overFreqIndex, std::uint32_t indexCapacity);
// 下载普通帧并替换对应源端口的帧；N == pointCount 时，长度须恰为 1 + 6N 和 9N。
// paramDataLen/tableDataLen 分别是 uint32/double 的元素数；源端口在每点参数的第三项。
// 各点源端口须相同且在 0..3；quick、dynamicLink、dynamicEnabled 仅支持 0。
// 指针保持 ABI 原有的非 const 签名；内部解码只读输入，并保存独立副本。
// 两个裸指针的实际可读容量仍由调用方保证，传入长度不能证明底层内存足够。
VNA_NATIVE_EXPORT std::uint32_t VNA_NATIVE_CALL DrvDataFrameDownload(std::uint32_t pointCount, std::uint32_t* paramData,
    std::uint32_t paramDataLen, double* tableData, std::uint32_t tableDataLen);

}  // extern "C"

#endif  // VNAPRO_VNA_NATIVE_NATIVE_API_CONTRACT_H
