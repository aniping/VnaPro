/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <thread>
#include <utility>
#include <vector>

#include "../../../../VnaNativeSimulator/tests/scoped_simulator_environment.h"
#include "fixture.h"
#include "native_fixture.h"
#include "scpi/scpi_engine.h"
#include "scpi/scpi_parser.h"
#include "vna_core/acquisition_core.h"
#include "vna_service/instrument_service.h"

namespace {
using VnaScpiTest::NativeFixture;

void Expect(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void Near(double value, double expected)
{
    Expect(std::fabs(value - expected) < 1e-10, "numeric mismatch: " + std::to_string(value));
}

void Drain(VnaScpi::ScpiEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        engine.Poll();
        if (!engine.HasPending()) {
            return;
        }
        Expect(std::chrono::steady_clock::now() < deadline, "engine drain timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (true);
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &text)
{
    Expect(engine.SubmitLine(text), "submit failed");
    Drain(engine);
    return engine.TakeOutput();
}

VnaService::OperationResult AwaitOperation(
    VnaService::InstrumentService &service, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "service operation did not terminate");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return operation.get();
}

std::vector<double> Numbers(const std::string &text)
{
    std::vector<double> values;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        values.push_back(std::stod(item));
    }
    return values;
}

void Run(const std::string &name, void (*test)(const std::string &), const std::string &driver, unsigned &failed)
{
    try {
        test(driver);
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
        ++failed;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
}

void ParserAndCompoundResponse(const std::string &driver)
{
    NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Expect(Command(engine, "*IDN?;*OPC?") == "VnaPro,NativeSimulator,4PORT,1.0;+1\n", "compound response boundary");
    const auto result = Command(engine, "SENSe1:FREQuency:STARt 2MHz;STOP 3e6;STARt?;STOP?");
    const auto delimiter = result.find(';');
    Near(std::stod(result.substr(0, delimiter)), 2000000.0);
    Near(std::stod(result.substr(delimiter + 1U)), 3000000.0);
    Expect(Command(engine, "CALC1:PAR:DEF:EXT 'W;A,1','a1,1';:CALC1:PAR:SEL 'W;A,1';SEL?") == "\"W;A,1\"\n",
        "quoted delimiters and relative path");
    Command(engine, "*CLS;:VNAP:DATA:SNAP");
    Expect(Command(engine, "SYST:ERR?").find("-113,") == 0U, "custom header must fail");
}

void ConfigurationAndRevision(const std::string &driver)
{
    NativeFixture fixture(driver);
    Expect(fixture.service.TryAcquireControl(), "control");
    const auto revision = fixture.service.Settings().revision;
    const auto first = fixture.service.SetStartHz(150000000.0, revision);
    const auto stale = fixture.service.SetStopHz(200000000.0, revision);
    while (first.wait_for(std::chrono::seconds(0)) != std::future_status::ready ||
           stale.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.Poll();
        std::this_thread::yield();
    }
    Expect(first.get().ok, "first revision change");
    Expect(!stale.get().ok && stale.get().errorCode == -221, "execution revision revalidation");
    Near(fixture.service.Settings().startHz, 150000000.0);
    Near(fixture.service.Settings().stopHz, 249000000.0);
    const auto prior = fixture.service.Settings().revision;
    auto outOfRange = fixture.service.SetStopHz(100.0);
    while (outOfRange.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.Poll();
    }
    Expect(!outOfRange.get().ok, "coupled start out of range");
    Expect(fixture.service.Settings().revision == prior, "failed candidate must not commit");
    VnaService::Measurement typed;
    typed.name = "UpperCaseTyped";
    typed.receiverKind = 'B';
    const auto defined = fixture.service.DefineMeasurement(typed);
    while (defined.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.Poll();
    }
    Expect(defined.get().ok && fixture.service.Settings().measurements.back().receiverKind == 'b',
        "typed receiver letters normalize while measurement name is preserved");
    fixture.service.ReleaseControl();
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS;SENS1:BAND 101Hz");
    Near(std::stod(Command(engine, "SENS1:BAND?")), 1000.0);
    Expect(Numbers(Command(engine, "SENS1:X?")).size() == 201U, "axis before acquisition");
    Expect(Command(engine, "CALC1:DATA? RDATA").empty(), "RDATA before acquisition must not fabricate data");
    Expect(Command(engine, "SYST:ERR?").find("-230,") == 0U, "missing data error");
}

void TwoPortSParameterDefinitions(const std::string &driver)
{
    NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    const std::vector<std::pair<std::string, std::string>> definitions = {{"ForwardReflection", "S1_1"},
        {"ForwardTransmission", "S21"}, {"ReverseTransmission", "S1_2"}, {"ReverseReflection", "S22"}};
    for (const auto &definition : definitions) {
        Command(engine, "CALC1:PAR:DEF:EXT '" + definition.first + "','" + definition.second + "';*OPC?");
        Expect(Command(engine, "SYST:ERR?").find("0,") == 0U, "标准双端口 S 参数定义被拒绝");
    }
    const auto catalog = Command(engine, "CALC1:PAR:CAT:EXT?");
    Expect(catalog.find("ReverseTransmission,S12,ReverseReflection,S22") != std::string::npos,
        "目录未规范化反向 S 参数名称");
    const auto revision = fixture.service.Settings().revision;
    for (const auto &parameter : {"s12", "S13", "S31", "S1__2", "S_12", "S012"}) {
        Command(engine, std::string("CALC1:PAR:DEF:EXT 'Invalid','") + parameter + "'");
        Expect(Command(engine, "SYST:ERR?").find("-224,") == 0U, "非法 S 参数未记录标准错误");
    }
    Expect(fixture.service.Settings().revision == revision, "拒绝的 S 参数定义改变了配置");
    Command(engine, "CALC1:PAR:SEL 'ReverseTransmission'");
    Expect(Command(engine, "CALC1:DATA? SDATA").empty(), "未校准测量不应返回校准数据");
    Expect(Command(engine, "SYST:ERR?").find("-224,") == 0U, "SDATA 未记录标准错误");
}

void FrequencyQueriesFollowInstrumentProfile(const std::string &)
{
    VnaService::InstrumentProfile profile;
    profile.portCount = 2U;
    VnaServiceTest::Fixture fixture(profile);
    VnaScpi::ScpiEngine engine(fixture.service);
    Near(std::stod(Command(engine, "SENS1:FREQ:STAR? MIN")), 10000000.0);
    Near(std::stod(Command(engine, "SENS1:FREQ:STOP? MAX")), 8000000000.0);
    Near(std::stod(Command(engine, "SENS1:FREQ:STAR?")), 10000000.0);
    Near(std::stod(Command(engine, "SENS1:FREQ:STOP?")), 8000000000.0);
    Near(std::stod(Command(engine, "SENS1:SWE:POIN?")), 201.0);
    Command(engine, "SENS1:FREQ:STOP MIN;STAR MAX;*OPC?");
    Expect(Command(engine, "SYST:ERR?").find("0,") == 0U, "频率 MIN/MAX 写入未使用仪器能力");
    Near(std::stod(Command(engine, "SENS1:FREQ:STAR?")), 8000000000.0);
    Near(std::stod(Command(engine, "SENS1:FREQ:STOP?")), 8000000000.0);
    const auto revision = fixture.service.Settings().revision;
    for (const auto &frequency : {"9MHz", "8.001GHz"}) {
        Command(engine, std::string("SENS1:FREQ:STOP ") + frequency);
        Expect(Command(engine, "SYST:ERR?").find("-222,") == 0U, "仪器频率范围外的设置被接受");
    }
    Command(engine, "CALC1:PAR:DEF:EXT 'OutOfRangePort','b3,3'");
    Expect(Command(engine, "SYST:ERR?").find("-222,") == 0U, "超出双端口能力的定义未记录范围错误");
    Expect(fixture.service.Settings().revision == revision, "能力范围外的请求改变了配置");
}

void SingleFrameAndReceivers(const std::string &driver)
{
    NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS;SENS1:SWE:POIN 21");
    Command(engine, "CALC1:PAR:DEF:EXT 'wave_a','A1,2';:CALC1:PAR:DEF:EXT 'wave_b','B1,2'");
    Expect(Command(engine, "TRIG:SOUR IMM;:SENS1:SWE:MODE SING;*OPC?;:SENS1:SWE:MODE?") == "+1;HOLD\n",
        "single must end in hold");
    const auto frame = fixture.core.ReadView().frame;
    Expect(frame && frame->ports.size() == 2U, "all required source ports must publish together");
    Command(engine, "CALC1:PAR:SEL 'wave_a'");
    const auto a = Numbers(Command(engine, "CALC1:DATA? RDATA"));
    Expect(a.size() == 42U, "2N A array");
    Near(a.front(), 1.12);
    Command(engine, "CALC1:PAR:SEL 'wave_b'");
    const auto b = Numbers(Command(engine, "CALC1:DATA? RDATA"));
    Expect(b.size() == 42U && b.front() > 0.04 && b.front() < 0.06, "source2 receiver1 B must be distinct");
    Expect(fixture.core.ReadView().frame == frame, "selection must not acquire or alter frame");
    Expect(Command(engine, "CALC1:PAR:CAT:EXT?").find("wave_a,a1_2,wave_b,b1_2") != std::string::npos,
        "extended catalog receiver comma encoding");
    Command(engine, "SENS1:FREQ:STAR 2MHz");
    Expect(Command(engine, "CALC1:DATA? RDATA").empty(), "old revision frame must be rejected");
}

void PendingBarriersAndErrors(const std::string &driver)
{
    NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS;*ESE 1;*SRE 32");
    Expect(engine.SubmitLine("SENS1:SWE:MODE SING;*OPC;*WAI;*STB?;*ESR?;*ESR?"), "single submit");
    engine.Poll();
    Expect(engine.TakeOutput().empty(), "barrier cannot complete on admission");
    Expect(engine.HasPending(), "finite operation pending");
    Drain(engine);
    Expect(engine.TakeOutput() == "96;1;0\n", "OPC event/status lifecycle");
    Expect(engine.SubmitLine("SENS1:SWE:MODE SING;*WAI;:BOGUS;:SYST:ERR?"), "ordered error submit");
    engine.Poll();
    Expect(fixture.service.ErrorCount() == 0U, "error must wait for prior barrier");
    Drain(engine);
    Expect(engine.TakeOutput().find("-113,") == 0U, "error after barrier");
    Command(engine, "*CLS;TRIG:SOUR MAN;:SENS1:SWE:MODE CONT");
    Expect(Command(engine, "INIT1:IMM;*OPC?") == "+1\n", "manual INIT finite completion");
    Command(engine, "INIT:CONT OFF");
    Expect(Command(engine, "SENS1:SWE:MODE?") == "CONT\n", "INIT CONT OFF is not HOLD");
    Command(engine, "SENS1:SWE:MODE HOLD");
    Command(engine, "*CLS;TRIG:SOUR IMM;:SENS1:SWE:MODE CONT");
    Command(engine, "INIT1:IMM");
    Expect(Command(engine, "SYST:ERR?").find("-221,") == 0U, "INIT rejects internal trigger source");
    Expect(
        Command(engine, "SENS1:SWE:MODE?") == "CONT\n" && fixture.core.ReadView().state == VnaCore::CoreState::RUNNING,
        "invalid INIT must not change the actual continuous mode");
    Command(engine, "SENS1:SWE:MODE HOLD");
}

void BusyAdmissionAndOldRun(const std::string &driver)
{
    NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS;SENS1:SWE:MODE SING;*OPC?");
    const auto revision = fixture.service.Settings().revision;
    Expect(engine.SubmitLine("SENS1:SWE:MODE SING;:SENS1:FREQ:STAR 2MHz;:CALC1:DATA? RDATA"), "busy sequence");
    engine.Poll();
    Expect(fixture.service.ErrorCount() >= 1U, "configuration must reject pending single at admission");
    Expect(fixture.service.Settings().revision == revision, "busy config must not enqueue for later HOLD");
    Drain(engine);
    Expect(engine.TakeOutput().empty(), "query during new finite run must not use old frame");
    Expect(fixture.service.Settings().revision == revision, "busy config must not commit after single");
}

void DisconnectAndInstrumentErrors(const std::string &driver)
{
    NativeFixture fixture(driver);
    {
        VnaScpi::ScpiEngine engine(fixture.service);
        VnaScpi::ScpiEngine refused(fixture.service);
        Expect(!refused.HasControl() && !refused.SubmitLine("*IDN?"), "second controller must be rejected");
        Expect(engine.SubmitLine("*CLS;SENS1:SWE:MODE SING;*OPC?;:UNSUPPORTED"), "accepted disconnected input");
        engine.Poll();
        engine.Disconnect();
        Expect(!engine.CanReleaseControl(), "disconnect must retain accepted operations");
        Drain(engine);
        Expect(engine.CanReleaseControl(), "drained connection must release control");
        Expect(engine.TakeOutput().empty(), "disconnected output discarded");
    }
    VnaScpi::ScpiEngine next(fixture.service);
    Expect(next.HasControl(), "new controller after barrier");
    Expect(Command(next, "SYST:ERR?").find("-113,") == 0U, "global error FIFO survives client replacement");
    next.SetTransportOutputPending(true);
    Expect(Command(next, "*STB?") == "16\n", "MAV includes transport output");
}

void SoftwareBounds(const std::string &driver)
{
    NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Expect(!engine.SubmitLine(std::string(16385U, 'A')), "line bound");
    for (unsigned index = 0; index < 32U; ++index) {
        Expect(engine.SubmitLine("*IDN?"), "bounded input accepted");
    }
    Expect(!engine.SubmitLine("*IDN?"), "input queue bound");
    Drain(engine);
    while (!engine.TakeOutput().empty()) {
    }
    Command(engine, "SENS1:SWE:POIN 10001;:SENS1:SWE:MODE SING;*OPC?");
    const auto data = Command(engine, "CALC1:DATA? RDATA");
    Expect(data.size() < 1024U * 1024U && Numbers(data).size() == 20002U, "full bounded ASCII array");
}

void BlockedFiniteAndWatchBudget(const std::string &)
{
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS;SENS1:SWE:MODE SING;*OPC?");
    const auto old = fixture.core.ReadView().frame;
    fixture.device->Arm();
    Expect(engine.SubmitLine("SENS1:SWE:MODE SING"), "blocked single");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasEntered()) {
        engine.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "controlled scan never entered");
        std::this_thread::yield();
    }
    Expect(fixture.core.ReadView().frame == old, "blocked scan cannot publish");
    Expect(!fixture.service.MeasurementData().ok, "pending scan cannot reuse old run frame");
    for (unsigned index = 0; index < 40U; ++index) {
        Expect(engine.SubmitLine("*OPC"), "OPC message accepted");
        engine.Poll();
    }
    Expect(fixture.service.ErrorCount() == 8U, "pending OPC watches must be bounded at 32");
    Expect(fixture.service.PopError().code == -363, "watch overflow error");
    Expect(engine.SubmitLine("*OPC?"), "pending query");
    engine.Poll();
    engine.Disconnect();
    for (unsigned tick = 0; tick < 10U; ++tick) {
        engine.Poll();
    }
    Expect(!engine.CanReleaseControl(), "disconnect must not release blocked device I/O");
    fixture.device->Release();
    Drain(engine);
    Expect(engine.CanReleaseControl(), "blocked task completed and released");
    Expect(fixture.core.ReadView().frame != old, "new complete frame must eventually replace old");
}

