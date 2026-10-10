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

// One engine per TCP controller; all calls belong to the service executor.
class ScpiEngine final {
public:
    // Acquires exclusive control when available. The service must outlive this engine.
    explicit ScpiEngine(VnaService::InstrumentService &service);
    // Destroy only after accepted operations drain; an idle engine releases its grant.
    ~ScpiEngine() noexcept;
    ScpiEngine(const ScpiEngine &) = delete;
    ScpiEngine &operator=(const ScpiEngine &) = delete;
    ScpiEngine(ScpiEngine &&) = delete;
    ScpiEngine &operator=(ScpiEngine &&) = delete;
    // Admits a bounded program line; false means disconnected, busy, or over budget.
    bool SubmitLine(const std::string &line);
    // Advances accepted commands without blocking for device I/O.
    void Poll();
    // Transfers one complete LF-terminated response, or an empty string if unavailable.
    std::string TakeOutput();
    // Includes queued commands, finite operations, encoding, and OPC watches.
    bool HasPending() const noexcept;
    // Reports whether this connection owns the service control grant.
    bool HasControl() const noexcept;
    // Includes unsent transport bytes in the SCPI message-available status bit.
    void SetTransportOutputPending(bool pending) noexcept;
    // Discards responses; keep polling until accepted operations release control.
    void Disconnect() noexcept;
    // True only after disconnection, operation completion, and control release.
    bool CanReleaseControl() const noexcept;

private:
    // Program-message state remains owned by this connection until all units finish.
    struct ScpiMessage final {
        std::vector<std::string> units;
        std::vector<std::string> parent;
        std::size_t position = 0;
        std::string response;
        bool queryFailed = false;
    };

    // Each OPC watch observes the operations accepted before its program unit.
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
    void DispatchSelectMeasurement(const ParsedCommand &command);
    void DispatchMeasurementCatalog(const ParsedCommand &command);
    void DispatchDeleteMeasurement(const ParsedCommand &command);
    void DispatchDataFormat(const ParsedCommand &command);
    void DispatchStimulusAxis(const ParsedCommand &command);
    void DispatchMeasurementData(const ParsedCommand &command);
    void PollOperationWatches();
    bool PollBarrier();
    void PollMessages();

    VnaService::InstrumentService &service_;
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

}  // namespace VnaScpi
#endif
