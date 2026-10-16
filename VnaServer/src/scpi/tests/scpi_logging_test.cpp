/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <future>
#include <iostream>
#include <iterator>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fixture.h"
#include "scpi/scpi_engine.h"
#include "vna_log/logger.h"

namespace {
constexpr unsigned LARGE_POINT_COUNT = 4000U;

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

class FileLogging final {
public:
    explicit FileLogging(std::string path, VnaLog::Level level = VnaLog::Level::INFO) : path_(std::move(path))
    {
        auto &logger = VnaLog::Logger::Instance();
        logger.Shutdown();
        std::remove(path_.c_str());
        VnaLog::LogConfig config;
        config.filePath = path_;
        config.consoleEnabled = false;
        config.level = level;
        config.pattern = "[%K] %n - %v";
        logger.Configure(config);
    }

    ~FileLogging()
    {
        VnaLog::Logger::Instance().Shutdown();
    }

    FileLogging(const FileLogging &) = delete;
    FileLogging &operator=(const FileLogging &) = delete;

    std::string Read() const
    {
        VnaLog::Logger::Instance().Flush();
        std::ifstream input(path_, std::ios::binary);
        return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    }

private:
    const std::string path_;
};

std::vector<std::string> Events(const std::string &text, const std::string &event)
{
    std::vector<std::string> records;
    std::istringstream input(text);
    std::string line;
    while (std::getline(input, line)) {
        if (line.find("VnaScpi - SCPI event=" + event + " ") != std::string::npos) {
            records.push_back(std::move(line));
        }
    }
    return records;
}

std::string Field(const std::string &record, const std::string &name)
{
    const auto begin = record.find(" " + name + "=");
    Require(begin != std::string::npos, "Missing diagnostic field " + name + ": " + record);
    const auto value = begin + name.size() + 2U;
    return record.substr(value, record.find(' ', value) - value);
}

std::string Received(const std::string &text, const std::string &program)
{
    const auto records = Events(text, "received");
    const auto found = std::find_if(records.begin(), records.end(), [&program](const std::string &record) {
        const auto position = record.find(" program=");
        return position != std::string::npos && record.substr(position + 9U) == program;
    });
    Require(found != records.end(), "Missing original received program: " + program);
    return *found;
}

std::vector<std::string> Related(
    const std::string &text, const std::string &event, const std::string &received, unsigned unit)
{
    auto records = Events(text, event);
    records.erase(std::remove_if(records.begin(), records.end(),
                      [&](const std::string &record) {
                          return Field(record, "connection") != Field(received, "connection") ||
                                 Field(record, "message") != Field(received, "message") ||
                                 Field(record, "unit") != std::to_string(unit);
                      }),
        records.end());
    return records;
}

std::string Terminal(const std::string &text, const std::string &event, const std::string &received, unsigned unit)
{
    const auto records = Related(text, event, received, unit);
    Require(records.size() == 1U, "A command must have exactly one " + event + " record");
    return records.front();
}

void Drain(VnaScpi::ScpiEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (engine.HasPending()) {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "SCPI completion timed out");
        std::this_thread::yield();
    }
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &program)
{
    Require(engine.SubmitLine(program), "SCPI rejected program: " + program);
    Drain(engine);
    return engine.TakeOutput();
}

void AwaitService(VnaServiceTest::Fixture &fixture, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Fixture operation timed out");
        std::this_thread::yield();
    }
    Require(operation.get().ok, "Fixture operation failed");
}

