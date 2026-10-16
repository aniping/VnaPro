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

#include "scpi/scpi_calibration_adapter.h"
#include "scpi/scpi_command_registry.h"
#include "scpi/scpi_constants.h"
#include "scpi/scpi_display_adapter.h"
#include "scpi/scpi_parser.h"
#include "scpi/scpi_query_policy.h"
#include "vna_core/sweep_plan.h"
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

bool Matches(const std::string &value, const char *shortName, const char *fullName)
{
    return value == shortName || value == fullName;
}
}  // 匿名命名空间

bool ScpiEngine::HasPending() const noexcept
{
    return !messages_.empty() || sequential_.valid() || waiting_ || encoder_.Pending() || !watches_.empty() ||
           (control_ && service_.HasPending());
}

void ScpiEngine::RecordCommandError(const ParsedCommand &command, int code, const std::string &text)
{
    if (control_) {
        service_.RecordError(code, text);
    } else {
        localError_.code = code;
        localError_.message = text;
    }
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
        RecordCommandError(ParsedCommand{}, INPUT_BUFFER_OVERRUN, "Response exceeds software output budget");
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
    if (!encoder_.Begin(std::move(data.values), format_, byteOrder_)) {
        RecordCommandError(command, DATA_CORRUPT_OR_STALE, "Empty, invalid or oversized numeric data");
    }
}

