/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include "scpi/scpi_engine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>
#include <vector>

#include "scpi/scpi_constants.h"
#include "scpi/scpi_parser.h"
#include "vna_service/instrument_service.h"

namespace VnaScpi {
namespace {
const std::string SCPI_CONTROL_OWNER = "SCPI";

bool OperationReady(const VnaService::Operation &operation)
{
    return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool AllReady(const std::vector<VnaService::Operation> &operations)
{
    for (const auto &operation : operations) {
        if (!OperationReady(operation)) {
            return false;
        }
    }
    return true;
}

std::string Number(double value)
{
    std::ostringstream stream;
    stream.exceptions(std::ios::badbit | std::ios::failbit);
    stream.imbue(std::locale::classic());
    stream << std::setprecision(ASCII_FRACTIONAL_DIGITS) << std::scientific << value;
    return stream.str();
}

bool StringArgument(const std::string &text, std::string &value)
{
    if (text.size() < 2U || (text.front() != '"' && text.front() != '\'') || text.back() != text.front()) {
        return false;
    }
    value.clear();
    for (std::size_t index = 1U; index + 1U < text.size(); ++index) {
        if (text[index] == text.front()) {
            if (index + 2U >= text.size() || text[index + 1U] != text.front()) {
                return false;
            }
            ++index;
        }
        value += text[index];
    }
    return true;
}

bool ReceiverParameter(const std::string &text, VnaService::Measurement &measurement)
{
    const auto comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    const std::string receiver = Trim(text.substr(0, comma));
    unsigned source = 0;
    unsigned port = 0;
    const char kind =
        receiver.empty() ? '\0' : static_cast<char>(std::tolower(static_cast<unsigned char>(receiver.front())));
    if (receiver.size() != 2U || (kind != 'a' && kind != 'b') ||
        !Integer(receiver.substr(1), port, RECEIVER_PORT_MAXIMUM) || port == 0U ||
        !Integer(text.substr(comma + 1U), source, RECEIVER_PORT_MAXIMUM) || source == 0U) {
        return false;
    }
    measurement.receiverKind = kind;
    measurement.receiverPort = port;
    measurement.sourcePort = source;
    return true;
}

bool MeasurementParameter(const std::string &text, VnaService::Measurement &measurement)
{
    // Keysight 扩展测量参数区分大小写；同时接受标准单数字端口下划线写法。
    if (text == "S11" || text == "S1_1" || text == "S21" || text == "S2_1") {
        measurement.kind = VnaService::MeasurementKind::S_PARAMETER;
        measurement.receiverKind = 'b';
        measurement.receiverPort = text[1] == '1' ? 1U : 2U;
        measurement.sourcePort = 1U;
        return true;
    }
    return ReceiverParameter(text, measurement);
}

bool Matches(const std::string &value, const char *shortName, const char *fullName)
{
    return value == shortName || value == fullName;
}
}  // 匿名命名空间

bool ScpiEngine::HasPending() const noexcept
{
    return !messages_.empty() || sequential_.valid() || waiting_ || !encoding_.empty() || !watches_.empty() ||
           service_.HasPending();
}

void ScpiEngine::RecordCommandError(const ParsedCommand &command, int code, const std::string &text)
{
    service_.RecordError(code, text);
    if (command.query && !messages_.empty()) {
        messages_.front().queryFailed = true;
    }
}

bool ScpiEngine::CheckArguments(const ParsedCommand &command, std::size_t count)
{
    if (command.arguments.size() == count) {
        return true;
    }
    RecordCommandError(command, command.arguments.size() < count ? MISSING_PARAMETER : PARAMETER_NOT_ALLOWED,
        "Parameter count mismatch");
    return false;
}

void ScpiEngine::AppendResponse(const std::string &unit)
{
    if (messages_.empty() || !connected_) {
        return;
    }
    ScpiMessage &message = messages_.front();
    if ((message.response.size() + unit.size() + 2U) > RESPONSE_MESSAGE_BYTE_LIMIT) {
        service_.RecordError(INPUT_BUFFER_OVERRUN, "Response exceeds software output budget");
        message.queryFailed = true;
        return;
    }
    if (!message.response.empty()) {
        message.response += ';';
    }
    message.response += unit;
}

void ScpiEngine::BeginEncoding(VnaService::DataResult data, const ParsedCommand &command)
{
    if (!data.ok) {
        RecordCommandError(command, DATA_CORRUPT_OR_STALE, data.error);
        return;
    }
    if (!connected_) {
        return;
    }
    encoding_ = std::move(data.values);
    encoded_ = 0;
    encodedResponse_.clear();
    if (encoding_.empty()) {
        RecordCommandError(command, DATA_CORRUPT_OR_STALE, "Empty data array");
    }
}

void ScpiEngine::EncodeBatch()
{
    if (encoding_.empty()) {
        return;
    }
    const std::size_t end = (std::min)(encoded_ + ENCODING_BATCH_SIZE, encoding_.size());
    for (; encoded_ < end; ++encoded_) {
        if (encoded_ != 0U) {
            encodedResponse_ += ',';
        }
        encodedResponse_ += Number(encoding_[encoded_]);
    }
    if (encodedResponse_.size() > NUMERIC_ARRAY_BYTE_LIMIT) {
        service_.RecordError(INPUT_BUFFER_OVERRUN, "Array exceeds software output budget");
        messages_.front().queryFailed = true;
        encoding_.clear();
        encodedResponse_.clear();
    } else if (encoded_ == encoding_.size()) {
        AppendResponse(encodedResponse_);
        encoding_.clear();
        encodedResponse_.clear();
    }
}

void ScpiEngine::SetOperation(VnaService::Operation operation, bool asynchronous)
{
    if (!asynchronous) {
        sequential_ = std::move(operation);
    }
}

bool ScpiEngine::NumericArgument(const ParsedCommand &command, double &value, double minimum, double maximum, bool hz)
{
    if (!CheckArguments(command, 1U)) {
        return false;
    }
    const std::string parameter = Upper(command.arguments.front());
    if (parameter == "MIN" || parameter == "MINIMUM") {
        value = minimum;
    } else if (parameter == "MAX" || parameter == "MAXIMUM") {
        value = maximum;
    } else if (!Numeric(command.arguments.front(), value, hz)) {
        RecordCommandError(command, DATA_TYPE_ERROR, "Numeric data or unit is invalid");
        return false;
    }
    return true;
}

bool ScpiEngine::QueryNumeric(const ParsedCommand &command, double current, double minimum, double maximum)
{
    if (command.arguments.empty()) {
        AppendResponse(Number(current));
        return true;
    }
    if (command.arguments.size() == 1U) {
        const auto parameter = Upper(command.arguments.front());
        if (Matches(parameter, "MIN", "MINIMUM")) {
            AppendResponse(Number(minimum));
            return true;
        }
        if (Matches(parameter, "MAX", "MAXIMUM")) {
            AppendResponse(Number(maximum));
            return true;
        }
    }
    RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Query accepts only MINimum or MAXimum");
    return false;
}

void ScpiEngine::Dispatch(const ParsedCommand &command)
{
    if (command.errorCode != 0) {
        RecordCommandError(command, command.errorCode, command.error);
        return;
    }
    if (DispatchCommon(command) || DispatchSweep(command) || DispatchMeasurement(command)) {
        return;
    }
    RecordCommandError(command, UNDEFINED_HEADER, "Undefined header");
}

bool ScpiEngine::DispatchCommon(const ParsedCommand &command)
{
    const std::string &path = command.path;
    if (path == "*IDN" && command.query) {
        DispatchIdentity(command);
        return true;
    }
    if (path == "*CLS" && !command.query) {
        DispatchClearStatus(command);
        return true;
    }
    if (path == "*OPC") {
        DispatchOperationComplete(command);
        return true;
    }
    if (path == "*WAI" && !command.query) {
        DispatchWait(command);
        return true;
    }
    if (path == "*ESE" || path == "*SRE") {
        DispatchEnableRegister(command);
        return true;
    }
    if ((path == "*ESR" || path == "*STB") && command.query) {
        DispatchStatusRegister(command);
        return true;
    }
    if ((path == "SYST:ERR" || path == "SYST:ERR:NEXT") && command.query) {
        DispatchNextError(command);
        return true;
    }
    if (path == "SYST:ERR:COUN" && command.query) {
        DispatchErrorCount(command);
        return true;
    }
    return false;
}

bool ScpiEngine::DispatchSweep(const ParsedCommand &command)
{
    const std::string &path = command.path;
    if (path == "SENS1:FREQ:STAR" || path == "SENS1:FREQ:STOP") {
        DispatchFrequency(command);
        return true;
    }
    if (path == "SENS1:SWE:POIN") {
        DispatchPoints(command);
        return true;
    }
    if (path == "SENS1:BAND" || path == "SENS1:BAND:RES" || path == "SENS1:BWID" || path == "SENS1:BWID:RES") {
        DispatchBandwidth(command);
        return true;
    }
    if (path == "SENS1:SWE:TYPE") {
        DispatchSweepType(command);
        return true;
    }
    if (path == "TRIG:SOUR" || path == "TRIG:SEQ:SOUR" || path == "INIT:CONT" || path == "INIT1:CONT") {
        DispatchTriggerSource(command);
        return true;
    }
    if (path == "TRIG:SCOP" || path == "TRIG:SEQ:SCOP") {
        DispatchTriggerScope(command);
        return true;
    }
    if (path == "SENS1:SWE:MODE") {
        DispatchSweepMode(command);
        return true;
    }
    if ((path == "INIT" || path == "INIT1" || path == "INIT:IMM" || path == "INIT1:IMM") && !command.query) {
        DispatchInitiate(command);
        return true;
    }
    return false;
}

bool ScpiEngine::DispatchMeasurement(const ParsedCommand &command)
{
    const std::string &path = command.path;
    if (path == "CALC1:PAR:DEF:EXT" || path == "CALC1:PAR:EXT") {
        DispatchDefineMeasurement(command);
        return true;
    }
    if (path == "CALC1:PAR:SEL") {
        DispatchSelectMeasurement(command);
        return true;
    }
    if (path == "CALC1:PAR:CAT:EXT" && command.query) {
        DispatchMeasurementCatalog(command);
        return true;
    }
    if ((path == "CALC1:PAR:DEL" || path == "CALC1:PAR:DEL:NAME") && !command.query) {
        DispatchDeleteMeasurement(command);
        return true;
    }
    if (path == "FORM" || path == "FORM:DATA") {
        DispatchDataFormat(command);
        return true;
    }
    if ((path == "SENS1:X" || path == "SENS1:X:VAL") && command.query) {
        DispatchStimulusAxis(command);
        return true;
    }
    if (path == "CALC1:DATA" && command.query) {
        DispatchMeasurementData(command);
        return true;
    }
    return false;
}

void ScpiEngine::DispatchIdentity(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        AppendResponse(service_.Identity());
    }
}

void ScpiEngine::DispatchClearStatus(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        service_.ClearStatus();
    }
}