void FailedSingleCannotReturnOldFrame(const std::string &)
{
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS;SENS1:SWE:MODE SING;*OPC?");
    const auto old = fixture.core.ReadView().frame;
    fixture.device->Arm(true);
    Expect(engine.SubmitLine("SENS1:SWE:MODE SING;*OPC?"), "failing single");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasEntered()) {
        engine.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "failure gate");
        std::this_thread::yield();
    }
    fixture.device->Release();
    Drain(engine);
    Expect(engine.TakeOutput() == "+1\n", "failed operation must still terminate OPC barrier");
    Expect(fixture.core.ReadView().frame == old, "failed single must retain old immutable frame");
    Expect(Command(engine, "CALC1:DATA? RDATA").empty(), "failed single cannot query old curve");
    Expect(fixture.service.ErrorCount() >= 2U, "operation and data failure diagnostics");
}

void MalformedHeaders(const std::string &driver)
{
    NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS");
    Expect(Command(engine, "?").empty(), "bare query has no fake output");
    Expect(Command(engine, "SYST:ERR?").find("-102,") == 0U, "bare query syntax error");
    Command(engine, "INIT:");
    Expect(Command(engine, "SYST:ERR?").find("-102,") == 0U, "trailing colon must not execute INIT");
}

void InvalidReceiverCannotProduceCurve(const std::string &)
{
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS;CALC1:PAR:DEF:EXT 'RawA','a1,1';:CALC1:PAR:DEF:EXT 'HealthyB','b1,1';:CALC1:PAR:SEL 'RawA'");
    fixture.device->SetInvalidSamples(true);
    Expect(Command(engine, "SENS1:SWE:MODE SING;*OPC?") == "+1\n", "Invalid-data scan still terminates");
    const auto held = fixture.core.ReadView().frame;
    Expect(Command(engine, "CALC1:DATA? RDATA").empty(), "Invalid receiver must not return a partial or fake curve");
    Expect(Command(engine, "SYST:ERR?").find("-230,") == 0U, "Invalid receiver gives standard data error");
    Command(engine, "CALC1:PAR:SEL 'HealthyB'");
    const auto healthy = Numbers(Command(engine, "CALC1:DATA? RDATA"));
    Expect(healthy.size() == 402U, "Another receiver on the same frame remains readable");
    for (const double sample : healthy) {
        Near(sample, 1.11);
    }
    Expect(Command(engine, "SYST:ERR?").find("0,") == 0U, "Healthy receiver adds no error");
    Expect(fixture.core.ReadView().frame == held, "Receiver selection cannot rewrite the held frame");
    fixture.device->SetInvalidSamples(false);
    Command(engine, "CALC1:PAR:SEL 'RawA'");
    Expect(Command(engine, "SENS1:SWE:MODE SING;*OPC?") == "+1\n", "Following valid scan completes");
    const auto restored = Numbers(Command(engine, "CALC1:DATA? RDATA"));
    Expect(restored.size() == 402U, "Valid scan restores a complete receiver curve");
    for (const double sample : restored) {
        Near(sample, 1.11);
    }
    Expect(fixture.core.ReadView().frame != held, "Valid scan publishes a new snapshot");
}