void ReceivedProgramsAndCompoundResults(const std::string &path)
{
    FileLogging logging(path);
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    const std::string program = "*IDN?;:FORM?;:SENS1:SWE:POIN?";
    const auto response = Command(engine, program);
    Require(response.find("VnaPro,") == 0U && response.find(";ASC,0;") != std::string::npos,
        "Diagnostics must preserve normal compound protocol output");
    Require(Command(engine, "FORM ASCII").empty(), "A successful setter must not acquire an ACK response");
    const auto text = logging.Read();
    const auto received = Received(text, program);
    Require(received.find("[INFO]") == 0U && Field(received, "role") == "controller" &&
                Field(received, "bytes") == std::to_string(program.size()),
        "Default INFO must record the complete received controller program and byte count");
    const std::vector<std::string> paths{"*IDN", "FORM", "SENS1:SWE:POIN"};
    for (unsigned unit = 1U; unit <= paths.size(); ++unit) {
        const auto started = Terminal(text, "started", received, unit);
        const auto completed = Terminal(text, "completed", received, unit);
        Require(Field(started, "path") == paths[unit - 1U] && Field(completed, "path") == paths[unit - 1U] &&
                    Field(completed, "query") == "1" && Field(completed, "code") == "0" &&
                    completed.find("[INFO]") == 0U,
            "Every compound unit needs an independently correlated execution result");
    }
    const auto setter = Terminal(text, "completed", Received(text, "FORM ASCII"), 1U);
    Require(Field(setter, "stage") == "sync" && Field(setter, "query") == "0",
        "A synchronous setter must log its actual successful execution");
}

void CommandErrorsPreserveTheErrorQueue(const std::string &path)
{
    FileLogging logging(path);
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    const std::vector<std::string> programs{"SENS1:FREQ:STAR 'unterminated", "UNDEFINED:HEADER", "FORM REAL,32"};
    const std::vector<int> codes{-102, -113, -222};
    for (const auto &program : programs) {
        Require(Command(engine, program).empty(), "Invalid setters must not return success responses");
    }
    const auto text = logging.Read();
    for (std::size_t index = 0U; index < programs.size(); ++index) {
        const auto received = Received(text, programs[index]);
        const auto failed = Terminal(text, "failed", received, 1U);
        Require(failed.find("[WARN]") == 0U && Field(failed, "code") == std::to_string(codes[index]) &&
                    Related(text, "completed", received, 1U).empty(),
            "Syntax, unknown header and parameter failures must log WARN without a success record");
        Require(Command(engine, "SYST:ERR?").find(std::to_string(codes[index]) + ",") == 0U,
            "Logging must neither consume nor reorder the instrument error FIFO");
    }
    Require(Command(engine, "SYST:ERR?").find("0,") == 0U, "Each failure must enter the FIFO exactly once");
}

VnaService::Operation ReadySave()
{
    std::promise<VnaService::OperationResult> promise;
    auto operation = promise.get_future().share();
    promise.set_value({true, 0, ""});
    return operation;
}

void ImportSet(VnaServiceTest::Fixture &fixture)
{
    auto set = std::make_shared<VnaService::CalibrationSet>();
    set->name = "Logging durable coefficients";
    set->deviceIdentity = fixture.service.Identity();
    set->ifBandwidthHz = fixture.service.Settings().ifBandwidthHz;
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->frequencyHz = fixture.service.StimulusAxis().values;
    data->onePortTerms.assign(data->frequencyHz.size(), {{0.01, 0.02}, {0.0, 0.0}, {1.3, 0.04}});
    set->powerDbm.assign(data->frequencyHz.size(), fixture.service.Settings().powerDbm);
    set->data = std::move(data);
    VnaService::CalibrationChange change;
    change.kind = VnaService::CalibrationChangeKind::IMPORT_SET;
    change.importedSet = std::move(set);
    AwaitService(fixture, fixture.service.ApplyCalibration(std::move(change)));
}