void ScpiEngine::EncodeBatch()
{
    if (!encoder_.Pending()) {
        return;
    }
    if (!encoder_.Poll()) {
        RecordCommandError(ParsedCommand{}, INPUT_BUFFER_OVERRUN, "Array exceeds software output budget");
        messages_.front().queryFailed = true;
    } else if (!encoder_.Pending()) {
        AppendResponse(encoder_.TakeOutput());
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

void ScpiEngine::Dispatch(const ParsedCommand &original)
{
    auto command = RouteChannelCommand(original);
    if (command.errorCode != 0) {
        RecordCommandError(command, command.errorCode, command.error);
        return;
    }
    if (command.channelId != 0U && !service_.HasChannel(command.channelId)) {
        RecordCommandError(command, DATA_OUT_OF_RANGE, "Channel does not exist");
        return;
    }
    if (command.channelId == 0U && (command.path.find("TRIG") == 0U || command.path.find("INIT") == 0U)) {
        command.channelId = service_.ActiveChannelId();
    }
    if (DispatchRegistered(command)) {
        return;
    }
    const auto calibration = ScpiCalibrationAdapter(service_).Execute(command);
    if (calibration.matched) {
        if (calibration.errorCode != 0) {
            RecordCommandError(command, calibration.errorCode, calibration.error);
        } else if (calibration.operation.valid()) {
            SetOperation(calibration.operation);
        } else if (!calibration.response.empty()) {
            AppendResponse(calibration.response);
        }
        return;
    }
    const auto display = ScpiDisplayAdapter(service_).Execute(command);
    if (display.matched) {
        if (display.errorCode != 0) {
            RecordCommandError(command, display.errorCode, display.error);
        } else if (display.operation.valid()) {
            SetOperation(display.operation);
        } else if (!display.response.empty()) {
            AppendResponse(display.response);
        }
        return;
    }
    RecordCommandError(command, UNDEFINED_HEADER, "Undefined header");
}

bool ScpiEngine::DispatchRegistered(const ParsedCommand &command)
{
    const auto *definition = FindCommand(command.path);
    if (definition == nullptr) {
        return false;
    }
    if (command.query ? !definition->query : !definition->setter) {
        RecordCommandError(command, UNDEFINED_HEADER, "Unsupported query or setter header");
        return true;
    }
    switch (definition->handler) {
        case CommandHandler::IDENTITY:
            DispatchIdentity(command);
            break;
        case CommandHandler::CLEAR_STATUS:
            DispatchClearStatus(command);
            break;
        case CommandHandler::RESET:
            DispatchReset(command);
            break;
        case CommandHandler::OPERATION_COMPLETE:
            DispatchOperationComplete(command);
            break;
        case CommandHandler::WAIT:
            DispatchWait(command);
            break;
        case CommandHandler::ENABLE_REGISTER:
            DispatchEnableRegister(command);
            break;
        case CommandHandler::STATUS_REGISTER:
            DispatchStatusRegister(command);
            break;
        case CommandHandler::NEXT_ERROR:
            DispatchNextError(command);
            break;
        case CommandHandler::ERROR_COUNT:
            DispatchErrorCount(command);
            break;
        case CommandHandler::FREQUENCY:
            DispatchFrequency(command);
            break;
        case CommandHandler::POINTS:
            DispatchPoints(command);
            break;
        case CommandHandler::BANDWIDTH:
            DispatchBandwidth(command);
            break;
        case CommandHandler::POWER:
            DispatchPower(command);
            break;
        case CommandHandler::SWEEP_TYPE:
            DispatchSweepType(command);
            break;
        case CommandHandler::TRIGGER_SOURCE:
            DispatchTriggerSource(command);
            break;
        case CommandHandler::TRIGGER_SCOPE:
            DispatchTriggerScope(command);
            break;
        case CommandHandler::SWEEP_MODE:
            DispatchSweepMode(command);
            break;
        case CommandHandler::INITIATE:
            DispatchInitiate(command);
            break;
        case CommandHandler::DEFINE_MEASUREMENT:
            DispatchDefineMeasurement(command);
            break;
        case CommandHandler::MEASUREMENT_CATALOG:
            DispatchMeasurementCatalog(command);
            break;
        case CommandHandler::DELETE_MEASUREMENT:
            DispatchDeleteMeasurement(command);
            break;
        case CommandHandler::DELETE_ALL_MEASUREMENTS:
            DispatchDeleteAllMeasurements(command);
            break;
        case CommandHandler::DATA_FORMAT:
            DispatchDataFormat(command);
            break;
        case CommandHandler::BYTE_ORDER:
            DispatchByteOrder(command);
            break;
        case CommandHandler::STIMULUS_AXIS:
            DispatchStimulusAxis(command);
            break;
        case CommandHandler::MEASUREMENT_DATA:
            DispatchMeasurementData(command);
            break;
        case CommandHandler::MULTIPLE_MEASUREMENT_DATA:
            DispatchMultipleMeasurementData(command);
            break;
    }
    return true;
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

void ScpiEngine::DispatchReset(const ParsedCommand &command)
{
    if (!CheckArguments(command, 0U)) {
        return;
    }
    // 服务 FIFO 顺序覆盖所有已存在通道；最后一个 future 也是前序操作的屏障。
    const auto channels = service_.Snapshot().channels;
    for (std::size_t index = 0U; index < channels.size(); ++index) {
        SetOperation(service_.ResetMeasurements("SCPI", channels[index].id, index + 1U == channels.size()));
    }
    format_ = NumericFormat::ASCII;
    byteOrder_ = ByteOrder::NORMAL;
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
    const auto &settings = service_.Settings(command.channelId);
    const auto &profile = service_.Profile();
    const bool start = path == "SENS1:FREQ:STAR";
    if (command.query) {
        QueryNumeric(
            command, start ? settings.startHz : settings.stopHz, profile.minFrequencyHz, profile.maxFrequencyHz);
        return;
    }
    double value = 0;
    if (NumericArgument(command, value, profile.minFrequencyHz, profile.maxFrequencyHz, true)) {
        SetOperation(start ? service_.SetStartHz(value, 0U, command.channelId)
                           : service_.SetStopHz(value, 0U, command.channelId));
    }
}

void ScpiEngine::DispatchPoints(const ParsedCommand &command)
{
    const auto &settings = service_.Settings(command.channelId);
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
    SetOperation(service_.SetPoints(static_cast<unsigned>(value), 0U, command.channelId));
}

void ScpiEngine::DispatchBandwidth(const ParsedCommand &command)
{
    const auto &settings = service_.Settings(command.channelId);
    if (command.query) {
        QueryNumeric(command, settings.ifBandwidthHz, IF_BANDWIDTH_MINIMUM_HZ, IF_BANDWIDTH_MAXIMUM_HZ);
        return;
    }
    double value = 0;
    if (NumericArgument(command, value, IF_BANDWIDTH_MINIMUM_HZ, IF_BANDWIDTH_MAXIMUM_HZ, true)) {
        SetOperation(service_.SetIfBandwidthHz(value, 0U, command.channelId));
    }
}

void ScpiEngine::DispatchSweepType(const ParsedCommand &command)
{
    const auto &settings = service_.Settings(command.channelId);
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

void ScpiEngine::DispatchPower(const ParsedCommand &command)
{
    if (command.query) {
        QueryNumeric(
            command, service_.Settings(command.channelId).powerDbm, VnaCore::MIN_POWER_DBM, VnaCore::MAX_POWER_DBM);
        return;
    }
    double value = 0.0;
    if (NumericArgument(command, value, VnaCore::MIN_POWER_DBM, VnaCore::MAX_POWER_DBM, false)) {
        VnaService::ConfigPatch patch;
        patch.hasPowerDbm = true;
        patch.powerDbm = value;
        SetOperation(service_.ApplyConfiguration(patch, 0U, "SCPI", command.channelId));
    }
}

void ScpiEngine::DispatchTriggerSource(const ParsedCommand &command)
{
    const std::string &path = command.path;
    const auto &settings = service_.Settings(command.channelId);
    const bool boolean = path.find("INIT") == 0U;
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse(boolean ? (settings.selectedSweepMode == VnaService::SweepMode::CONTINUOUS &&
                                                 settings.triggerSource == VnaService::TriggerSource::IMMEDIATE
                                             ? "1"
                                             : "0")
                                   : (settings.triggerSource == VnaService::TriggerSource::IMMEDIATE ? "IMM" : "MAN"));
        }
        return;
    }
    if (!CheckArguments(command, 1)) {
        return;
    }
    const std::string value = Upper(command.arguments.front());
    if (boolean) {
        if (value == "OFF" || value == "0") {
            SetOperation(service_.SelectSingleAndHold("SCPI", command.channelId));
        } else if (value == "ON" || value == "1") {
            SetOperation(service_.RunContinuous("SCPI", command.channelId));
        } else {
            RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Continuous initiation accepts ON/OFF or 1/0");
        }
        return;
    }
    VnaService::TriggerSource source = VnaService::TriggerSource::MANUAL;
    if (Matches(value, "IMM", "IMMEDIATE")) {
        source = VnaService::TriggerSource::IMMEDIATE;
    } else if (Matches(value, "MAN", "MANUAL")) {
        source = VnaService::TriggerSource::MANUAL;
    } else {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Unsupported trigger source");
        return;
    }
    const bool asynchronous =
        source == VnaService::TriggerSource::IMMEDIATE && settings.sweepMode == VnaService::SweepMode::SINGLE;
    SetOperation(service_.SetTriggerSource(source, "SCPI", command.channelId), asynchronous);
}

void ScpiEngine::DispatchTriggerScope(const ParsedCommand &command)
{
    const auto &settings = service_.Settings(command.channelId);
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse(settings.currentTriggerScope || service_.Snapshot().channels.size() > 1U ? "CURR" : "ALL");
        }
        return;
    }
    if (!CheckArguments(command, 1)) {
        return;
    }
    const std::string value = Upper(command.arguments.front());
    if (value == "ALL") {
        if (service_.Snapshot().channels.size() > 1U) {
            RecordCommandError(command, SETTINGS_CONFLICT, "All-channel trigger scope is not implemented");
        } else {
            service_.SetCurrentTriggerScope(false, command.channelId);
        }
    } else if (Matches(value, "CURR", "CURRENT")) {
        service_.SetCurrentTriggerScope(true, command.channelId);
    } else {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Unsupported trigger scope");
    }
}

