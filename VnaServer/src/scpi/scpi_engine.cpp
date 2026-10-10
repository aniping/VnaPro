/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>
#include <vector>

#include "scpi/scpi_constants.h"
#include "scpi/scpi_engine.h"
#include "scpi/scpi_parser.h"
#include "vna_service/instrument_service.h"

namespace vna_scpi {
namespace {
const std::size_t kOutputBudget = 1024U * 1024U;

bool OperationReady(const vna_service::Operation &operation)
{
    return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool AllReady(const std::vector<vna_service::Operation> &operations)
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

bool ReceiverParameter(const std::string &text, vna_service::Measurement &measurement)
{
    const auto comma = text.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    const std::string receiver = trim(text.substr(0, comma));
    unsigned source = 0;
    unsigned port = 0;
    const char kind =
        receiver.empty() ? '\0' : static_cast<char>(std::tolower(static_cast<unsigned char>(receiver.front())));
    if (receiver.size() != 2U || (kind != 'a' && kind != 'b') ||
        !integer(receiver.substr(1), port, RECEIVER_PORT_MAXIMUM) || port == 0U ||
        !integer(text.substr(comma + 1U), source, RECEIVER_PORT_MAXIMUM) || source == 0U) {
        return false;
    }
    measurement.receiverKind = kind;
    measurement.receiverPort = port;
    measurement.sourcePort = source;
    return true;
}

bool Matches(const std::string &value, const char *shortName, const char *fullName)
{
    return value == shortName || value == fullName;
}
}  // namespace

class ScpiEngine::Impl {
public:
    struct Message {
        std::vector<std::string> units;
        std::vector<std::string> parent;
        std::size_t position = 0;
        std::string response;
        bool queryFailed = false;
    };

    struct OpcWatch {
        std::vector<vna_service::Operation> operations;
        unsigned generation = 0;
    };

    explicit Impl(vna_service::InstrumentService &value) : service_(value), control_(value.tryAcquireControl())
    {
    }

    vna_service::InstrumentService &service_;
    bool control_;
    bool connected_ = true;
    bool transportOutputPending_ = false;
    std::deque<Message> messages_;
    std::deque<std::string> output_;
    std::size_t outputBytes_ = 0;
    vna_service::Operation sequential_;
    bool waiting_ = false;
    bool waitingQuery_ = false;
    std::vector<vna_service::Operation> barrier_;
    std::vector<OpcWatch> watches_;
    std::vector<double> encoding_;
    std::size_t encoded_ = 0;
    std::string encodedResponse_;

    bool HasPending() const;

    void RecordCommandError(const ParsedCommand &command, int code, const std::string &text);

    bool CheckArguments(const ParsedCommand &command, std::size_t count);

    void AppendResponse(const std::string &unit);

    void BeginEncoding(vna_service::DataResult data, const ParsedCommand &command);

    void EncodeBatch();

    void SetOperation(vna_service::Operation operation, bool asynchronous = false);

    bool NumericArgument(const ParsedCommand &command, double &value, double minimum, double maximum, bool hz);

    bool QueryNumeric(const ParsedCommand &command, double current, double minimum, double maximum);

    void Dispatch(const ParsedCommand &command);
    bool DispatchCommon(const ParsedCommand &command);
    void DispatchIdentity(const ParsedCommand &command);
    void DispatchClearStatus(const ParsedCommand &command);
    void DispatchOperationComplete(const ParsedCommand &command);
    void DispatchWait(const ParsedCommand &command);
    void DispatchEnableRegister(const ParsedCommand &command);
    void DispatchStatusRegister(const ParsedCommand &command);
    void DispatchNextError(const ParsedCommand &command);
    void DispatchErrorCount(const ParsedCommand &command);