void DurableCompletionIsObserved(const std::string &path, bool disconnect)
{
    FileLogging logging(path);
    VnaServiceTest::Fixture fixture;
    auto saving = ReadySave();
    unsigned saves = 0U;
    fixture.service.SetCalibrationSaveHandler([&](unsigned, std::vector<VnaService::CalibrationSetPtr>, std::uint64_t) {
        ++saves;
        return saving;
    });
    VnaScpi::ScpiEngine engine(fixture.service);
    ImportSet(fixture);
    std::promise<VnaService::OperationResult> persistence;
    saving = persistence.get_future().share();
    const std::string program = "SENS1:CORR:CSET:ACT 'Logging durable coefficients',0;*OPC?";
    Require(engine.SubmitLine(program), "Durable activation must be admitted");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (saves < 2U) {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Activation did not enter the actual saver");
        std::this_thread::yield();
    }
    const auto before = logging.Read();
    const auto received = Received(before, program);
    Require(Related(before, "completed", received, 1U).empty() && Related(before, "failed", received, 1U).empty() &&
                engine.TakeOutput().empty() && fixture.service.Calibration().selectedSetId == 0U,
        "Neither logs, protocol nor committed selection may claim success before the durable future resolves");
    if (disconnect) {
        engine.Disconnect();
        persistence.set_value({true, 0, ""});
    } else {
        persistence.set_value({false, -310, "explicit durable write failure"});
    }
    Drain(engine);
    const auto after = logging.Read();
    const auto terminal = Terminal(after, disconnect ? "completed" : "failed", received, 1U);
    Require(Field(terminal, "stage") == "service" && Field(terminal, "path") == "SENS1:CORR:CSET:ACT" &&
                Field(terminal, "code") == (disconnect ? "0" : "-310"),
        "The actual service result must retain the original activation command association");
    if (disconnect) {
        Require(engine.CanReleaseControl() && engine.TakeOutput().empty() &&
                    fixture.service.Calibration().selectedSetId == 1U,
            "Disconnect must discard responses but still log and commit an admitted durable operation");
    } else {
        Require(engine.TakeOutput() == "+1\n" && terminal.find("[WARN]") == 0U &&
                    Related(after, "completed", received, 1U).empty() &&
                    Command(engine, "SYST:ERR?").find("-310,") == 0U && Command(engine, "SYST:ERR?").find("0,") == 0U &&
                    Field(Terminal(after, "completed", received, 2U), "stage") == "barrier" &&
                    fixture.service.Calibration().selectedSetId == 0U,
            "A failed save completes OPC, preserves the old selection and remains visible in the error FIFO");
    }
}

void ObserverAdmissionDoesNotExecuteAPrefix(const std::string &path)
{
    FileLogging logging(path);
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine controller(fixture.service);
    VnaScpi::ScpiEngine observer(fixture.service);
    const auto revision = fixture.service.Settings().revision;
    const auto points = fixture.service.Settings().points;
    const std::string program = "*IDN?;:SENS1:SWE:POIN 17";
    Require(!observer.SubmitLine(program), "Mixed observer writes must reject the entire program");
    const auto text = logging.Read();
    const auto received = Received(text, program);
    const auto rejected = Terminal(text, "rejected", received, 2U);
    Require(Field(received, "role") == "observer" && rejected.find("[WARN]") == 0U &&
                Field(rejected, "code") == "-221" && Related(text, "started", received, 1U).empty() &&
                Related(text, "completed", received, 1U).empty() && observer.TakeOutput().empty() &&
                fixture.service.Settings().revision == revision && fixture.service.Settings().points == points &&
                Command(controller, "SYST:ERR?").find("0,") == 0U,
        "Rejected observer input must be diagnosable without executing its read prefix or changing the global FIFO");
}

void ResetObservesEveryChannelResult(const std::string &path)
{
    FileLogging logging(path);
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    VnaService::ChannelCreationOptions options;
    options.copyChannel = true;
    AwaitService(fixture, fixture.service.CreateChannel(options, fixture.service.Settings().revision,
                              fixture.service.Display().revision, "SCPI"));
    Command(engine, "SENS1:SWE:POIN 7;:SENS2:SWE:POIN 9");
    const auto firstRevision = fixture.service.Settings(1U).acquisitionRevision;
    const auto lastRevision = fixture.service.Settings(2U).acquisitionRevision;
    fixture.device->FailNextPlanPreparation();
    const std::string program = "*RST;*OPC?";
    Require(Command(engine, program) == "+1\n", "Reset OPC must report completion even when one reset fails");
    Require(fixture.service.Settings(1U).points == 7U &&
                fixture.service.Settings(1U).acquisitionRevision == firstRevision &&
                fixture.service.Settings(2U).points == fixture.service.Profile().defaultPoints &&
                fixture.service.Settings(2U).acquisitionRevision > lastRevision,
        "The controlled first reset must fail while the final channel reset actually succeeds");
    const auto text = logging.Read();
    const auto received = Received(text, program);
    const auto failed = Terminal(text, "failed", received, 1U);
    Require(Field(failed, "path") == "*RST" && Field(failed, "stage") == "service" && Field(failed, "code") == "-310" &&
                Related(text, "completed", received, 1U).empty() && Command(engine, "SYST:ERR?").find("-310,") == 0U &&
                Command(engine, "SYST:ERR?").find("0,") == 0U,
        "A successful final reset future cannot hide a preceding failed channel or duplicate its FIFO error");
}