void ScpiEngine::DispatchOperationComplete(const ParsedCommand &command)
{
    if (!CheckArguments(command, 0)) {
        return;
    }
    if (command.query) {
        barrier_ = service_.PendingOperations();
        waiting_ = true;
        waitingQuery_ = true;
    } else {
        if (watches_.size() >= OPC_WATCH_LIMIT) {
            RecordCommandError(command, INPUT_BUFFER_OVERRUN, "OPC pending buffer overrun");
            return;
        }
        ScpiOpcWatch watch;
        watch.operations = service_.PendingOperations();
        watch.generation = service_.StatusGeneration();
        if (AllReady(watch.operations)) {
            service_.MarkOperationComplete();
        } else {
            watches_.push_back(std::move(watch));
        }
    }
}

void ScpiEngine::DispatchWait(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        barrier_ = service_.PendingOperations();
        waiting_ = true;
        waitingQuery_ = false;
    }
}

void ScpiEngine::DispatchEnableRegister(const ParsedCommand &command)
{
    const std::string &path = command.path;
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse(std::to_string(path == "*ESE" ? service_.EventEnable() : service_.ServiceRequestEnable()));
        }
    } else {
        unsigned mask = 0;
        if (!CheckArguments(command, 1)) {
            return;
        }
        if (!Integer(command.arguments.front(), mask, REGISTER_MASK_MAXIMUM)) {
            RecordCommandError(command, DATA_OUT_OF_RANGE, "Register mask outside 0..255");
            return;
        }
        if (path == "*ESE") {
            service_.SetEventEnable(mask);
        } else {
            service_.SetServiceRequestEnable(mask);
        }
    }
}