void ScpiEngine::DispatchSweepMode(const ParsedCommand &command)
{
    const auto &settings = service_.Settings(command.channelId);
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
    SetOperation(service_.SetSweepMode(mode, "SCPI", command.channelId),
        mode == VnaService::SweepMode::SINGLE && settings.triggerSource == VnaService::TriggerSource::IMMEDIATE);
}

void ScpiEngine::DispatchInitiate(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        const bool stopped = service_.Settings(command.channelId).sweepMode == VnaService::SweepMode::HOLD;
        SetOperation(
            stopped ? service_.RunSingle("SCPI", command.channelId) : service_.Trigger("SCPI", command.channelId),
            true);
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
    if (!measurement.SetParameter(parameter)) {
        RecordCommandError(
            command, ILLEGAL_PARAMETER_VALUE, "Supported parameters are Sij (i,j=1..4) and aN,P/bN,P (N,P=1..4)");
        return;
    }
    SetOperation(service_.DefineMeasurement(measurement, 0U, "SCPI", command.channelId));
}

void ScpiEngine::DispatchMeasurementCatalog(const ParsedCommand &command)
{
    const auto &settings = service_.Settings(command.channelId);
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
        if (measurement.channelId != command.channelId) {
            continue;
        }
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
    SetOperation(service_.DeleteMeasurement(name, 0U, "SCPI", command.channelId));
}

void ScpiEngine::DispatchDataFormat(const ParsedCommand &command)
{
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse(format_ == NumericFormat::ASCII ? "ASC,0" : "REAL,64");
        }
        return;
    }
    if (command.arguments.empty() || command.arguments.size() > 2U) {
        RecordCommandError(command, command.arguments.empty() ? MISSING_PARAMETER : PARAMETER_NOT_ALLOWED,
            "FORM requires ASCii[,0] or REAL,64");
        return;
    }
    const auto type = Upper(command.arguments[0]);
    const bool ascii = Matches(type, "ASC", "ASCII");
    if (!ascii && type != "REAL") {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Supported numeric formats are ASCii and REAL,64");
        return;
    }
    if (!ascii && command.arguments.size() != 2U) {
        RecordCommandError(command, MISSING_PARAMETER, "REAL format requires width 64");
        return;
    }
    if (command.arguments.size() == 2U) {
        unsigned precision = 0;
        if (!Integer(command.arguments[1], precision, 64U) || precision != (ascii ? 0U : 64U)) {
            RecordCommandError(command, DATA_OUT_OF_RANGE, "Supported numeric formats are ASCii,0 and REAL,64");
            return;
        }
    }
    format_ = ascii ? NumericFormat::ASCII : NumericFormat::REAL64;
}