void ConfigurationPreservesDirectSettings(const std::string &)
{
    VnaServiceTest::Fixture fixture;
    auto &service = fixture.service;
    Expect(service.TryAcquireControl(), "typed control");
    VnaService::Measurement alternate;
    alternate.name = "Alternate";
    alternate.receiverKind = 'b';
    const auto defined = service.DefineMeasurement(alternate);
    while (defined.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.Poll();
    }
    Expect(defined.get().ok, "alternate receiver definition");
    Expect(service.SelectMeasurement("Alternate") && service.SetCurrentTriggerScope(true), "initial direct settings");
    fixture.device->ArmPlan();
    const auto configured = service.SetPoints(301U);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->HasPlanEntered()) {
        service.Poll();
        Expect(std::chrono::steady_clock::now() < deadline, "configuration gate not entered");
        std::this_thread::yield();
    }
    Expect(!service.SelectMeasurement("Trc1"), "SEL must reject an in-flight candidate snapshot");
    Expect(!service.SetCurrentTriggerScope(false), "scope must reject an in-flight candidate snapshot");
    Expect(service.PopError().code == -221 && service.PopError().code == -221, "direct setting conflicts");
    fixture.device->Release();
    while (configured.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.Poll();
    }
    Expect(configured.get().ok, "configuration completion");
    Expect(service.Settings().selectedMeasurement == "Alternate" && service.Settings().currentTriggerScope,
        "successful prior settings cannot be overwritten by candidate commit");
    Expect(service.SelectMeasurement("Trc1") && service.SetCurrentTriggerScope(false), "direct settings after commit");
    service.ReleaseControl();
    Expect(!service.SelectMeasurement("Alternate") && !service.SetCurrentTriggerScope(true),
        "direct writes require control");
    Expect(service.Settings().selectedMeasurement == "Trc1" && !service.Settings().currentTriggerScope,
        "unauthorized direct settings must not change state");
}

