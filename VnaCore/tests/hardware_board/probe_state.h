/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_HARDWARE_BOARD_PROBE_STATE_H
#define VNAPRO_HARDWARE_BOARD_PROBE_STATE_H

#include <cstdint>
#include <string>
#include <vector>

namespace HardwareBoardProbe {

// 独立 ABI 测试仪器，只检查调用契约；不使用模拟器的状态机、模型或错误枚举。
class ProbeState final {
public:
    ProbeState() = default;
    ~ProbeState();
    std::uint32_t Initialize();
    void SetList(const char *name, std::uint64_t *values, std::uint32_t count);
    void SetPowers(double *values, std::uint32_t count);
    void SetPorts(std::uint32_t *ports, std::uint32_t count);
    std::uint32_t Download();
    std::uint32_t Rf(std::uint32_t enabled);
    std::uint32_t Start();
    std::uint32_t Get(std::uint32_t first, std::uint32_t count, double *data, int capacity);

private:
    bool Enter(const char *name, const std::string &arguments = std::string());
    void Record(const char *name, const std::string &arguments = std::string()) const;
    std::uint32_t Failure(const char *name, std::uint32_t occurrence) const;
    bool WaitForGate(std::uint32_t occurrence) const;
    bool ListsRetained() const;
    bool ValidateLists() const;

    std::string trace_;
    std::string failure_;
    std::string powerPattern_;
    std::uint32_t failureCode_ = 731U;
    std::uint32_t failAt_ = 1U;
    std::uint32_t gateAt_ = 0U;
    std::string thread_;
    std::vector<std::uint64_t> frequencies_;
    std::vector<double> powers_;
    std::vector<std::uint64_t> bandwidths_;
    std::vector<std::uint32_t> ports_;
    const std::uint64_t *frequencyPointer_ = nullptr;
    const double *powerPointer_ = nullptr;
    const std::uint64_t *bandwidthPointer_ = nullptr;
    const std::uint32_t *portPointer_ = nullptr;
    bool initialized_ = false;
    bool valid_ = true;
    bool rf_ = false;
    bool scanning_ = false;
    std::uint32_t sweep_ = 0U;
    std::uint32_t next_ = 0U;
    std::uint32_t getCalls_ = 0U;
    std::uint32_t downloadCalls_ = 0U;
    std::uint32_t offCalls_ = 0U;
};

ProbeState &State();

}  // 命名空间 HardwareBoardProbe

#endif
