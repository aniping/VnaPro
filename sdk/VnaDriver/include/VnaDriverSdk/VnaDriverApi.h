#ifndef VNA_DRIVER_SDK_VNA_DRIVER_API_H
#define VNA_DRIVER_SDK_VNA_DRIVER_API_H

#include "VnaDriverSdk/VnaDriverAbi.h"
#include "VnaDriverSdk/VnaDriverExport.h"

/* descriptor 及其中字符串仅在回调期间有效，宿主负责立即复制。 */
typedef void (*VnaDeviceFoundCallbackV1)(const VnaDeviceDescriptorViewV1 *, void *);

/*
 * 宿主服务可从驱动执行线程调用。Acquire 成功后，驱动必须且只能 Commit 或 Discard 一次；
 * Commit 后驱动立即失去写权限，缓冲区由宿主在上层释放波块时归还。
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
 * 同一模块的 Discover/CreateDevice 由宿主串行化，同一设备的状态命令由设备 Actor 串行化。
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
    /* Execute 是允许阻塞的采集入口，数据通过 HostApi 缓冲回调流式提交。 */
    VnaStatusV1 (*Execute)(VnaDeviceHandle, VnaSweepIdV1, const VnaExecutionContextV1 *);
    /* 未声明 VNA_CAPABILITY_ABORT 时可以为空；声明后必须实现。 */
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
