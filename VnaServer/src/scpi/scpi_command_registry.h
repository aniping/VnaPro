/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_COMMAND_REGISTRY_H
#define VNAPRO_SCPI_COMMAND_REGISTRY_H

#include <string>

namespace VnaScpi {
enum class CommandHandler {
    IDENTITY,
    CLEAR_STATUS,
    RESET,
    OPERATION_COMPLETE,
    WAIT,
    ENABLE_REGISTER,
    STATUS_REGISTER,
    NEXT_ERROR,
    ERROR_COUNT,
    FREQUENCY,
    POINTS,
    BANDWIDTH,
    POWER,
    SWEEP_TYPE,
    TRIGGER_SOURCE,
    TRIGGER_SCOPE,
    SWEEP_MODE,
    INITIATE,
    DEFINE_MEASUREMENT,
    MEASUREMENT_CATALOG,
    DELETE_MEASUREMENT,
    DELETE_ALL_MEASUREMENTS,
    DATA_FORMAT,
    BYTE_ORDER,
    STIMULUS_AXIS,
    MEASUREMENT_DATA,
    MULTIPLE_MEASUREMENT_DATA
};

// 同一登记同时约束实际分派、query/setter 与观察者准入。
struct CommandDefinition final {
    const char *path;
    CommandHandler handler;
    bool setter;
    bool query;
    bool observer;
};

// 输入为通道冻结后的规范路径；不存在的命令返回空指针。
const CommandDefinition *FindCommand(const std::string &path) noexcept;
}  // 命名空间 VnaScpi
#endif
