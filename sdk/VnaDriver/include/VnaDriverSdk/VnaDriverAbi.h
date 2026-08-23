#ifndef VNA_DRIVER_SDK_VNA_DRIVER_ABI_H
#define VNA_DRIVER_SDK_VNA_DRIVER_ABI_H

#include <stddef.h>
#include <stdint.h>

/*
 * V1 ABI 只使用 C99 定宽类型、显式长度和不透明句柄，避免 C++ 标准库布局跨编译器泄漏。
 * 所有 structSize 字段由调用方写入自身可见的结构大小；所有 reserved 字段必须写零。
 */
#define VNA_DRIVER_ABI_MAJOR_V1 UINT32_C(1)
#define VNA_DRIVER_ABI_MINOR_V1 UINT32_C(0)
#define VNA_DRIVER_ABI_VERSION_V1 UINT32_C(0x00010000)

/* 统一状态码只能表达跨厂商可稳定处理的分类；设备原始码写入 vendorCode。 */
#define VNA_STATUS_OK UINT32_C(0)
#define VNA_STATUS_INVALID_ARGUMENT UINT32_C(1)
#define VNA_STATUS_INVALID_STATE UINT32_C(2)
#define VNA_STATUS_UNSUPPORTED UINT32_C(3)
#define VNA_STATUS_NOT_FOUND UINT32_C(4)
#define VNA_STATUS_CONNECTION_FAILED UINT32_C(5)
#define VNA_STATUS_CONNECTION_LOST UINT32_C(6)
#define VNA_STATUS_TIMEOUT UINT32_C(7)
#define VNA_STATUS_PROTOCOL_ERROR UINT32_C(8)
#define VNA_STATUS_HARDWARE_FAULT UINT32_C(9)
#define VNA_STATUS_DATA_LOSS UINT32_C(10)
#define VNA_STATUS_CANCELLED UINT32_C(11)
#define VNA_STATUS_INTERNAL_ERROR UINT32_C(12)

/* 驱动日志级别按严重程度递增；Host 会按配置过滤，不得在逐点热路径调用。 */
#define VNA_LOG_TRACE UINT32_C(0)
#define VNA_LOG_DEBUG UINT32_C(1)
#define VNA_LOG_INFO UINT32_C(2)
#define VNA_LOG_WARNING UINT32_C(3)
#define VNA_LOG_ERROR UINT32_C(4)
#define VNA_LOG_CRITICAL UINT32_C(5)

/* 波类型使用端口参考方向：Incident 对应入射波 a，Outgoing 对应出射波 b。 */
#define VNA_WAVE_INCIDENT UINT32_C(0)
#define VNA_WAVE_OUTGOING UINT32_C(1)

/* V1 触发只区分设备内部触发与外部硬件触发。 */
#define VNA_TRIGGER_INTERNAL UINT32_C(0)
#define VNA_TRIGGER_EXTERNAL UINT32_C(1)

/* 质量值是位标志；零表示有效，非零位可以组合。 */
#define VNA_QUALITY_VALID UINT32_C(0)
#define VNA_QUALITY_OVERLOAD UINT32_C(1)
#define VNA_QUALITY_UNLOCKED UINT32_C(2)
#define VNA_QUALITY_SOURCE_UNSETTLED UINT32_C(4)
#define VNA_QUALITY_REFERENCE_LOW UINT32_C(8)
#define VNA_QUALITY_MISSING UINT32_C(16)

/* 能力位描述驱动可以接受的计划维度，不代表任意组合一定可执行。 */
#define VNA_CAPABILITY_PER_POINT_IF_BANDWIDTH UINT32_C(1)
#define VNA_CAPABILITY_PER_POINT_POWER UINT32_C(2)
#define VNA_CAPABILITY_PER_POINT_DWELL UINT32_C(4)
#define VNA_CAPABILITY_SIMULTANEOUS_SOURCES UINT32_C(8)
#define VNA_CAPABILITY_ABORT UINT32_C(16)
#define VNA_CAPABILITY_INTERNAL_TRIGGER UINT32_C(32)
#define VNA_CAPABILITY_EXTERNAL_TRIGGER UINT32_C(64)

/* 策略位表达调用方允许的退化与必须保持的测量语义。 */
#define VNA_POLICY_ALLOW_QUANTIZATION UINT32_C(1)
#define VNA_POLICY_ALLOW_MULTIPLE_SWEEPS UINT32_C(2)
#define VNA_POLICY_REQUIRE_PHASE_COHERENCE UINT32_C(4)
#define VNA_POLICY_REQUIRE_ATOMIC_CONFIGURATION UINT32_C(8)

/* Prepare 返回的执行位描述驱动实际选择的执行方式和保证。 */
#define VNA_EXECUTION_QUANTIZED UINT32_C(1)
#define VNA_EXECUTION_ATOMIC UINT32_C(2)
#define VNA_EXECUTION_PHASE_COHERENT UINT32_C(4)
#define VNA_EXECUTION_COMPILED UINT32_C(8)

/* 所有句柄只允许交还给创建它的 ABI 表，不得解引用或跨模块解释。 */
typedef void *VnaDriverHandle;
typedef void *VnaDeviceHandle;
typedef void *VnaBufferHandle;
typedef uint64_t VnaSweepIdV1;

