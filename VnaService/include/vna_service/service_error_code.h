/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_SERVICE_ERROR_CODE_H
#define VNAPRO_SERVICE_ERROR_CODE_H

namespace vna_service {
namespace detail {
enum ServiceErrorCode : int {
    MISSING_PARAMETER_ERROR = -109,
    SETTINGS_CONFLICT_ERROR = -221,
    DATA_OUT_OF_RANGE_ERROR = -222,
    INVALID_PARAMETER_ERROR = -224,
    DEVICE_SPECIFIC_ERROR = -310,
    ERROR_QUEUE_OVERFLOW = -350,
    INPUT_BUFFER_OVERRUN = -363
};
}  // namespace detail
}  // namespace vna_service
#endif
