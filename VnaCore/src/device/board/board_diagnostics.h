/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_VNA_CORE_BOARD_DIAGNOSTICS_H
#define VNAPRO_VNA_CORE_BOARD_DIAGNOSTICS_H

#include <cstdint>
#include <string>
#include <vector>

namespace VnaCore {
namespace Detail {

// 精确的 VN10 下发缓冲；工作端口编号 1～4，会话拥有数组，日志只在配置边界同步借用。
struct BoardDownloadLists final {
    std::vector<std::uint64_t> frequencies;
    std::vector<double> powers;
    std::vector<std::uint32_t> bandwidths;
    std::vector<std::uint32_t> ports;
};

// 以下诊断不传播异常；关闭对应日志等级时不构造数组文本。
void LogBoardBinding(const std::string &path, const char *event, const char *error = "") noexcept;
void LogBoardCallBegin(const char *phase, std::uint64_t config, const char *step) noexcept;
std::uint32_t LogBoardCallReturned(
    const char *phase, std::uint64_t config, const char *step, std::uint32_t status) noexcept;
void LogBoardSetterReturned(std::uint64_t config, const char *step) noexcept;
// INFO 摘要；DEBUG 分块完整值。仅在 SET 完成、Download 开始前调用。
void LogBoardConfiguration(std::uint64_t config, const BoardDownloadLists &lists) noexcept;
// Download 失败时以 ERROR 补完整值，普通 INFO 配置也可保留故障参数。
void LogBoardConfigurationFailure(std::uint64_t config, const BoardDownloadLists &lists) noexcept;

}  // 命名空间 Detail
}  // 命名空间 VnaCore

#endif