void PollUntilAcquireEntered(VnaScpi::ScpiEngine &engine, VnaServiceTest::Fixture &fixture)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (!fixture.device->HasEntered()) {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Actual acquisition did not enter its controlled gate");
        std::this_thread::yield();
    }
}

void ClearStatusCancelsAnOpcEvent(const std::string &path)
{
    FileLogging logging(path);
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    fixture.device->Arm();
    const std::string program = "INIT1:IMM;*OPC;*CLS";
    Require(engine.SubmitLine(program), "Admit an actual acquisition and pending OPC event before CLS");
    PollUntilAcquireEntered(engine, fixture);
    engine.Poll();
    const auto before = logging.Read();
    const auto received = Received(before, program);
    Require(Field(Terminal(before, "cancelled", received, 2U), "stage") == "barrier" &&
                Related(before, "completed", received, 1U).empty() &&
                Related(before, "completed", received, 2U).empty() && engine.TakeOutput().empty(),
        "CLS must cancel the pending OPC event without claiming completion or sending a setter response");
    fixture.device->Release();
    Drain(engine);
    const auto after = logging.Read();
    Require(Field(Terminal(after, "completed", received, 1U), "stage") == "service" &&
                Related(after, "completed", received, 2U).empty() && Command(engine, "*ESR?") == "0\n" &&
                Command(engine, "SYST:ERR?").find("0,") == 0U,
        "Completing the admitted acquisition must not resurrect the cancelled OPC bit or create errors");
}

void WaitCompletesAfterARealFailure(const std::string &path)
{
    FileLogging logging(path);
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    fixture.device->Arm(true);
    const std::string program = "INIT1:IMM;*WAI;*IDN?";
    Require(engine.SubmitLine(program), "Admit an acquisition with an explicit pending failure");
    PollUntilAcquireEntered(engine, fixture);
    const auto before = logging.Read();
    const auto received = Received(before, program);
    Require(Related(before, "completed", received, 1U).empty() && Related(before, "failed", received, 1U).empty() &&
                Related(before, "completed", received, 2U).empty() && engine.TakeOutput().empty(),
        "WAI must not complete or dispatch the later query while the actual acquisition is blocked");
    fixture.device->Release();
    Drain(engine);
    const auto after = logging.Read();
    const auto waited = Terminal(after, "completed", received, 2U);
    Require(Field(Terminal(after, "failed", received, 1U), "code") == "-310" && Field(waited, "stage") == "barrier" &&
                Field(waited, "code") == "0" && std::stoull(Field(waited, "priorFailures")) > 0U &&
                engine.TakeOutput().find("VnaPro,") == 0U && Command(engine, "SYST:ERR?").find("-310,") == 0U &&
                Command(engine, "SYST:ERR?").find("0,") == 0U,
        "WAI completes after failure, records that prior failure, and preserves query and FIFO protocol semantics");
}