/* 字符串不是零结尾契约；视图只在产生它的 ABI 调用期间有效。 */
typedef struct VnaStringViewV1 {
    const char *data;
    size_t size;
} VnaStringViewV1;

typedef struct VnaStatusV1 {
    uint32_t code;
    int32_t vendorCode;
    VnaStringViewV1 message;
} VnaStatusV1;

/* 波值为复数标量，具体单位与归一化必须由驱动契约另行约定。 */
typedef struct VnaComplex64V1 {
    double real;
    double imag;
} VnaComplex64V1;

typedef struct VnaWaveKeyV1 {
    uint32_t kind;
    uint16_t port;
    uint16_t reserved;
} VnaWaveKeyV1;

/* 一个源端口的功率与相位设定；phaseDegrees 必须是有限值。 */
typedef struct VnaSourcePlanV1 {
    uint16_t port;
    uint16_t reserved;
    double powerDbm;
    double phaseDegrees;
} VnaSourcePlanV1;

/* 一个激励状态同时包含源集合，以及本状态要求返回的有序波集合。 */
typedef struct VnaExcitationPlanViewV1 {
    uint16_t excitationId;
    uint16_t reserved;
    size_t sourceCount;
    const VnaSourcePlanV1 *sources;
    size_t requiredWaveCount;
    const VnaWaveKeyV1 *requiredWaves;
} VnaExcitationPlanViewV1;

/* 一个频点拥有自己的频率、IFBW、驻留时间和激励列表。 */
typedef struct VnaPointPlanViewV1 {
    uint32_t pointId;
    uint32_t reserved;
    double frequencyHz;
    double ifBandwidthHz;
    int64_t dwellNanoseconds;
    size_t excitationCount;
    const VnaExcitationPlanViewV1 *excitations;
} VnaPointPlanViewV1;

/* points 及其嵌套数组均由宿主持有，只保证在 Prepare 调用期间有效。 */
typedef struct VnaSweepPlanViewV1 {
    uint32_t structSize;
    uint32_t triggerMode;
    uint32_t policyFlags;
    uint32_t reserved;
    size_t pointCount;
    const VnaPointPlanViewV1 *points;
} VnaSweepPlanViewV1;

/* 能力值由宿主复制；驱动返回后不得再修改这些字段。 */
typedef struct VnaCapabilitiesViewV1 {
    uint32_t structSize;
    uint16_t portCount;
    uint16_t reserved;
    double minimumFrequencyHz;
    double maximumFrequencyHz;
    double minimumIfBandwidthHz;
    double maximumIfBandwidthHz;
    double minimumPowerDbm;
    double maximumPowerDbm;
    size_t maximumPoints;
    size_t maximumExcitationsPerPoint;
    size_t preferredBatchPoints;
    uint32_t featureFlags;
    uint32_t reservedFlags;
} VnaCapabilitiesViewV1;

typedef struct VnaWaveLayoutViewV1 {
    uint32_t layoutId;
    uint16_t excitationId;
    uint16_t reserved;
    size_t waveCount;
    const VnaWaveKeyV1 *waveKeys;
} VnaWaveLayoutViewV1;

/* layouts 由驱动持有；宿主会在 Prepare 返回前完成深复制和一致性校验。 */
typedef struct VnaPrepareResultViewV1 {
    uint32_t structSize;
    uint32_t physicalSweepCount;
    size_t preferredBatchPoints;
    size_t estimatedBytesPerPoint;
    uint32_t executionFlags;
    uint32_t reserved;
    size_t layoutCount;
    const VnaWaveLayoutViewV1 *layouts;
} VnaPrepareResultViewV1;

/*
 * values 与 qualityFlags 均按 [wave][point] 的波优先顺序排列。
 * pointCapacity 是步长，不等于本次 CommitWaveBlock 的实际 pointCount。
 */
typedef struct VnaMutableWaveBlockViewV1 {
    uint32_t structSize;
    uint32_t pointCapacity;
    uint32_t waveCount;
    uint32_t reserved;
    VnaComplex64V1 *values;
    uint32_t *qualityFlags;
} VnaMutableWaveBlockViewV1;

typedef struct VnaExecutionContextV1 {
    uint32_t structSize;
    uint32_t reserved;
} VnaExecutionContextV1;

/* 创建信息保留 structSize，便于后续 ABI 小版本追加字段。 */
typedef struct VnaDriverCreateInfoV1 {
    uint32_t structSize;
    uint32_t reserved;
} VnaDriverCreateInfoV1;

/* driverId 是稳定机器标识，displayName 仅用于诊断或展示。 */
typedef struct VnaDriverMetadataV1 {
    uint32_t structSize;
    uint32_t reserved;
    VnaStringViewV1 driverId;
    VnaStringViewV1 displayName;
} VnaDriverMetadataV1;

/* driverDeviceId 只在驱动命名空间内唯一，宿主会转换成不透明 DeviceId。 */
typedef struct VnaDeviceDescriptorViewV1 {
    uint32_t structSize;
    uint16_t portCount;
    uint16_t reserved;
    VnaStringViewV1 driverDeviceId;
    VnaStringViewV1 vendor;
    VnaStringViewV1 model;
    VnaStringViewV1 serialNumber;
} VnaDeviceDescriptorViewV1;

#endif