void EmbeddedNulNumeric(const std::string &driver)
{
    NativeFixture fixture(driver);
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "*CLS");
    const auto revision = fixture.service.Settings().revision;
    const auto originalStart = fixture.service.Settings().startHz;
    std::string input = "SENS1:FREQ:STAR 2";
    input.push_back('\0');
    input += "garbage";
    Expect(Command(engine, input).empty(), "invalid numeric write must not reply ACK");
    Expect(fixture.service.Settings().revision == revision && fixture.service.Settings().startHz == originalStart,
        "NUL cannot truncate numeric suffix and mutate configuration");
    Expect(Command(engine, "SYST:ERR?").find("-104,") == 0U, "embedded NUL numeric data error");
    double value = 0;
    Expect(!VnaScpi::Numeric(std::string("2\0garbage", 9U), value, true), "numeric helper checks entire input bytes");
}

void BootstrapPreparationFailureCanRetry(const std::string &)
{
    auto owned = std::make_unique<VnaServiceTest::ControlledDevice>();
    auto *device = owned.get();
    VnaCore::AcquisitionCore core(std::move(owned));
    VnaService::InstrumentService service(core);
    Expect(core.Initialize().get().ok, "bootstrap device initialization");
    device->FailNextPlanPreparation();
    std::string error;
    Expect(!service.BootstrapMockProfile(error), "first bootstrap must report preparation failure");
    Expect(!error.empty() && core.ReadView().state == VnaCore::CoreState::READY,
        "preparation failure must leave the device retryable");
    Expect(service.Settings().measurements.empty() && service.Settings().selectedMeasurement.empty() &&
               service.Settings().revision == 0U,
        "failed bootstrap must not publish a partial default catalog");
    Expect(service.BootstrapMockProfile(error), "bootstrap retry must succeed");
    Expect(service.Settings().measurements.size() == 1U && service.Settings().measurements.front().name == "Trc1" &&
               service.Settings().measurements.front().Parameter() == "S21" &&
               service.Settings().selectedMeasurement == "Trc1" &&
               service.Settings().revision == core.ReadView().configRevision,
        "retry must commit one default measurement and the successful revision");
    Expect(!service.BootstrapMockProfile(error) && service.Settings().measurements.size() == 1U,
        "an initialized profile must reject repeated bootstrap without catalog mutation");
}

