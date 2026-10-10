#include "scpi/scpi_engine.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <deque>
#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>
#include <vector>

#include "scpi/scpi_parser.h"
#include "vna_service/instrument_service.h"

namespace vna_scpi {
namespace {
const std::size_t kOutputBudget = 1024U * 1024U;

bool operationReady(const vna_service::Operation &operation)
{
    return operation.wait_for(std::chrono::seconds(0)) == std::future_status::ready;
}

bool allReady(const std::vector<vna_service::Operation> &operations)
{
    for (const auto &operation : operations) {
        if (!operationReady(operation)) {
            return false;
        }
    }
    return true;
}

std::string number(double value)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setprecision(17) << std::scientific << value;
    return stream.str();
}

bool stringArgument(const std::string &text, std::string &value)
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

bool receiverParameter(const std::string &text, vna_service::Measurement &measurement)
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
    if (receiver.size() != 2U || (kind != 'a' && kind != 'b') || !integer(receiver.substr(1), port, 4U) || port == 0U ||
        !integer(text.substr(comma + 1U), source, 4U) || source == 0U) {
        return false;
    }
    measurement.receiverKind = kind;
    measurement.receiverPort = port;
    measurement.sourcePort = source;
    return true;
}

bool matches(const std::string &value, const char *shortName, const char *fullName)
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

    explicit Impl(vna_service::InstrumentService &value) : service(value), control(value.tryAcquireControl())
    {
    }

    vna_service::InstrumentService &service;
    bool control;
    bool connected = true;
    bool transportOutputPending = false;
    std::deque<Message> messages;
    std::deque<std::string> output;
    std::size_t outputBytes = 0;
    vna_service::Operation sequential;
    bool waiting = false;
    bool waitingQuery = false;
    std::vector<vna_service::Operation> barrier;
    std::vector<OpcWatch> watches;
    std::vector<double> encoding;
    std::size_t encoded = 0;
    std::string encodedResponse;

    bool pending() const
    {
        return !messages.empty() || sequential.valid() || waiting || !encoding.empty() || !watches.empty() ||
               service.hasPending();
    }

    void error(const ParsedCommand &command, int code, const std::string &text)
    {
        service.recordError(code, text);
        if (command.query && !messages.empty()) {
            messages.front().queryFailed = true;
        }
    }

    bool arguments(const ParsedCommand &command, std::size_t count)
    {
        if (command.arguments.size() == count) {
            return true;
        }
        error(command, command.arguments.size() < count ? -109 : -108, "Parameter count mismatch");
        return false;
    }

    void response(const std::string &unit)
    {
        if (messages.empty() || !connected) {
            return;
        }
        Message &message = messages.front();
        if (message.response.size() + unit.size() + 2U > kOutputBudget) {
            service.recordError(-363, "Response exceeds software output budget");
            message.queryFailed = true;
            return;
        }
        if (!message.response.empty()) {
            message.response += ';';
        }
        message.response += unit;
    }

    void beginEncoding(vna_service::DataResult data, const ParsedCommand &command)
    {
        if (!data.ok) {
            error(command, -230, data.error);
            return;
        }
        if (!connected) {
            return;
        }
        encoding = std::move(data.values);
        encoded = 0;
        encodedResponse.clear();
        if (encoding.empty()) {
            error(command, -230, "Empty data array");
        }
    }

    void encodeBatch()
    {
        if (encoding.empty()) {
            return;
        }
        const std::size_t end = (std::min)(encoded + 256U, encoding.size());
        for (; encoded < end; ++encoded) {
            if (encoded != 0U) {
                encodedResponse += ',';
            }
            encodedResponse += number(encoding[encoded]);
        }
        if (encodedResponse.size() > kOutputBudget) {
            service.recordError(-363, "Array exceeds software output budget");
            messages.front().queryFailed = true;
            encoding.clear();
            encodedResponse.clear();
        } else if (encoded == encoding.size()) {
            response(encodedResponse);
            encoding.clear();
            encodedResponse.clear();
        }
    }

    void setOperation(vna_service::Operation operation, bool asynchronous = false)
    {
        if (!asynchronous) {
            sequential = std::move(operation);
        }
    }

    bool numericArgument(const ParsedCommand &command, double &value, double minimum, double maximum, bool hz)
    {
        if (!arguments(command, 1U)) {
            return false;
        }
        const std::string parameter = upper(command.arguments.front());
        if (parameter == "MIN" || parameter == "MINIMUM") {
            value = minimum;
        } else if (parameter == "MAX" || parameter == "MAXIMUM") {
            value = maximum;
        } else if (!numeric(command.arguments.front(), value, hz)) {
            error(command, -104, "Numeric data or unit is invalid");
            return false;
        }
        return true;
    }

    bool queryNumeric(const ParsedCommand &command, double current, double minimum, double maximum)
    {
        if (command.arguments.empty()) {
            response(number(current));
            return true;
        }
        if (command.arguments.size() == 1U) {
            const auto parameter = upper(command.arguments.front());
            if (matches(parameter, "MIN", "MINIMUM")) {
                response(number(minimum));
                return true;
            }
            if (matches(parameter, "MAX", "MAXIMUM")) {
                response(number(maximum));
                return true;
            }
        }
        error(command, -224, "Query accepts only MINimum or MAXimum");
        return false;
    }

    void dispatch(const ParsedCommand &command)
    {
        const std::string &path = command.path;
        const auto &settings = service.settings();
        if (command.errorCode != 0) {
            error(command, command.errorCode, command.error);
            return;
        }
        if (path == "*IDN" && command.query) {
            if (arguments(command, 0)) {
                response(service.identity());
            }
            return;
        }
        if (path == "*CLS" && !command.query) {
            if (arguments(command, 0)) {
                service.clearStatus();
            }
            return;
        }
        if (path == "*OPC") {
            if (!arguments(command, 0)) {
                return;
            }
            if (command.query) {
                barrier = service.pendingOperations();
                waiting = true;
                waitingQuery = true;
            } else {
                if (watches.size() >= 32U) {
                    error(command, -363, "OPC pending buffer overrun");
                    return;
                }
                OpcWatch watch;
                watch.operations = service.pendingOperations();
                watch.generation = service.statusGeneration();
                if (allReady(watch.operations)) {
                    service.markOperationComplete();
                } else {
                    watches.push_back(std::move(watch));
                }
            }
            return;
        }
        if (path == "*WAI" && !command.query) {
            if (arguments(command, 0)) {
                barrier = service.pendingOperations();
                waiting = true;
                waitingQuery = false;
            }
            return;
        }
        if (path == "*ESE" || path == "*SRE") {
            if (command.query) {
                if (arguments(command, 0)) {
                    response(std::to_string(path == "*ESE" ? service.eventEnable() : service.serviceRequestEnable()));
                }
            } else {
                unsigned mask = 0;
                if (!arguments(command, 1)) {
                    return;
                }
                if (!integer(command.arguments.front(), mask, 255U)) {
                    error(command, -222, "Register mask outside 0..255");
                    return;
                }
                if (path == "*ESE") {
                    service.setEventEnable(mask);
                } else {
                    service.setServiceRequestEnable(mask);
                }
            }
            return;
        }
        if ((path == "*ESR" || path == "*STB") && command.query) {
            if (arguments(command, 0)) {
                response(
                    std::to_string(path == "*ESR" ? service.readEventRegister()
                                                  : service.statusByte(!output.empty() || transportOutputPending)));
            }
            return;
        }
        if ((path == "SYST:ERR" || path == "SYST:ERR:NEXT") && command.query) {
            if (arguments(command, 0)) {
                const auto value = service.popError();
                response(std::to_string(value.code) + ',' + quote(value.message));
            }
            return;
        }
        if (path == "SYST:ERR:COUN" && command.query) {
            if (arguments(command, 0)) {
                response(std::to_string(service.errorCount()));
            }
            return;
        }
        if (path == "SENS1:FREQ:STAR" || path == "SENS1:FREQ:STOP") {
            const bool start = path == "SENS1:FREQ:STAR";
            if (command.query) {
                queryNumeric(command, start ? settings.startHz : settings.stopHz, 1.0, 1000000000.0);
                return;
            }
            double value = 0;
            if (numericArgument(command, value, 1.0, 1000000000.0, true)) {
                setOperation(start ? service.setStartHz(value) : service.setStopHz(value));
            }
            return;
        }
        if (path == "SENS1:SWE:POIN") {
            if (command.query) {
                if (command.arguments.empty()) {
                    response(std::to_string(settings.points));
                } else if (command.arguments.size() == 1U &&
                           matches(upper(command.arguments.front()), "MIN", "MINIMUM")) {
                    response("2");
                } else if (command.arguments.size() == 1U &&
                           matches(upper(command.arguments.front()), "MAX", "MAXIMUM")) {
                    response("10001");
                } else {
                    error(command, -224, "Query accepts only MINimum or MAXimum");
                }
                return;
            }
            double value = 0;
            if (!numericArgument(command, value, 2.0, 10001.0, false)) {
                return;
            }
            if (value < 2.0 || value > 10001.0 || value != static_cast<unsigned>(value)) {
                error(command, -222, "Point count outside 2..10001");
                return;
            }
            setOperation(service.setPoints(static_cast<unsigned>(value)));
            return;
        }
        if (path == "SENS1:BAND" || path == "SENS1:BAND:RES" || path == "SENS1:BWID" || path == "SENS1:BWID:RES") {
            if (command.query) {
                queryNumeric(command, settings.ifBandwidthHz, 10.0, 100000.0);
                return;
            }
            double value = 0;
            if (numericArgument(command, value, 10.0, 100000.0, true)) {
                setOperation(service.setIfBandwidthHz(value));
            }
            return;
        }
        if (path == "SENS1:SWE:TYPE") {
            if (command.query) {
                if (arguments(command, 0)) {
                    response("LIN");
                }
            } else if (arguments(command, 1)) {
                if (!matches(upper(command.arguments.front()), "LIN", "LINEAR")) {
                    error(command, -224, "Only LINear sweep is supported");
                } else if (settings.sweepMode != vna_service::SweepMode::Hold || service.hasPending()) {
                    error(command, -221, "Configuration requires HOLD");
                }
            }
            return;
        }
        if (path == "TRIG:SOUR" || path == "TRIG:SEQ:SOUR" || path == "INIT:CONT" || path == "INIT1:CONT") {
            const bool boolean = path.find("INIT") == 0U;
            if (command.query) {
                if (arguments(command, 0)) {
                    response(boolean
                                 ? (settings.triggerSource == vna_service::TriggerSource::Immediate ? "1" : "0")
                                 : (settings.triggerSource == vna_service::TriggerSource::Immediate ? "IMM" : "MAN"));
                }
                return;
            }
            if (!arguments(command, 1)) {
                return;
            }
            const std::string value = upper(command.arguments.front());
            vna_service::TriggerSource source;
            if (boolean && (value == "ON" || value == "1")) {
                source = vna_service::TriggerSource::Immediate;
            } else if (boolean && (value == "OFF" || value == "0")) {
                source = vna_service::TriggerSource::Manual;
            } else if (!boolean && matches(value, "IMM", "IMMEDIATE")) {
                source = vna_service::TriggerSource::Immediate;
            } else if (!boolean && matches(value, "MAN", "MANUAL")) {
                source = vna_service::TriggerSource::Manual;
            } else {
                error(command, -224, "Unsupported trigger source");
                return;
            }
            const bool asynchronous =
                source == vna_service::TriggerSource::Immediate && settings.sweepMode == vna_service::SweepMode::Single;
            setOperation(service.setTriggerSource(source), asynchronous);
            return;
        }
        if (path == "TRIG:SCOP" || path == "TRIG:SEQ:SCOP") {
            if (command.query) {
                if (arguments(command, 0)) {
                    response(settings.currentTriggerScope ? "CURR" : "ALL");
                }
                return;
            }
            if (!arguments(command, 1)) {
                return;
            }
            const std::string value = upper(command.arguments.front());
            if (value == "ALL") {
                service.setCurrentTriggerScope(false);
            } else if (matches(value, "CURR", "CURRENT")) {
                service.setCurrentTriggerScope(true);
            } else {
                error(command, -224, "Unsupported trigger scope");
            }
            return;
        }
        if (path == "SENS1:SWE:MODE") {
            if (command.query) {
                if (arguments(command, 0)) {
                    response(settings.sweepMode == vna_service::SweepMode::Hold
                                 ? "HOLD"
                                 : (settings.sweepMode == vna_service::SweepMode::Single ? "SING" : "CONT"));
                }
                return;
            }
            if (!arguments(command, 1)) {
                return;
            }
            const std::string value = upper(command.arguments.front());
            vna_service::SweepMode mode;
            if (value == "HOLD") {
                mode = vna_service::SweepMode::Hold;
            } else if (matches(value, "CONT", "CONTINUOUS")) {
                mode = vna_service::SweepMode::Continuous;
            } else if (matches(value, "SING", "SINGLE")) {
                mode = vna_service::SweepMode::Single;
            } else {
                error(command, -224, "Unsupported sweep mode");
                return;
            }
            setOperation(
                service.setSweepMode(mode), mode == vna_service::SweepMode::Single &&
                                                settings.triggerSource == vna_service::TriggerSource::Immediate);
            return;
        }
        if ((path == "INIT" || path == "INIT1" || path == "INIT:IMM" || path == "INIT1:IMM") && !command.query) {
            if (arguments(command, 0)) {
                setOperation(service.trigger(), true);
            }
            return;
        }
        if (path == "CALC1:PAR:DEF:EXT" || path == "CALC1:PAR:EXT") {
            if (command.query) {
                error(command, -113, "Undefined query header");
                return;
            }
            if (!arguments(command, 2)) {
                return;
            }
            vna_service::Measurement measurement;
            std::string parameter;
            if (!stringArgument(command.arguments[0], measurement.name) ||
                !stringArgument(command.arguments[1], parameter)) {
                error(command, -104, "Measurement name and parameter require quoted strings");
                return;
            }
            if (!receiverParameter(parameter, measurement)) {
                error(command, -224, "Only logical a1..a4/b1..b4 non-ratio receivers are supported");
                return;
            }
            setOperation(service.defineMeasurement(measurement));
            return;
        }
        if (path == "CALC1:PAR:SEL") {
            if (command.query) {
                if (arguments(command, 0)) {
                    response(quote(settings.selectedMeasurement));
                }
                return;
            }
            if (!arguments(command, 1)) {
                return;
            }
            std::string name;
            if (!stringArgument(command.arguments.front(), name)) {
                error(command, -104, "Measurement name requires a string");
                return;
            }
            service.selectMeasurement(name);
            return;
        }
        if (path == "CALC1:PAR:CAT:EXT" && command.query) {
            if (command.arguments.size() > 1U) {
                error(command, -108, "Too many parameters");
                return;
            }
            if (!command.arguments.empty()) {
                const auto value = upper(command.arguments.front());
                if (!matches(value, "NORM", "NORMAL") && !matches(value, "DEF", "DEFINE") &&
                    !matches(value, "DISP", "DISPLAY")) {
                    error(command, -224, "Unsupported catalog option");
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
            response(quote(catalog.empty() ? "NO CATALOG" : catalog));
            return;
        }
        if ((path == "CALC1:PAR:DEL" || path == "CALC1:PAR:DEL:NAME") && !command.query) {
            if (!arguments(command, 1)) {
                return;
            }
            std::string name;
            if (!stringArgument(command.arguments.front(), name)) {
                error(command, -104, "Measurement name requires a string");
                return;
            }
            setOperation(service.deleteMeasurement(name));
            return;
        }
        if (path == "FORM" || path == "FORM:DATA") {
            if (command.query) {
                if (arguments(command, 0)) {
                    response("ASC,0");
                }
                return;
            }
            if (command.arguments.empty() || command.arguments.size() > 2U) {
                error(command, -109, "FORM requires ASCii[,0]");
                return;
            }
            if (!matches(upper(command.arguments[0]), "ASC", "ASCII")) {
                error(command, -224, "Only ASCii data format is implemented");
                return;
            }
            if (command.arguments.size() == 2U) {
                unsigned precision = 0;
                if (!integer(command.arguments[1], precision, 0U)) {
                    error(command, -222, "Only ASCii,0 is implemented");
                }
            }
            return;
        }
        if ((path == "SENS1:X" || path == "SENS1:X:VAL") && command.query) {
            if (arguments(command, 0)) {
                beginEncoding(service.stimulusAxis(), command);
            }
            return;
        }
        if (path == "CALC1:DATA" && command.query) {
            if (!arguments(command, 1)) {
                return;
            }
            if (upper(command.arguments.front()) != "RDATA") {
                error(command, -224, "Only RDATA is implemented");
                return;
            }
            beginEncoding(service.measurementData(), command);
            return;
        }
        error(command, -113, "Undefined header");
    }

    void poll()
    {
        service.poll();
        for (auto watch = watches.begin(); watch != watches.end();) {
            if (watch->generation != service.statusGeneration()) {
                watch = watches.erase(watch);
            } else if (allReady(watch->operations)) {
                service.markOperationComplete();
                watch = watches.erase(watch);
            } else {
                ++watch;
            }
        }
        if (sequential.valid()) {
            if (!operationReady(sequential)) {
                return;
            }
            sequential = vna_service::Operation();
        }
        if (waiting) {
            if (!allReady(barrier)) {
                return;
            }
            if (waitingQuery) {
                response("+1");
            }
            waiting = false;
            barrier.clear();
        }
        encodeBatch();
        if (!encoding.empty()) {
            return;
        }
        // Parsing/dispatching is bounded independently of numeric encoding.
        for (unsigned count = 0; count < 32U && !messages.empty(); ++count) {
            Message &message = messages.front();
            if (message.position == message.units.size()) {
                if (connected && !message.queryFailed && !message.response.empty()) {
                    if (outputBytes + message.response.size() + 1U > kOutputBudget) {
                        return;
                    }
                    message.response += '\n';
                    outputBytes += message.response.size();
                    output.push_back(std::move(message.response));
                }
                messages.pop_front();
                continue;
            }
            const ParsedCommand command = parseCommand(message.units[message.position++], message.parent);
            dispatch(command);
            if (sequential.valid() || waiting || !encoding.empty()) {
                break;
            }
        }
        if (!connected && !pending() && control) {
            service.releaseControl();
            control = false;
        }
    }
};

ScpiEngine::ScpiEngine(vna_service::InstrumentService &service) : impl_(new Impl(service))
{
}

ScpiEngine::~ScpiEngine()
{
    // The owner normally drains first. An idle engine may also be destroyed directly.
    if (impl_->control && !impl_->pending()) {
        impl_->service.releaseControl();
    }
}

bool ScpiEngine::submitLine(const std::string &line)
{
    if (!impl_->connected || !impl_->control || impl_->messages.size() >= 32U || line.size() > 16384U) {
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
    impl_->messages.push_back(std::move(message));
    return true;
}

void ScpiEngine::poll()
{
    impl_->poll();
}

std::string ScpiEngine::takeOutput()
{
    if (impl_->output.empty()) {
        return std::string();
    }
    std::string output = std::move(impl_->output.front());
    impl_->output.pop_front();
    impl_->outputBytes -= output.size();
    return output;
}

bool ScpiEngine::hasPending() const
{
    return impl_->pending();
}

bool ScpiEngine::hasControl() const
{
    return impl_->control;
}

void ScpiEngine::setTransportOutputPending(bool pending)
{
    impl_->transportOutputPending = pending;
}

void ScpiEngine::disconnect()
{
    impl_->connected = false;
    impl_->output.clear();
    impl_->outputBytes = 0;
    impl_->encoding.clear();
    impl_->encodedResponse.clear();
    for (auto &message : impl_->messages) {
        message.response.clear();
    }
}

bool ScpiEngine::canReleaseControl() const
{
    return !impl_->connected && !impl_->control && !impl_->pending();
}

}  // namespace vna_scpi