    void Poll();
};

bool ScpiEngine::Impl::HasPending() const
{
    return !messages_.empty() || sequential_.valid() || waiting_ || !encoding_.empty() || !watches_.empty() ||
           service_.hasPending();
}

void ScpiEngine::Impl::RecordCommandError(const ParsedCommand &command, int code, const std::string &text)
{
    service_.recordError(code, text);
    if (command.query && !messages_.empty()) {
        messages_.front().queryFailed = true;
    }
}

bool ScpiEngine::Impl::CheckArguments(const ParsedCommand &command, std::size_t count)
{
    if (command.arguments.size() == count) {
        return true;
    }
    RecordCommandError(command, command.arguments.size() < count ? MISSING_PARAMETER : PARAMETER_NOT_ALLOWED,
        "Parameter count mismatch");
    return false;
}

void ScpiEngine::Impl::AppendResponse(const std::string &unit)
{
    if (messages_.empty() || !connected_) {
        return;
    }
    Message &message = messages_.front();
    if (message.response.size() + unit.size() + 2U > kOutputBudget) {
        service_.recordError(INPUT_BUFFER_OVERRUN, "Response exceeds software output budget");
        message.queryFailed = true;
        return;
    }
    if (!message.response.empty()) {
        message.response += ';';
    }
    message.response += unit;
}

void ScpiEngine::Impl::BeginEncoding(vna_service::DataResult data, const ParsedCommand &command)
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

void ScpiEngine::Impl::EncodeBatch()
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
    if (encodedResponse_.size() > kOutputBudget) {
        service_.recordError(INPUT_BUFFER_OVERRUN, "Array exceeds software output budget");
        messages_.front().queryFailed = true;
        encoding_.clear();
        encodedResponse_.clear();
    } else if (encoded_ == encoding_.size()) {
        AppendResponse(encodedResponse_);
        encoding_.clear();
        encodedResponse_.clear();
    }
}

void ScpiEngine::Impl::SetOperation(vna_service::Operation operation, bool asynchronous)
{
    if (!asynchronous) {
        sequential_ = std::move(operation);
    }
}

bool ScpiEngine::Impl::NumericArgument(
    const ParsedCommand &command, double &value, double minimum, double maximum, bool hz)
{
    if (!CheckArguments(command, 1U)) {
        return false;
    }
    const std::string parameter = upper(command.arguments.front());
    if (parameter == "MIN" || parameter == "MINIMUM") {
        value = minimum;
    } else if (parameter == "MAX" || parameter == "MAXIMUM") {
        value = maximum;
    } else if (!numeric(command.arguments.front(), value, hz)) {
        RecordCommandError(command, DATA_TYPE_ERROR, "Numeric data or unit is invalid");
        return false;
    }
    return true;
}

bool ScpiEngine::Impl::QueryNumeric(const ParsedCommand &command, double current, double minimum, double maximum)
{
    if (command.arguments.empty()) {
        AppendResponse(Number(current));
        return true;
    }
    if (command.arguments.size() == 1U) {
        const auto parameter = upper(command.arguments.front());
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

void ScpiEngine::Impl::Dispatch(const ParsedCommand &command)
{
    const std::string &path = command.path;
    const auto &settings = service_.settings();
    if (command.errorCode != 0) {
        RecordCommandError(command, command.errorCode, command.error);
        return;
    }
    if (DispatchCommon(command)) {
        return;
    }
    if (path == "SENS1:FREQ:STAR" || path == "SENS1:FREQ:STOP") {
        const bool start = path == "SENS1:FREQ:STAR";
        if (command.query) {
            QueryNumeric(
                command, start ? settings.startHz : settings.stopHz, FREQUENCY_MINIMUM_HZ, FREQUENCY_MAXIMUM_HZ);
            return;
        }
        double value = 0;
        if (NumericArgument(command, value, FREQUENCY_MINIMUM_HZ, FREQUENCY_MAXIMUM_HZ, true)) {
            SetOperation(start ? service_.setStartHz(value) : service_.setStopHz(value));
        }
        return;
    }
    if (path == "SENS1:SWE:POIN") {
        if (command.query) {
            if (command.arguments.empty()) {
                AppendResponse(std::to_string(settings.points));
            } else if (command.arguments.size() == 1U && Matches(upper(command.arguments.front()), "MIN", "MINIMUM")) {
                AppendResponse(std::to_string(SWEEP_POINTS_MINIMUM));
            } else if (command.arguments.size() == 1U && Matches(upper(command.arguments.front()), "MAX", "MAXIMUM")) {
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
        SetOperation(service_.setPoints(static_cast<unsigned>(value)));
        return;
    }
    if (path == "SENS1:BAND" || path == "SENS1:BAND:RES" || path == "SENS1:BWID" || path == "SENS1:BWID:RES") {
        if (command.query) {
            QueryNumeric(command, settings.ifBandwidthHz, IF_BANDWIDTH_MINIMUM_HZ, IF_BANDWIDTH_MAXIMUM_HZ);
            return;
        }
        double value = 0;
        if (NumericArgument(command, value, IF_BANDWIDTH_MINIMUM_HZ, IF_BANDWIDTH_MAXIMUM_HZ, true)) {
            SetOperation(service_.setIfBandwidthHz(value));
        }
        return;
    }
    if (path == "SENS1:SWE:TYPE") {
        if (command.query) {
            if (CheckArguments(command, 0)) {
                AppendResponse("LIN");
            }
        } else if (CheckArguments(command, 1)) {
            if (!Matches(upper(command.arguments.front()), "LIN", "LINEAR")) {
                RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Only LINear sweep is supported");
            } else if (settings.sweepMode != vna_service::SweepMode::Hold || service_.hasPending()) {
                RecordCommandError(command, SETTINGS_CONFLICT, "Configuration requires HOLD");
            }
        }
        return;
    }
    if (path == "TRIG:SOUR" || path == "TRIG:SEQ:SOUR" || path == "INIT:CONT" || path == "INIT1:CONT") {
        const bool boolean = path.find("INIT") == 0U;
        if (command.query) {
            if (CheckArguments(command, 0)) {
                AppendResponse(boolean
                                   ? (settings.triggerSource == vna_service::TriggerSource::Immediate ? "1" : "0")
                                   : (settings.triggerSource == vna_service::TriggerSource::Immediate ? "IMM" : "MAN"));
            }
            return;
        }
        if (!CheckArguments(command, 1)) {
            return;
        }
        const std::string value = upper(command.arguments.front());
        vna_service::TriggerSource source;
        if (boolean && (value == "ON" || value == "1")) {
            source = vna_service::TriggerSource::Immediate;
        } else if (boolean && (value == "OFF" || value == "0")) {
            source = vna_service::TriggerSource::Manual;
        } else if (!boolean && Matches(value, "IMM", "IMMEDIATE")) {
            source = vna_service::TriggerSource::Immediate;
        } else if (!boolean && Matches(value, "MAN", "MANUAL")) {
            source = vna_service::TriggerSource::Manual;
        } else {
            RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Unsupported trigger source");
            return;
        }
        const bool asynchronous =
            source == vna_service::TriggerSource::Immediate && settings.sweepMode == vna_service::SweepMode::Single;
        SetOperation(service_.setTriggerSource(source), asynchronous);
        return;
    }
    if (path == "TRIG:SCOP" || path == "TRIG:SEQ:SCOP") {
        if (command.query) {
            if (CheckArguments(command, 0)) {
                AppendResponse(settings.currentTriggerScope ? "CURR" : "ALL");
            }
            return;
        }
        if (!CheckArguments(command, 1)) {
            return;
        }
        const std::string value = upper(command.arguments.front());
        if (value == "ALL") {
            service_.setCurrentTriggerScope(false);
        } else if (Matches(value, "CURR", "CURRENT")) {
            service_.setCurrentTriggerScope(true);
        } else {
            RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Unsupported trigger scope");
        }
        return;
    }
    if (path == "SENS1:SWE:MODE") {
        if (command.query) {
            if (CheckArguments(command, 0)) {
                AppendResponse(settings.sweepMode == vna_service::SweepMode::Hold
                                   ? "HOLD"
                                   : (settings.sweepMode == vna_service::SweepMode::Single ? "SING" : "CONT"));
            }
            return;
        }
        if (!CheckArguments(command, 1)) {
            return;
        }
        const std::string value = upper(command.arguments.front());
        vna_service::SweepMode mode;
        if (value == "HOLD") {
            mode = vna_service::SweepMode::Hold;
        } else if (Matches(value, "CONT", "CONTINUOUS")) {
            mode = vna_service::SweepMode::Continuous;
        } else if (Matches(value, "SING", "SINGLE")) {
            mode = vna_service::SweepMode::Single;
        } else {
            RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Unsupported sweep mode");
            return;
        }
        SetOperation(service_.setSweepMode(mode),
            mode == vna_service::SweepMode::Single && settings.triggerSource == vna_service::TriggerSource::Immediate);
        return;
    }
    if ((path == "INIT" || path == "INIT1" || path == "INIT:IMM" || path == "INIT1:IMM") && !command.query) {
        if (CheckArguments(command, 0)) {
            SetOperation(service_.trigger(), true);
        }
        return;
    }
    if (path == "CALC1:PAR:DEF:EXT" || path == "CALC1:PAR:EXT") {
        if (command.query) {
            RecordCommandError(command, UNDEFINED_HEADER, "Undefined query header");
            return;
        }
        if (!CheckArguments(command, 2)) {
            return;
        }
        vna_service::Measurement measurement;
        std::string parameter;
        if (!StringArgument(command.arguments[0], measurement.name) ||
            !StringArgument(command.arguments[1], parameter)) {
            RecordCommandError(command, DATA_TYPE_ERROR, "Measurement name and parameter require quoted strings");
            return;
        }
        if (!ReceiverParameter(parameter, measurement)) {
            RecordCommandError(
                command, ILLEGAL_PARAMETER_VALUE, "Only logical a1..a4/b1..b4 non-ratio receivers are supported");
            return;
        }
        SetOperation(service_.defineMeasurement(measurement));
        return;
    }
    if (path == "CALC1:PAR:SEL") {
        if (command.query) {
            if (CheckArguments(command, 0)) {
                AppendResponse(quote(settings.selectedMeasurement));
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
        service_.selectMeasurement(name);
        return;
    }
    if (path == "CALC1:PAR:CAT:EXT" && command.query) {
        if (command.arguments.size() > 1U) {
            RecordCommandError(command, PARAMETER_NOT_ALLOWED, "Too many parameters");
            return;
        }
        if (!command.arguments.empty()) {
            const auto value = upper(command.arguments.front());
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
            std::string parameter = measurement.parameter();
            std::replace(parameter.begin(), parameter.end(), ',', '_');
            catalog += measurement.name + ',' + parameter;
        }
        AppendResponse(quote(catalog.empty() ? "NO CATALOG" : catalog));
        return;
    }
    if ((path == "CALC1:PAR:DEL" || path == "CALC1:PAR:DEL:NAME") && !command.query) {
        if (!CheckArguments(command, 1)) {
            return;
        }
        std::string name;
        if (!StringArgument(command.arguments.front(), name)) {
            RecordCommandError(command, DATA_TYPE_ERROR, "Measurement name requires a string");
            return;
        }
        SetOperation(service_.deleteMeasurement(name));
        return;
    }
    if (path == "FORM" || path == "FORM:DATA") {
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
        if (!Matches(upper(command.arguments[0]), "ASC", "ASCII")) {
            RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Only ASCii data format is implemented");
            return;
        }
        if (command.arguments.size() == 2U) {
            unsigned precision = 0;
            if (!integer(command.arguments[1], precision, 0U)) {
                RecordCommandError(command, DATA_OUT_OF_RANGE, "Only ASCii,0 is implemented");
            }
        }
        return;
    }
    if ((path == "SENS1:X" || path == "SENS1:X:VAL") && command.query) {
        if (CheckArguments(command, 0)) {
            BeginEncoding(service_.stimulusAxis(), command);
        }
        return;
    }
    if (path == "CALC1:DATA" && command.query) {
        if (!CheckArguments(command, 1)) {
            return;
        }
        if (upper(command.arguments.front()) != "RDATA") {
            RecordCommandError(command, ILLEGAL_PARAMETER_VALUE, "Only RDATA is implemented");
            return;
        }
        BeginEncoding(service_.measurementData(), command);
        return;
    }
    RecordCommandError(command, UNDEFINED_HEADER, "Undefined header");
}

bool ScpiEngine::Impl::DispatchCommon(const ParsedCommand &command)
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

void ScpiEngine::Impl::DispatchIdentity(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        AppendResponse(service_.identity());
    }
}

void ScpiEngine::Impl::DispatchClearStatus(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        service_.clearStatus();
    }
}

void ScpiEngine::Impl::DispatchOperationComplete(const ParsedCommand &command)
{
    if (!CheckArguments(command, 0)) {
        return;
    }
    if (command.query) {
        barrier_ = service_.pendingOperations();
        waiting_ = true;
        waitingQuery_ = true;
    } else {
        if (watches_.size() >= OPC_WATCH_LIMIT) {
            RecordCommandError(command, INPUT_BUFFER_OVERRUN, "OPC pending buffer overrun");
            return;
        }
        OpcWatch watch;
        watch.operations = service_.pendingOperations();
        watch.generation = service_.statusGeneration();
        if (AllReady(watch.operations)) {
            service_.markOperationComplete();
        } else {
            watches_.push_back(std::move(watch));
        }
    }
}

void ScpiEngine::Impl::DispatchWait(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        barrier_ = service_.pendingOperations();
        waiting_ = true;
        waitingQuery_ = false;
    }
}

void ScpiEngine::Impl::DispatchEnableRegister(const ParsedCommand &command)
{
    const std::string &path = command.path;
    if (command.query) {
        if (CheckArguments(command, 0)) {
            AppendResponse(std::to_string(path == "*ESE" ? service_.eventEnable() : service_.serviceRequestEnable()));
        }
    } else {
        unsigned mask = 0;
        if (!CheckArguments(command, 1)) {
            return;
        }
        if (!integer(command.arguments.front(), mask, REGISTER_MASK_MAXIMUM)) {
            RecordCommandError(command, DATA_OUT_OF_RANGE, "Register mask outside 0..255");
            return;
        }
        if (path == "*ESE") {
            service_.setEventEnable(mask);
        } else {
            service_.setServiceRequestEnable(mask);
        }
    }
}

void ScpiEngine::Impl::DispatchStatusRegister(const ParsedCommand &command)
{
    const std::string &path = command.path;
    if (CheckArguments(command, 0)) {
        AppendResponse(
            std::to_string(path == "*ESR" ? service_.readEventRegister()
                                          : service_.statusByte(!output_.empty() || transportOutputPending_)));
    }
}

void ScpiEngine::Impl::DispatchNextError(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        const auto value = service_.popError();
        AppendResponse(std::to_string(value.code) + ',' + quote(value.message));
    }
}

void ScpiEngine::Impl::DispatchErrorCount(const ParsedCommand &command)
{
    if (CheckArguments(command, 0)) {
        AppendResponse(std::to_string(service_.errorCount()));
    }
}

void ScpiEngine::Impl::Poll()
{
    service_.poll();
    for (auto watch = watches_.begin(); watch != watches_.end();) {
        if (watch->generation != service_.statusGeneration()) {
            watch = watches_.erase(watch);
        } else if (AllReady(watch->operations)) {
            service_.markOperationComplete();
            watch = watches_.erase(watch);
        } else {
            ++watch;
        }
    }
    if (sequential_.valid()) {
        if (!OperationReady(sequential_)) {
            return;
        }
        sequential_ = vna_service::Operation();
    }
    if (waiting_) {
        if (!AllReady(barrier_)) {
            return;
        }
        if (waitingQuery_) {
            AppendResponse("+1");
        }
        waiting_ = false;
        barrier_.clear();
    }
    EncodeBatch();
    if (!encoding_.empty()) {
        return;
    }
    // Parsing/dispatching is bounded independently of numeric encoding.
    for (unsigned count = 0; count < 32U && !messages_.empty(); ++count) {
        Message &message = messages_.front();
        if (message.position == message.units.size()) {
            if (connected_ && !message.queryFailed && !message.response.empty()) {
                if (outputBytes_ + message.response.size() + 1U > kOutputBudget) {
                    return;
                }
                message.response += '\n';
                outputBytes_ += message.response.size();
                output_.push_back(std::move(message.response));
            }
            messages_.pop_front();
            continue;
        }
        const ParsedCommand command = parseCommand(message.units[message.position++], message.parent);
        Dispatch(command);
        if (sequential_.valid() || waiting_ || !encoding_.empty()) {
            break;
        }
    }
    if (!connected_ && !HasPending() && control_) {
        service_.releaseControl();
        control_ = false;
    }
}

ScpiEngine::ScpiEngine(vna_service::InstrumentService &service_) : impl_(new Impl(service_))
{
}

ScpiEngine::~ScpiEngine()
{
    // The owner normally drains first. An idle engine may also be destroyed directly.
    if (impl_->control_ && !impl_->HasPending()) {
        impl_->service_.releaseControl();
    }
}

bool ScpiEngine::submitLine(const std::string &line)
{
    if (!impl_->connected_ || !impl_->control_ || impl_->messages_.size() >= 32U || line.size() > 16384U) {
        return false;
    }
    if (trim(line).empty()) {
        return true;
    }
    Impl::Message message;
    message.units = splitProgram(line);
    if (message.units.size() > 128U) {
        return false;
    }
    impl_->messages_.push_back(std::move(message));
    return true;
}

void ScpiEngine::poll()
{
    impl_->Poll();
}

std::string ScpiEngine::takeOutput()
{
    if (impl_->output_.empty()) {
        return std::string();
    }
    std::string output_ = std::move(impl_->output_.front());
    impl_->output_.pop_front();
    impl_->outputBytes_ -= output_.size();
    return output_;
}

bool ScpiEngine::hasPending() const
{
    return impl_->HasPending();
}

bool ScpiEngine::hasControl() const
{
    return impl_->control_;
}

void ScpiEngine::setTransportOutputPending(bool pending)
{
    impl_->transportOutputPending_ = pending;
}

void ScpiEngine::disconnect()
{
    impl_->connected_ = false;
    impl_->output_.clear();
    impl_->outputBytes_ = 0;
    impl_->encoding_.clear();
    impl_->encodedResponse_.clear();
    for (auto &message : impl_->messages_) {
        message.response.clear();
    }
}

bool ScpiEngine::canReleaseControl() const
{
    return !impl_->connected_ && !impl_->control_ && !impl_->HasPending();
}

}  // namespace vna_scpi