void ScpiEngine::DispatchStatusRegister(const ParsedCommand &command)
{
    const std::string &path = command.path;
    if (CheckArguments(command, 0)) {
        AppendResponse(
            std::to_string(path == "*ESR" ? service_.ReadEventRegister()
                                          : service_.StatusByte(!output_.empty() || transportOutputPending_)));
    }
}

void ScpiEngine::DispatchNextError(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        const auto value = service_.PopError();
        AppendResponse(std::to_string(value.code) + ',' + Quote(value.message));
    }
}

void ScpiEngine::DispatchErrorCount(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        AppendResponse(std::to_string(service_.ErrorCount()));
    }
}

void ScpiEngine::DispatchFrequency(const ParsedCommand &command)
{
    const std::string &path = command.path;
    const auto &settings = service_.Settings();
    const bool start = path == "SENS1:FREQ:STAR";
    if (command.query) {
        QueryNumeric(command, start ? settings.startHz : settings.stopHz, FREQUENCY_MINIMUM_HZ, FREQUENCY_MAXIMUM_HZ);
        return;
    }
    double value = 0;
    if (NumericArgument(command, value, FREQUENCY_MINIMUM_HZ, FREQUENCY_MAXIMUM_HZ, true)) {
        SetOperation(start ? service_.SetStartHz(value) : service_.SetStopHz(value));
    }
}