void NumericResponsesAreSummariesOnly(const std::string &path)
{
    FileLogging logging(path);
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    Require(Command(engine, "SENS1:SWE:POIN 4000;:INIT1:IMM;*OPC?") == "+1\n", "Prepare a real complete large frame");
    const std::string query = "CALC1:DATA? RDATA";
    const auto ascii = Command(engine, query);
    Require(std::count(ascii.begin(), ascii.end(), ',') + 1U == LARGE_POINT_COUNT * 2U,
        "ASCII response must contain every real and imaginary component");
    const auto asciiLog = logging.Read();
    const auto asciiRecord = Terminal(asciiLog, "completed", Received(asciiLog, query), 1U);
    Require(Field(asciiRecord, "stage") == "encoding" &&
                Field(asciiRecord, "values") == std::to_string(LARGE_POINT_COUNT * 2U) &&
                std::stoull(Field(asciiRecord, "bytes")) == ascii.size() - 1U &&
                asciiLog.find(ascii.substr(0U, 120U)) == std::string::npos,
        "ASCII diagnostics must report array size without recording measurement samples");
    Command(engine, "FORM REAL,64;:FORM:BORD SWAP");
    const std::string binaryQuery = ":CALC1:DATA? RDATA";
    const auto binary = Command(engine, binaryQuery);
    Require(binary.size() > LARGE_POINT_COUNT * 2U * sizeof(double) && binary.front() == '#',
        "REAL64 must preserve the complete IEEE block response");
    const auto binaryLog = logging.Read();
    const auto binaryRecord = Terminal(binaryLog, "completed", Received(binaryLog, binaryQuery), 1U);
    Require(Field(binaryRecord, "stage") == "encoding" &&
                Field(binaryRecord, "values") == std::to_string(LARGE_POINT_COUNT * 2U) &&
                std::stoull(Field(binaryRecord, "bytes")) == binary.size() - 1U &&
                binaryLog.find(binary.substr(32U, 64U)) == std::string::npos && binaryLog.size() < ascii.size(),
        "Binary diagnostics must remain bounded summaries, never dump the block payload");
    const std::string cancelledQuery = "calc1:data? RDATA";
    Require(engine.SubmitLine(cancelledQuery), "Admit a final array before disconnect");
    engine.Poll();
    const auto pendingLog = logging.Read();
    const auto pending = Received(pendingLog, cancelledQuery);
    Require(Related(pendingLog, "completed", pending, 1U).empty(),
        "Beginning array encoding must not report successful response completion");
    engine.Disconnect();
    Drain(engine);
    const auto cancelledLog = logging.Read();
    Require(Field(Terminal(cancelledLog, "cancelled", pending, 1U), "stage") == "encoding" &&
                Related(cancelledLog, "completed", pending, 1U).empty() && engine.CanReleaseControl() &&
                engine.TakeOutput().empty(),
        "Disconnect during encoding must log cancellation without a fictitious successful array response");
}

std::string ProtocolWithLogging(const std::string &path, VnaLog::Level level)
{
    FileLogging logging(path, level);
    std::string output;
    {
        VnaServiceTest::Fixture fixture;
        {
            VnaScpi::ScpiEngine engine(fixture.service);
            output += Command(engine, "*IDN?;:FORM?;:SENS1:SWE:POIN?");
            output += Command(engine, "FORM REAL,32");
            output += Command(engine, "SYST:ERR?;*OPC?;*ESR?");
        }
        Require(fixture.service.ControlOwner().empty(), "Diagnostics must not retain a released controller");
    }
    if (level == VnaLog::Level::OFF) {
        Require(logging.Read().empty(), "Disabled diagnostics must remain silent");
    }
    return output;
}
}  // 匿名命名空间

int main(int argc, char **argv)
{
    try {
        Require(argc == 2, "Usage: scpi_logging_test <log-file-prefix>");
        const std::string prefix = argv[1];
        ReceivedProgramsAndCompoundResults(prefix + "-compound.log");
        CommandErrorsPreserveTheErrorQueue(prefix + "-errors.log");
        DurableCompletionIsObserved(prefix + "-failure.log", false);
        DurableCompletionIsObserved(prefix + "-disconnect.log", true);
        ObserverAdmissionDoesNotExecuteAPrefix(prefix + "-observer.log");
        ResetObservesEveryChannelResult(prefix + "-reset.log");
        ClearStatusCancelsAnOpcEvent(prefix + "-opc-cancel.log");
        WaitCompletesAfterARealFailure(prefix + "-wai.log");
        NumericResponsesAreSummariesOnly(prefix + "-arrays.log");
        Require(ProtocolWithLogging(prefix + "-on.log", VnaLog::Level::INFO) ==
                    ProtocolWithLogging(prefix + "-off.log", VnaLog::Level::OFF),
            "Logging configuration must not change any protocol response or error");
        std::cout
            << "PASS INFO reception/results, WARN/FIFO, durable barriers, observers, array summaries and disconnect\n";
        return 0;
    } catch (const std::exception &error) {
        VnaLog::Logger::Instance().Shutdown();
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
