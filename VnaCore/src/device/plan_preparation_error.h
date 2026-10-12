/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNACORE_SRC_DEVICE_PLANPREPARATIONERROR_H
#define VNAPRO_VNACORE_SRC_DEVICE_PLANPREPARATIONERROR_H
#include <exception>

namespace VnaCore {
namespace Detail {

// 此异常仅用于计划相关设备状态尚未变更时发生的失败。
class PlanPreparationError final : public std::exception {
public:
    // 消息使用程序生命周期的字符串字面量，构造异常本身不进行动态分配。
    explicit PlanPreparationError(const char *message = "device plan buffer preparation failed") noexcept;
    // 区分设备状态变更前的分配失败与原生配置失败。
    const char *what() const noexcept override;

private:
    const char * const message_;
};

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