void InvalidServiceEnumsDoNotMutate(const std::string &)
{
    VnaServiceTest::Fixture fixture;
    auto &service = fixture.service;
    Expect(service.TryAcquireControl(), "typed enum validation control");
    const auto settings = service.Settings();
    const auto before = fixture.core.ReadView();
    const auto source = service.SetTriggerSource(static_cast<VnaService::TriggerSource>(99));
    const auto mode = service.SetSweepMode(static_cast<VnaService::SweepMode>(99));
    Expect(source.wait_for(std::chrono::seconds(0)) == std::future_status::ready &&
               mode.wait_for(std::chrono::seconds(0)) == std::future_status::ready && !source.get().ok &&
               !mode.get().ok && source.get().errorCode == -224 && mode.get().errorCode == -224 &&
               !service.HasPending(),
        "invalid enum values must return immediate parameter rejections without queue admission");
    service.Poll();
    const auto after = fixture.core.ReadView();
    Expect(service.Settings().triggerSource == settings.triggerSource &&
               service.Settings().sweepMode == settings.sweepMode && service.Settings().revision == settings.revision &&
               after.state == before.state && after.runId == before.runId &&
               after.configRevision == before.configRevision && after.frame == before.frame,
        "rejected enum values must not change committed settings or device state");
    Expect(
        service.PopError().code == -224 && service.PopError().code == -224, "standard parameter errors are recorded");
    Expect(AwaitOperation(service, service.RunSingle("SCPI")).ok && fixture.core.ReadView().frame &&
               service.Settings().sweepMode == VnaService::SweepMode::HOLD,
        "a valid finite operation must still complete after enum rejection");
    service.ReleaseControl();
}
}  // 匿名命名空间