void ScpiEngine::DispatchPoints(const ParsedCommand &command)
{
    const auto &settings = service_.Settings();
    if (command.query) {
        if (command.arguments.empty()) {
            AppendResponse(std::to_string(settings.points));
        } else if (command.arguments.size() == 1U && Matches(Upper(command.arguments.front()), "MIN", "MINIMUM")) {
            AppendResponse(std::to_string(SWEEP_POINTS_MINIMUM));
        } else if (command.arguments.size() == 1U && Matches(Upper(command.arguments.front()), "MAX", "MAXIMUM")) {
            AppendResponse(std::to_string(SWEEP_POINTS_MAXIMUM));
        } else {
            RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Query accepts only MINimum or MAXimum");
        }
        return;
    }
    double value = 0;
    if (!NumericArgument(command, value, SWEEP_POINTS_MINIMUM, SWEEP_POINTS_MAXIMUM, false)) {
        return;
    }
    if (value < SWEEP_POINTS_MINIMUM || value > SWEEP_POINTS_MAXIMUM || value != static_cast<unsigned>(value)) {
        RecordCommandError(command, DATA_OUT_OF_RANGE, "Point count outside 2..10001");
        return;
    }
    SetOperation(service_.SetPoints(static_cast<unsigned>(value)));
}

void ScpiEngine::DispatchBandwidth(const ParsedCommand &command)
{
    const auto &settings = service_.Settings();
    if (command.query) {
        QueryNumeric(command, settings.ifBandwidthHz, IF_BANDWIDTH_MINIMUM_HZ, IF_BANDWIDTH_MAXIMUM_HZ);
        return;
    }
    double value = 0;
    if (NumericArgument(command, value, IF_BANDWIDTH_MINIMUM_HZ, IF_BANDWIDTH_MAXIMUM_HZ, true)) {
        SetOperation(service_.SetIfBandwidthHz(value));
    }
}

