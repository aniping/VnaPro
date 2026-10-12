/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#ifndef VNAPRO_SCPI_ENGINE_H
#define VNAPRO_SCPI_ENGINE_H

#include <cstddef>
#include <deque>
#include <string>
#include <vector>

#include "scpi/parsed_command.h"
#include "vna_service/instrument_service.h"

namespace VnaScpi {

// 每个 TCP 控制连接对应一个引擎；所有调用都由服务执行线程完成。
class ScpiEngine final {
public:
    // 控制权空闲时取得写控制权，否则固定为只读观察连接；不会自动升级。
    explicit ScpiEngine(VnaService::InstrumentService &service);
    // 已接收的操作全部完成后才能销毁；空闲引擎在销毁时释放控制权。
    ~ScpiEngine() noexcept;
    ScpiEngine(const ScpiEngine &) = delete;
    ScpiEngine &operator=(const ScpiEngine &) = delete;
    ScpiEngine(ScpiEngine &&) = delete;
    ScpiEngine &operator=(ScpiEngine &&) = delete;
    // 接收长度受限的程序消息行；连接已断开、引擎忙或超出容量预算时返回假。
    bool SubmitLine(const std::string &line);
    // 推进已接收的命令，不阻塞等待设备 I/O。
    void Poll();
    // 移交一条以 LF 结尾的完整响应；暂无响应时返回空字符串。
    std::string TakeOutput();
    // 包括排队命令、有限操作、响应编码和 OPC 完成监视。
    bool HasPending() const noexcept;
    // 报告本连接是否持有服务控制权。
    bool HasControl() const noexcept;
    // 观察连接的最近错误仅保存在本连接，不改变仪器的全局 SCPI 错误队列。
    const VnaService::InstrumentError &LocalError() const noexcept;
    // 设置 SCPI 消息可用状态位时，也计入传输层的待发送字节。
    void SetTransportOutputPending(bool pending) noexcept;
    // 丢弃响应；应继续轮询，直到已接收操作完成并释放控制权。
    void Disconnect() noexcept;
    // 仅在连接断开、操作完成且控制权释放后返回真。
    bool CanReleaseControl() const noexcept;

private:
    // 程序消息状态由本连接持有，直到所有程序单元执行完成。
    struct ScpiMessage final {
        std::vector<std::string> units;
        std::vector<std::string> parent;
        std::size_t position = 0;
        std::string response;
        bool queryFailed = false;
    };

    // 每个 OPC 监视项仅跟踪其程序单元之前已接收的操作。
    struct ScpiOpcWatch final {
        std::vector<VnaService::Operation> operations;
        unsigned generation = 0;
    };

    void RecordCommandError(const ParsedCommand &command, int code, const std::string &text);
    bool CheckArguments(const ParsedCommand &command, std::size_t count);
    void AppendResponse(const std::string &unit);
    void BeginEncoding(VnaService::DataResult data, const ParsedCommand &command);
    void EncodeBatch();
    void SetOperation(VnaService::Operation operation, bool asynchronous = false);
    bool NumericArgument(const ParsedCommand &command, double &value, double minimum, double maximum, bool hz);
    bool QueryNumeric(const ParsedCommand &command, double current, double minimum, double maximum);
    void Dispatch(const ParsedCommand &command);
    bool DispatchCommon(const ParsedCommand &command);
    bool DispatchSweep(const ParsedCommand &command);
    bool DispatchMeasurement(const ParsedCommand &command);
    void DispatchIdentity(const ParsedCommand &command);
    void DispatchClearStatus(const ParsedCommand &command);
    void DispatchOperationComplete(const ParsedCommand &command);
    void DispatchWait(const ParsedCommand &command);
    void DispatchEnableRegister(const ParsedCommand &command);
    void DispatchStatusRegister(const ParsedCommand &command);
    void DispatchNextError(const ParsedCommand &command);
    void DispatchErrorCount(const ParsedCommand &command);
    void DispatchFrequency(const ParsedCommand &command);
    void DispatchPoints(const ParsedCommand &command);
    void DispatchBandwidth(const ParsedCommand &command);
    void DispatchSweepType(const ParsedCommand &command);
    void DispatchTriggerSource(const ParsedCommand &command);
    void DispatchTriggerScope(const ParsedCommand &command);
    void DispatchSweepMode(const ParsedCommand &command);
    void DispatchInitiate(const ParsedCommand &command);
    void DispatchDefineMeasurement(const ParsedCommand &command);
    void DispatchMeasurementCatalog(const ParsedCommand &command);
    void DispatchDeleteMeasurement(const ParsedCommand &command);
    void DispatchDataFormat(const ParsedCommand &command);
    void DispatchStimulusAxis(const ParsedCommand &command);
    void DispatchMeasurementData(const ParsedCommand &command);
    void PollOperationWatches();
    bool PollBarrier();
    void PollMessages();
    bool ObserverMessageAllowed(const ScpiMessage &message);

    VnaService::InstrumentService &service_;
    VnaService::InstrumentError localError_;
    bool control_ = false;
    bool connected_ = true;
    bool transportOutputPending_ = false;
    std::deque<ScpiMessage> messages_;
    std::deque<std::string> output_;
    std::size_t outputBytes_ = 0;
    VnaService::Operation sequential_;
    bool waiting_ = false;
    bool waitingQuery_ = false;
    std::vector<VnaService::Operation> barrier_;
    std::vector<ScpiOpcWatch> watches_;
    std::vector<double> encoding_;
    std::size_t encoded_ = 0;
    std::string encodedResponse_;
};

}  // 命名空间 VnaScpi
#endif
