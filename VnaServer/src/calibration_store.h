/* Copyright (c) 2026 Huawei Device Co., Ltd. */
#ifndef VNAPRO_SERVER_CALIBRATION_STORE_H
#define VNAPRO_SERVER_CALIBRATION_STORE_H

#include <cstdint>
#include <map>
#include <mutex>
#include <string>
#include <vector>

#include "vna_service/calibration.h"

namespace VnaServer {

// 每通道以原子 manifest 发布不可变 JSON 集；不启用校正、不替代服务的系数/设备适用性验证。
// 同实例 Load/Save 由内部互斥锁串行；跨实例使用通道文件锁和 manifest 指纹拒绝陈旧写入。
class CalibrationStore final {
public:
    explicit CalibrationStore(std::string directory);
    // 首次无 manifest 是空库；损坏、缺文件或 I/O 错误返回假且保留调用方输出。
    bool Load(unsigned channelId, std::vector<VnaService::CalibrationSetPtr> &sets, std::uint64_t &selectedId,
        std::string &error);
    // 返回真表示已发布；若 error 非空，是提交后的持久性/清理警告，调用方须报告且不能回滚内存。
    // 旧 shared_ptr 集复用文件；返回假不会发布新 manifest。
    bool Save(unsigned channelId, const std::vector<VnaService::CalibrationSetPtr> &sets, std::uint64_t selectedId,
        std::string &error);

private:
    struct Entry final {
        VnaService::CalibrationSetPtr set;
        std::string file;
        std::string digest;
        std::size_t bytes = 0U;
    };

    using Entries = std::map<std::uint64_t, Entry>;
    Entries Read(unsigned channelId, std::vector<VnaService::CalibrationSetPtr> &sets, std::uint64_t &selectedId,
        std::string &manifestDigest);
    std::string directory_;
    std::map<unsigned, Entries> cache_;
    std::map<unsigned, std::string> manifests_;
    std::mutex mutex_;
};
}  // 命名空间 VnaServer
#endif
