#ifndef VNA_DRIVER_SDK_VNA_DRIVER_API_H
#define VNA_DRIVER_SDK_VNA_DRIVER_API_H

#include "VnaDriverSdk/VnaDriverAbi.h"
#include "VnaDriverSdk/VnaDriverExport.h"

/* descriptor 及其中字符串仅在回调期间有效，宿主负责立即复制。 */
typedef void (*VnaDeviceFoundCallbackV1)(const VnaDeviceDescriptorViewV1 *, void *);

/*
 * 缓冲区服务只能由当前设备的 Execute 调用链单线程串行调用；Execute 返回前必须结束全部
 * Acquire/Commit/Discard，返回后不得再使用该 SweepId。Acquire 成功后，驱动必须且只能
 * Commit 或 Discard 一次；Commit 后驱动立即失去写权限，缓冲区由宿主在上层释放波块时归还。
 */
typedef struct VnaDriverHostApiV1 {
    uint32_t structSize;
    uint32_t abiMajor;
    uint32_t abiMinor;
    uint32_t reserved;
    /* 日志回调不得保留 message.data。 */
    void (*Log)(uint32_t level, VnaStringViewV1 message);
    /* 未注册的 SweepId 按已取消处理，驱动应尽快退出 Execute。 */
    uint8_t (*IsCancellationRequested)(VnaSweepIdV1 sweepId);
    /* Acquire 成功后 writableView 只归当前驱动线程写入。 */
    VnaStatusV1 (*AcquireWaveBlock)(
        VnaSweepIdV1 sweepId, uint32_t layoutId, uint32_t pointCapacity, VnaBufferHandle *handle,
        VnaMutableWaveBlockViewV1 *writableView);
    /* Commit 只提交实际有效的点区间；成功后句柄失效。 */
    VnaStatusV1 (*CommitWaveBlock)(VnaBufferHandle handle, uint32_t firstPointIndex, uint32_t pointCount);
    /* 放弃尚未提交的句柄；函数允许用于错误清理路径。 */
    void (*DiscardWaveBlock)(VnaBufferHandle handle);
} VnaDriverHostApiV1;

/*
 * 驱动拥有 Create 返回的模块句柄和 CreateDevice 返回的设备句柄；宿主保证成对调用销毁函数。
 * 同一模块的 Discover/CreateDevice 由宿主串行化；除 RequestAbort 外，同一设备的状态命令
 * 均由设备 Actor 串行化。RequestAbort 是唯一可在 Execute 交接前或与其并发的例外。
 */
typedef struct VnaDriverApiV1 {
    uint32_t structSize;
    uint32_t abiMajor;
    uint32_t abiMinor;
    uint32_t reserved;
    VnaDriverMetadataV1 metadata;
    /* 模块级生命周期：Create/Destroy 必须严格成对。 */
    VnaStatusV1 (*Create)(const VnaDriverCreateInfoV1 *, VnaDriverHandle *);
    void (*Destroy)(VnaDriverHandle);
    /* Discover 可以回调零到多个设备，但回调视图不得在返回后继续使用。 */
    VnaStatusV1 (*Discover)(VnaDriverHandle, VnaDeviceFoundCallbackV1, void *);
    /* 设备句柄由模块创建，在模块销毁前必须先 DestroyDevice。 */
    VnaStatusV1 (*CreateDevice)(VnaDriverHandle, VnaStringViewV1, VnaDeviceHandle *);
    void (*DestroyDevice)(VnaDeviceHandle);
    /* 同一设备的连接、准备、执行与断开由宿主状态机串行调用。 */
    VnaStatusV1 (*Connect)(VnaDeviceHandle);
    VnaStatusV1 (*Disconnect)(VnaDeviceHandle);
    VnaStatusV1 (*GetCapabilities)(VnaDeviceHandle, VnaCapabilitiesViewV1 *);
    /* Prepare 只能编译配置，不得开始采集或异步保存宿主计划指针。 */
    VnaStatusV1 (*Prepare)(VnaDeviceHandle, const VnaSweepPlanViewV1 *, VnaPrepareResultViewV1 *);
    /* Execute 是允许阻塞的采集入口；返回构成该 SweepId 全部 HostApi 缓冲调用的完成屏障。 */
    VnaStatusV1 (*Execute)(VnaDeviceHandle, VnaSweepIdV1, const VnaExecutionContextV1 *);
    /*
     * 未声明 VNA_CAPABILITY_ABORT 时可以为空；声明后必须实现。它可与 Execute 并发，
     * 调用可发生在 Start 完成后、Execute 真正进入前，此时允许返回 INVALID_STATE；宿主
     * 已先设置 IsCancellationRequested，并会等待 Execute 实际终态再完成中止命令。并发时驱动
     * 必须自行同步；宿主保证同一设备的多个 RequestAbort 串行。返回 OK 表示已接受
     * 请求；若没有断线、超时或数据通路等更具体故障，这一 Execute 必须以
     * VNA_STATUS_CANCELLED 结束。若扫频已越过终态线性化点，则返回
     * VNA_STATUS_INVALID_STATE，不得事后虚报接受。
     */
    VnaStatusV1 (*RequestAbort)(VnaDeviceHandle, VnaSweepIdV1);
} VnaDriverApiV1;

typedef VnaStatusV1 (*VnaDriverGetApiFunctionV1)(uint32_t, const VnaDriverHostApiV1 *, VnaDriverApiV1 *);

/* 动态库只导出这一入口；版本协商失败时不得部分初始化 driverApi。 */
#if defined(__cplusplus) && defined(_WIN32)
extern "C" __declspec(dllexport) VnaStatusV1
VnaDriverGetApi(uint32_t requestedVersion, const VnaDriverHostApiV1 *hostApi, VnaDriverApiV1 *driverApi);
#elif defined(__cplusplus)
extern "C" __attribute__((visibility("default"))) VnaStatusV1
VnaDriverGetApi(uint32_t requestedVersion, const VnaDriverHostApiV1 *hostApi, VnaDriverApiV1 *driverApi);
#elif defined(_WIN32)
__declspec(dllexport) VnaStatusV1
VnaDriverGetApi(uint32_t requestedVersion, const VnaDriverHostApiV1 *hostApi, VnaDriverApiV1 *driverApi);
#else
__attribute__((visibility("default"))) VnaStatusV1
VnaDriverGetApi(uint32_t requestedVersion, const VnaDriverHostApiV1 *hostApi, VnaDriverApiV1 *driverApi);
#endif

#endif
