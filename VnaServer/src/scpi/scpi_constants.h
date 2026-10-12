/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_SCPI_CONSTANTS_H
#define VNAPRO_SCPI_SCPI_CONSTANTS_H

#include <cstddef>

namespace VnaScpi {

// 这些值是标准 SCPI 错误队列的错误码，不是传输状态码。
enum ScpiErrorCode {
    SYNTAX_ERROR = -102,
    DATA_TYPE_ERROR = -104,
    PARAMETER_NOT_ALLOWED = -108,
    MISSING_PARAMETER = -109,
    UNDEFINED_HEADER = -113,
    SETTINGS_CONFLICT = -221,
    DATA_OUT_OF_RANGE = -222,
    ILLEGAL_PARAMETER_VALUE = -224,
    DATA_CORRUPT_OR_STALE = -230,
    INPUT_BUFFER_OVERRUN = -363
};

// 各项上限分别约束不同的协议或传输资源。
constexpr std::size_t RESPONSE_MESSAGE_BYTE_LIMIT = 1024U * 1024U;
constexpr std::size_t NUMERIC_ARRAY_BYTE_LIMIT = 1024U * 1024U;
constexpr std::size_t RESPONSE_QUEUE_BYTE_LIMIT = 1024U * 1024U;
constexpr std::size_t TRANSPORT_OUTPUT_BYTE_LIMIT = 1024U * 1024U;
constexpr std::size_t LINE_BUDGET = 16U * 1024U;
constexpr std::size_t MESSAGE_QUEUE_LIMIT = 32U;
// 全部 SCPI 连接的数量上限，同时约束累计输入、编码与输出内存预算。
constexpr std::size_t CONNECTION_LIMIT = 8U;
constexpr std::size_t OPC_WATCH_LIMIT = 32U;
constexpr std::size_t PROGRAM_UNIT_LIMIT = 128U;
constexpr std::size_t ENCODING_BATCH_SIZE = 256U;
constexpr unsigned DISPATCH_BATCH_SIZE = 32U;
constexpr unsigned REGISTER_MASK_MAXIMUM = 255U;
constexpr unsigned RECEIVER_PORT_MAXIMUM = 4U;
constexpr unsigned SWEEP_POINTS_MINIMUM = 2U;
constexpr unsigned SWEEP_POINTS_MAXIMUM = 10001U;
constexpr double IF_BANDWIDTH_MINIMUM_HZ = 10.0;
constexpr double IF_BANDWIDTH_MAXIMUM_HZ = 100000.0;
constexpr int ASCII_FRACTIONAL_DIGITS = 17;

}  // 命名空间 VnaScpi

#endif
