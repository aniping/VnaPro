/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_NATIVE_SIM_FAILURE_INJECTOR_H
#define VNAPRO_NATIVE_SIM_FAILURE_INJECTOR_H

#include "vna_native/native_api_contract.h"

namespace VnaNative {

enum class FailurePoint {
    BEFORE_FRAME_COMMIT,
    BEFORE_PENDING_COMMIT
};

class VNA_NATIVE_HIDDEN FailureInjector {
public:
    FailureInjector(const FailureInjector &) = delete;
    FailureInjector &operator=(const FailureInjector &) = delete;
    FailureInjector(FailureInjector &&) = delete;
    FailureInjector &operator=(FailureInjector &&) = delete;

    // 通过接口析构时，释放派生测试夹具。
    virtual ~FailureInjector() = default;

    // 提交前在状态互斥锁下调用；实现不得重入状态对象。
    virtual void ThrowIfRequested(FailurePoint point) = 0;

protected:
    FailureInjector() noexcept = default;
};

}  // 命名空间 VnaNative
#endif  // VNAPRO_NATIVE_SIM_FAILURE_INJECTOR_H