void ScpiEngine::DispatchByteOrder(const ParsedCommand &command)
{
    if (!CheckArguments(command, command.query ? 0U : 1U)) {
        return;
    }
    if (command.query) {
        AppendResponse(byteOrder_ == ByteOrder::NORMAL ? "NORM" : "SWAP");
        return;
    }
    const auto order = Upper(command.arguments.front());
    if (Matches(order, "NORM", "NORMAL")) {
        byteOrder_ = ByteOrder::NORMAL;
    } else if (Matches(order, "SWAP", "SWAPPED")) {
        byteOrder_ = ByteOrder::SWAPPED;
    } else {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Byte order requires NORMal or SWAPped");
    }
}

void ScpiEngine::DispatchDeleteAllMeasurements(const ParsedCommand &command)
{
    if (CheckArguments(command, 0U)) {
        SetOperation(service_.DeleteAllMeasurements("SCPI", command.channelId));
    }
}

void ScpiEngine::DispatchMultipleMeasurementData(const ParsedCommand &command)
{
    if (!CheckArguments(command, 1U)) {
        return;
    }
    std::string selection;
    if (!StringArgument(command.arguments.front(), selection)) {
        RecordCommandError(command, DATA_TYPE_ERROR, "Measurement numbers require a quoted comma-separated list");
        return;
    }
    std::vector<unsigned> numbers;
    std::size_t begin = 0U;
    do {
        const auto separator = selection.find(',', begin);
        const auto item = Trim(selection.substr(begin, separator - begin));
        unsigned number = 0U;
        if (item.empty() || item.find_first_not_of("0123456789") != std::string::npos ||
            !Integer(item, number, VnaDisplay::MAX_DISPLAY_TRACES) || number == 0U ||
            numbers.size() >= VnaDisplay::MAX_DISPLAY_TRACES) {
            RecordCommandError(
                command, ILLEGAL_PARAMETER_VALUE, "Measurement selection requires positive catalog numbers");
            return;
        }
        numbers.push_back(number);
        if (separator == std::string::npos) {
            break;
        }
        begin = separator + 1U;
    } while (true);
    BeginEncoding(service_.MeasurementData(numbers, true, command.channelId), command);
}

void ScpiEngine::DispatchStimulusAxis(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        BeginEncoding(service_.StimulusAxis(command.channelId), command);
    }
}

void ScpiEngine::DispatchMeasurementData(const ParsedCommand &command)
{
    if (!CheckArguments(command, 1)) {
        return;
    }
    const auto kind = Upper(command.arguments.front());
    if (kind != "RDATA" && kind != "SDATA") {
        RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Only RDATA and SDATA are implemented");
        return;
    }
    BeginEncoding(service_.MeasurementData(kind == "SDATA", command.channelId), command);
}

void ScpiEngine::Poll()
{
    if (control_) {
        service_.Poll();
    }
    PollOperationWatches();
    if (!PollBarrier()) {
        return;
    }
    EncodeBatch();
    if (encoder_.Pending()) {
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
        if (sequential_.valid() || waiting_ || encoder_.Pending()) {
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
    if (!connected_ || messages_.size() >= MESSAGE_QUEUE_LIMIT || line.size() > LINE_BUDGET) {
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
    if (!control_ && !ObserverMessageAllowed(message)) {
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

const VnaService::InstrumentError &ScpiEngine::LocalError() const noexcept
{
    return localError_;
}

bool ScpiEngine::ObserverMessageAllowed(const ScpiMessage &message)
{
    std::vector<std::string> parent;
    for (const auto &unit : message.units) {
        const auto command = ParseCommand(unit, parent);
        if (!IsObserverQuery(command)) {
            const bool destructive = !command.query || command.path == "*ESR" || command.path == "SYST:ERR" ||
                                     command.path == "SYST:ERR:NEXT";
            const int code =
                command.errorCode != 0 ? command.errorCode : (destructive ? SETTINGS_CONFLICT : UNDEFINED_HEADER);
            RecordCommandError(command, code,
                command.errorCode != 0
                    ? command.error
                    : (destructive ? "Observer connection cannot change instrument state" : "Undefined header"));
            return false;
        }
    }
    return true;
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
    encoder_.Clear();
    for (auto &message : messages_) {
        message.response.clear();
    }
}

bool ScpiEngine::CanReleaseControl() const noexcept
{
    return !connected_ && !control_ && !HasPending();
}

}  // 命名空间 VnaScpi