int main(int argc, char **argv)
{
    VnaTest::ScopedSimulatorEnvironment environment("golden", nullptr);
    if (argc != 2) {
        std::cerr << "native simulator library path required\n";
        return 2;
    }
    const std::string driver = argv[1];
    unsigned failed = 0;
    Run("parser and compound responses", ParserAndCompoundResponse, driver, failed);
    Run("configuration and revision", ConfigurationAndRevision, driver, failed);
    Run("标准双端口 S 参数定义及错误合同", TwoPortSParameterDefinitions, driver, failed);
    Run("频率查询及设置遵守仪器能力", FrequencyQueriesFollowInstrumentProfile, driver, failed);
    Run("single frame and receiver mapping", SingleFrameAndReceivers, driver, failed);
    Run("pending barriers and errors", PendingBarriersAndErrors, driver, failed);
    Run("busy admission and old run", BusyAdmissionAndOldRun, driver, failed);
    Run("disconnect and persistent instrument errors", DisconnectAndInstrumentErrors, driver, failed);
    Run("software bounds", SoftwareBounds, driver, failed);
    Run("blocked finite operation and OPC budget", BlockedFiniteAndWatchBudget, driver, failed);
    Run("failed single preserves old frame but rejects data", FailedSingleCannotReturnOldFrame, driver, failed);
    Run("malformed query headers", MalformedHeaders, driver, failed);
    Run("invalid receiver rejects curve, valid receiver and next scan survive", InvalidReceiverCannotProduceCurve,
        driver, failed);
    Run("configuration and direct settings cannot overwrite", ConfigurationPreservesDirectSettings, driver, failed);
    Run("embedded NUL numeric suffix", EmbeddedNulNumeric, driver, failed);
    Run("bootstrap preparation failure permits an atomic retry", BootstrapPreparationFailureCanRetry, driver, failed);
    Run("invalid service enum values reject before device mutation", InvalidServiceEnumsDoNotMutate, driver, failed);
    return failed == 0U ? 0 : 1;
}
