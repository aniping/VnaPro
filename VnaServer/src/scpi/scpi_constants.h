/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_SCPI_CONSTANTS_H
#define VNAPRO_SCPI_SCPI_CONSTANTS_H

#include <cstddef>

namespace vna_scpi {

// Values are the standard SCPI error queue codes, not transport status codes.
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

// Each bound protects a separate protocol or transport resource.
constexpr std::size_t RESPONSE_MESSAGE_BYTE_LIMIT = 1024U * 1024U;
constexpr std::size_t NUMERIC_ARRAY_BYTE_LIMIT = 1024U * 1024U;
constexpr std::size_t RESPONSE_QUEUE_BYTE_LIMIT = 1024U * 1024U;
constexpr std::size_t TRANSPORT_OUTPUT_BYTE_LIMIT = 1024U * 1024U;
constexpr std::size_t LINE_BUDGET = 16U * 1024U;
constexpr std::size_t MESSAGE_QUEUE_LIMIT = 32U;
constexpr std::size_t OPC_WATCH_LIMIT = 32U;
constexpr std::size_t PROGRAM_UNIT_LIMIT = 128U;
constexpr std::size_t ENCODING_BATCH_SIZE = 256U;
constexpr unsigned DISPATCH_BATCH_SIZE = 32U;
constexpr unsigned REGISTER_MASK_MAXIMUM = 255U;
constexpr unsigned RECEIVER_PORT_MAXIMUM = 4U;
constexpr unsigned SWEEP_POINTS_MINIMUM = 2U;
constexpr unsigned SWEEP_POINTS_MAXIMUM = 10001U;
constexpr double FREQUENCY_MINIMUM_HZ = 1.0;
constexpr double FREQUENCY_MAXIMUM_HZ = 1000000000.0;
constexpr double IF_BANDWIDTH_MINIMUM_HZ = 10.0;
constexpr double IF_BANDWIDTH_MAXIMUM_HZ = 100000.0;
constexpr int ASCII_FRACTIONAL_DIGITS = 17;

}  // namespace vna_scpi

#endif