void ScpiEngine::DispatchSweepType(const ParsedCommand &command)
{
    const auto &settings = service_.Settings();
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse("LIN");
        }
    } else if (CheckArguments(command, 1)) {
        if (!Matches(Upper(command.arguments.front()), "LIN", "LINEAR")) {
            RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Only LINear sweep is supported");
        } else if (settings.sweepMode != VnaService::SweepMode::HOLD || service_.HasPending()) {
            RecordCommandError(command, SETTINGS_CONFLICT, "Configuration requires HOLD");
        }
    }
}

void ScpiEngine::DispatchTriggerSource(const ParsedCommand &command)
{
    const std::string &path = command.path;
    const auto &settings = service_.Settings();
    const bool boolean = path.find("INIT") == 0U;
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse(boolean ? (settings.triggerSource == VnaService::TriggerSource::IMMEDIATE ? "1" : "0")
                                   : (settings.triggerSource == VnaService::TriggerSource::IMMEDIATE ? "IMM" : "MAN"));
        }
        return;
    }
    if (!CheckArguments(command, 1)) {
        return;
    }
    const std::string value = Upper(command.arguments.front());
    VnaService::TriggerSource source = VnaService::TriggerSource::MANUAL;
    if (boolean && (value == "ON" || value == "1")) {
        source = VnaService::TriggerSource::IMMEDIATE;
    } else if (boolean && (value == "OFF" || value == "0")) {
        source = VnaService::TriggerSource::MANUAL;
    } else if (!boolean && Matches(value, "IMM", "IMMEDIATE")) {
        source = VnaService::TriggerSource::IMMEDIATE;
    } else if (!boolean && Matches(value, "MAN", "MANUAL")) {
        source = VnaService::TriggerSource::MANUAL;
    } else {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Unsupported trigger source");
        return;
    }
    const bool asynchronous =
        source == VnaService::TriggerSource::IMMEDIATE && settings.sweepMode == VnaService::SweepMode::SINGLE;
    SetOperation(service_.SetTriggerSource(source), asynchronous);
}

void ScpiEngine::DispatchTriggerScope(const ParsedCommand &command)
{
    const auto &settings = service_.Settings();
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse(settings.currentTriggerScope ? "CURR" : "ALL");
        }
        return;
    }
    if (!CheckArguments(command, 1)) {
        return;
    }
    const std::string value = Upper(command.arguments.front());
    if (value == "ALL") {
        service_.SetCurrentTriggerScope(false);
    } else if (Matches(value, "CURR", "CURRENT")) {
        service_.SetCurrentTriggerScope(true);
    } else {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Unsupported trigger scope");
    }
}

void ScpiEngine::DispatchSweepMode(const ParsedCommand &command)
{
    const auto &settings = service_.Settings();
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse(settings.sweepMode == VnaService::SweepMode::HOLD
                               ? "HOLD"
                               : (settings.sweepMode == VnaService::SweepMode::SINGLE ? "SING" : "CONT"));
        }
        return;
    }
    if (!CheckArguments(command, 1)) {
        return;
    }
    const std::string value = Upper(command.arguments.front());
    VnaService::SweepMode mode = VnaService::SweepMode::HOLD;
    if (value == "HOLD") {
        mode = VnaService::SweepMode::HOLD;
    } else if (Matches(value, "CONT", "CONTINUOUS")) {
        mode = VnaService::SweepMode::CONTINUOUS;
    } else if (Matches(value, "SING", "SINGLE")) {
        mode = VnaService::SweepMode::SINGLE;
    } else {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Unsupported sweep mode");
        return;
    }
    SetOperation(service_.SetSweepMode(mode),
        mode == VnaService::SweepMode::SINGLE && settings.triggerSource == VnaService::TriggerSource::IMMEDIATE);
}

void ScpiEngine::DispatchInitiate(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        SetOperation(service_.Trigger(), true);
    }
}

void ScpiEngine::DispatchDefineMeasurement(const ParsedCommand &command)
{
    if (command.query) {
        RecordCommandError(command, UNDEFINED_HEADER, "Undefined query header");
        return;
    }
    if (!CheckArguments(command, 2)) {
        return;
    }
    VnaService::Measurement measurement;
    std::string parameter;
    if (!StringArgument(command.arguments[0], measurement.name) || !StringArgument(command.arguments[1], parameter)) {
        RecordCommandError(command, DATA_TYPE_ERROR, "Measurement name and parameter require quoted strings");
        return;
    }
    if (!MeasurementParameter(parameter, measurement)) {
        RecordCommandError(
            command, ILLEGAL_PARAMETER_VALUE, "Only S11, S21 and logical a1..a4/b1..b4 receivers are supported");
        return;
    }
    SetOperation(service_.DefineMeasurement(measurement));
}

void ScpiEngine::DispatchSelectMeasurement(const ParsedCommand &command)
{
    const auto &settings = service_.Settings();
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse(Quote(settings.selectedMeasurement));
        }
        return;
    }
    if (!CheckArguments(command, 1)) {
        return;
    }
    std::string name;
    if (!StringArgument(command.arguments.front(), name)) {
        RecordCommandError(command, DATA_TYPE_ERROR, "Measurement name requires a string");
        return;
    }
    service_.SelectMeasurement(name);
}

void ScpiEngine::DispatchMeasurementCatalog(const ParsedCommand &command)
{
    const auto &settings = service_.Settings();
    if (command.arguments.size() > 1U) {
        RecordCommandError(command, PARAMETER_NOT_ALLOWED, "Too many parameters");
        return;
    }
    if (!command.arguments.empty()) {
        const auto value = Upper(command.arguments.front());
        if (!Matches(value, "NORM", "NORMAL") && !Matches(value, "DEF", "DEFINE") &&
            !Matches(value, "DISP", "DISPLAY")) {
            RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Unsupported catalog option");
            return;
        }
    }
    std::string catalog;
    for (const auto &measurement : settings.measurements) {
        if (!catalog.empty()) {
            catalog += ',';
        }
        std::string parameter = measurement.Parameter();
        std::replace(parameter.begin(), parameter.end(), ',', '_');
        catalog += measurement.name + ',' + parameter;
    }
    AppendResponse(Quote(catalog.empty() ? "NO CATALOG" : catalog));
}

void ScpiEngine::DispatchDeleteMeasurement(const ParsedCommand &command)
{
    if (!CheckArguments(command, 1)) {
        return;
    }
    std::string name;
    if (!StringArgument(command.arguments.front(), name)) {
        RecordCommandError(command, DATA_TYPE_ERROR, "Measurement name requires a string");
        return;
    }
    SetOperation(service_.DeleteMeasurement(name));
}

void ScpiEngine::DispatchDataFormat(const ParsedCommand &command)
{
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse("ASC,0");
        }
        return;
    }
    if (command.arguments.empty() || command.arguments.size() > 2U) {
        RecordCommandError(command, MISSING_PARAMETER, "FORM requires ASCii[,0]");
        return;
    }
    if (!Matches(Upper(command.arguments[0]), "ASC", "ASCII")) {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Only ASCii data format is implemented");
        return;
    }
    if (command.arguments.size() == 2U) {
        unsigned precision = 0;
        if (!Integer(command.arguments[1], precision, 0U)) {
            RecordCommandError(command, DATA_OUT_OF_RANGE, "Only ASCii,0 is implemented");
        }
    }
}

void ScpiEngine::DispatchStimulusAxis(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        BeginEncoding(service_.StimulusAxis(), command);
    }
}

void ScpiEngine::DispatchMeasurementData(const ParsedCommand &command)
{
    if (!CheckArguments(command, 1)) {
        return;
    }
    if (Upper(command.arguments.front()) != "RDATA") {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Only RDATA is implemented");
        return;
    }
    BeginEncoding(service_.MeasurementData(), command);
}

void ScpiEngine::Poll()
{
    service_.Poll();
    PollOperationWatches();
    if (!PollBarrier()) {
        return;
    }
    EncodeBatch();
    if (!encoding_.empty()) {
        return;
    }
    PollMessages();
    if (!connected_ && !HasPending() && control_) {
        service_.ReleaseControl(SCPI_CONTROL_OWNER);
        control_ = false;
    }
}

void ScpiEngine::PollOperationWatches()
{
    for (auto watch = watches_.begin(); watch != watches_.end();) {
        if (watch->generation != service_.StatusGeneration()) {
            watch = watches_.erase(watch);
        } else if (AllReady(watch->operations)) {
            service_.MarkOperationComplete();
            watch = watches_.erase(watch);
        } else {
            ++watch;
        }
    }
}

bool ScpiEngine::PollBarrier()
{
    if (sequential_.valid()) {
        if (!OperationReady(sequential_)) {
            return false;
        }
        sequential_ = VnaService::Operation();
    }
    if (waiting_) {
        if (!AllReady(barrier_)) {
            return false;
        }
        if (waitingQuery_) {
            AppendResponse("+1");
        }
        waiting_ = false;
        barrier_.clear();
    }
    return true;
}

void ScpiEngine::PollMessages()
{
    // 解析与命令分派使用独立于数值编码的处理预算。
    for (unsigned count = 0; count < DISPATCH_BATCH_SIZE && !messages_.empty(); ++count) {
        ScpiMessage &message = messages_.front();
        if (message.position == message.units.size()) {
            if (connected_ && !message.queryFailed && !message.response.empty()) {
                if ((outputBytes_ + message.response.size() + 1U) > RESPONSE_QUEUE_BYTE_LIMIT) {
                    return;
                }
                message.response += '\n';
                outputBytes_ += message.response.size();
                output_.push_back(std::move(message.response));
            }
            messages_.pop_front();
            continue;
        }
        const ParsedCommand command = ParseCommand(message.units[message.position], message.parent);
        ++message.position;
        Dispatch(command);
        if (sequential_.valid() || waiting_ || !encoding_.empty()) {
            break;
        }
    }
}

ScpiEngine::ScpiEngine(VnaService::InstrumentService &service) : service_(service)
{
    // 所有可能分配内存的成员构造成功后，才获取控制权。
    control_ = service.TryAcquireControl(SCPI_CONTROL_OWNER);
}

ScpiEngine::~ScpiEngine() noexcept
{
    // 所有者通常应先等待操作全部完成；空闲引擎也可以直接销毁。
    if (control_ && !HasPending()) {
        service_.ReleaseControl(SCPI_CONTROL_OWNER);
    }
}

bool ScpiEngine::SubmitLine(const std::string &line)
{
    if (!connected_ || !control_ || messages_.size() >= MESSAGE_QUEUE_LIMIT || line.size() > LINE_BUDGET) {
        return false;
    }
    if (Trim(line).empty()) {
        return true;
    }
    ScpiMessage message;
    message.units = SplitProgram(line);
    if (message.units.size() > PROGRAM_UNIT_LIMIT) {
        return false;
    }
    messages_.push_back(std::move(message));
    return true;
}

std::string ScpiEngine::TakeOutput()
{
    if (output_.empty()) {
        return std::string();
    }
    std::string output = std::move(output_.front());
    output_.pop_front();
    outputBytes_ -= output.size();
    return output;
}

bool ScpiEngine::HasControl() const noexcept
{
    return control_;
}

void ScpiEngine::SetTransportOutputPending(bool pending) noexcept
{
    transportOutputPending_ = pending;
}

void ScpiEngine::Disconnect() noexcept
{
    connected_ = false;
    output_.clear();
    outputBytes_ = 0;
    encoding_.clear();
    encodedResponse_.clear();
    for (auto &message : messages_) {
        message.response.clear();
    }
}

bool ScpiEngine::CanReleaseControl() const noexcept
{
    return !connected_ && !control_ && !HasPending();
}

}  // 命名空间 VnaScpi
