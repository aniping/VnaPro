#include <chrono>
#include <cmath>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <thread>

#include "../../../../VnaNativeSimulator/tests/SimulatorEnvironment.h"
#include "Fixture.h"
#include "NativeFixture.h"
#include "scpi/ScpiEngine.h"
#include "scpi/ScpiParser.h"
#include "vna_core/AcquisitionCore.h"
#include "vna_service/InstrumentService.h"

namespace {
using vna_scpi_test::NativeFixture;
std::string driver;

void expect(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

void near(double value, double expected)
{
    expect(std::fabs(value - expected) < 1e-10, "numeric mismatch: " + std::to_string(value));
}

void drain(vna_scpi::ScpiEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    do {
        engine.poll();
        if (!engine.hasPending()) {
            return;
        }
        expect(std::chrono::steady_clock::now() < deadline, "engine drain timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (true);
}

std::string command(vna_scpi::ScpiEngine &engine, const std::string &text)
{
    expect(engine.submitLine(text), "submit failed");
    drain(engine);
    return engine.takeOutput();
}

std::vector<double> numbers(const std::string &text)
{
    std::vector<double> values;
    std::stringstream stream(text);
    std::string item;
    while (std::getline(stream, item, ',')) {
        values.push_back(std::stod(item));
    }
    return values;
}

void run(const char *name, void (*test)(), unsigned &failed)
{
    try {
        test();
        std::cout << "PASS " << name << '\n';
    } catch (const std::exception &error) {
        ++failed;
        std::cerr << "FAIL " << name << ": " << error.what() << '\n';
    }
}

void parserAndCompoundResponse()
{
    NativeFixture fixture(driver);
    vna_scpi::ScpiEngine engine(fixture.service);
    expect(command(engine, "*IDN?;*OPC?") == "VnaPro,NativeSimulator,4PORT,1.0;+1\n", "compound response boundary");
    const auto result = command(engine, "SENSe1:FREQuency:STARt 2MHz;STOP 3e6;STARt?;STOP?");
    const auto delimiter = result.find(';');
    near(std::stod(result.substr(0, delimiter)), 2000000.0);
    near(std::stod(result.substr(delimiter + 1U)), 3000000.0);
    expect(command(engine, "CALC1:PAR:DEF:EXT 'W;A,1','a1,1';:CALC1:PAR:SEL 'W;A,1';SEL?") == "\"W;A,1\"\n",
        "quoted delimiters and relative path");
    command(engine, "*CLS;:VNAP:DATA:SNAP");
    expect(command(engine, "SYST:ERR?").find("-113,") == 0U, "custom header must fail");
}

void configurationAndRevision()
{
    NativeFixture fixture(driver);
    expect(fixture.service.tryAcquireControl(), "control");
    const auto revision = fixture.service.settings().revision;
    const auto first = fixture.service.setStartHz(150000000.0, revision);
    const auto stale = fixture.service.setStopHz(200000000.0, revision);
    while (first.wait_for(std::chrono::seconds(0)) != std::future_status::ready ||
           stale.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.poll();
        std::this_thread::yield();
    }
    expect(first.get().ok, "first revision change");
    expect(!stale.get().ok && stale.get().errorCode == -221, "execution revision revalidation");
    near(fixture.service.settings().startHz, 150000000.0);
    near(fixture.service.settings().stopHz, 249000000.0);
    const auto prior = fixture.service.settings().revision;
    auto outOfRange = fixture.service.setStopHz(100.0);
    while (outOfRange.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.poll();
    }
    expect(!outOfRange.get().ok, "coupled start out of range");
    expect(fixture.service.settings().revision == prior, "failed candidate must not commit");
    vna_service::Measurement typed;
    typed.name = "UpperCaseTyped";
    typed.receiverKind = 'B';
    const auto defined = fixture.service.defineMeasurement(typed);
    while (defined.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        fixture.service.poll();
    }
    expect(defined.get().ok && fixture.service.settings().measurements.back().receiverKind == 'b',
        "typed receiver letters normalize while measurement name is preserved");
    fixture.service.releaseControl();
    vna_scpi::ScpiEngine engine(fixture.service);
    command(engine, "*CLS;SENS1:BAND 101Hz");
    near(std::stod(command(engine, "SENS1:BAND?")), 1000.0);
    expect(numbers(command(engine, "SENS1:X?")).size() == 201U, "axis before acquisition");
    expect(command(engine, "CALC1:DATA? RDATA").empty(), "RDATA before acquisition must not fabricate data");
    expect(command(engine, "SYST:ERR?").find("-230,") == 0U, "missing data error");
}

void singleFrameAndReceivers()
{
    NativeFixture fixture(driver);
    vna_scpi::ScpiEngine engine(fixture.service);
    command(engine, "*CLS;SENS1:SWE:POIN 21");
    command(engine, "CALC1:PAR:DEF:EXT 'wave_a','A1,2';:CALC1:PAR:DEF:EXT 'wave_b','B1,2'");
    expect(command(engine, "TRIG:SOUR IMM;:SENS1:SWE:MODE SING;*OPC?;:SENS1:SWE:MODE?") == "+1;HOLD\n",
        "single must end in hold");
    const auto frame = fixture.core.readView().frame;
    expect(frame && frame->ports.size() == 2U, "all required source ports must publish together");
    command(engine, "CALC1:PAR:SEL 'wave_a'");
    const auto a = numbers(command(engine, "CALC1:DATA? RDATA"));
    expect(a.size() == 42U, "2N A array");
    near(a.front(), 1.12);
    command(engine, "CALC1:PAR:SEL 'wave_b'");
    const auto b = numbers(command(engine, "CALC1:DATA? RDATA"));
    expect(b.size() == 42U && b.front() > 0.04 && b.front() < 0.06, "source2 receiver1 B must be distinct");
    expect(fixture.core.readView().frame == frame, "selection must not acquire or alter frame");
    expect(command(engine, "CALC1:PAR:CAT:EXT?").find("wave_a,a1_2,wave_b,b1_2") != std::string::npos,
        "extended catalog receiver comma encoding");
    command(engine, "SENS1:FREQ:STAR 2MHz");
    expect(command(engine, "CALC1:DATA? RDATA").empty(), "old revision frame must be rejected");
}

void pendingBarriersAndErrors()
{
    NativeFixture fixture(driver);
    vna_scpi::ScpiEngine engine(fixture.service);
    command(engine, "*CLS;*ESE 1;*SRE 32");
    expect(engine.submitLine("SENS1:SWE:MODE SING;*OPC;*WAI;*STB?;*ESR?;*ESR?"), "single submit");
    engine.poll();
    expect(engine.takeOutput().empty(), "barrier cannot complete on admission");
    expect(engine.hasPending(), "finite operation pending");
    drain(engine);
    expect(engine.takeOutput() == "96;1;0\n", "OPC event/status lifecycle");
    expect(engine.submitLine("SENS1:SWE:MODE SING;*WAI;:BOGUS;:SYST:ERR?"), "ordered error submit");
    engine.poll();
    expect(fixture.service.errorCount() == 0U, "error must wait for prior barrier");
    drain(engine);
    expect(engine.takeOutput().find("-113,") == 0U, "error after barrier");
    command(engine, "*CLS;TRIG:SOUR MAN;:SENS1:SWE:MODE CONT");
    expect(command(engine, "INIT1:IMM;*OPC?") == "+1\n", "manual INIT finite completion");
    command(engine, "INIT:CONT OFF");
    expect(command(engine, "SENS1:SWE:MODE?") == "CONT\n", "INIT CONT OFF is not HOLD");
    command(engine, "SENS1:SWE:MODE HOLD");
    command(engine, "*CLS;TRIG:SOUR IMM;:SENS1:SWE:MODE CONT");
    command(engine, "INIT1:IMM");
    expect(command(engine, "SYST:ERR?").find("-221,") == 0U, "INIT rejects internal trigger source");
    expect(
        command(engine, "SENS1:SWE:MODE?") == "CONT\n" && fixture.core.readView().state == vna_core::CoreState::Running,
        "invalid INIT must not change the actual continuous mode");
    command(engine, "SENS1:SWE:MODE HOLD");
}

void busyAdmissionAndOldRun()
{
    NativeFixture fixture(driver);
    vna_scpi::ScpiEngine engine(fixture.service);
    command(engine, "*CLS;SENS1:SWE:MODE SING;*OPC?");
    const auto revision = fixture.service.settings().revision;
    expect(engine.submitLine("SENS1:SWE:MODE SING;:SENS1:FREQ:STAR 2MHz;:CALC1:DATA? RDATA"), "busy sequence");
    engine.poll();
    expect(fixture.service.errorCount() >= 1U, "configuration must reject pending single at admission");
    expect(fixture.service.settings().revision == revision, "busy config must not enqueue for later HOLD");
    drain(engine);
    expect(engine.takeOutput().empty(), "query during new finite run must not use old frame");
    expect(fixture.service.settings().revision == revision, "busy config must not commit after single");
}

void disconnectAndInstrumentErrors()
{
    NativeFixture fixture(driver);
    {
        vna_scpi::ScpiEngine engine(fixture.service);
        vna_scpi::ScpiEngine refused(fixture.service);
        expect(!refused.hasControl() && !refused.submitLine("*IDN?"), "second controller must be rejected");
        expect(engine.submitLine("*CLS;SENS1:SWE:MODE SING;*OPC?;:UNSUPPORTED"), "accepted disconnected input");
        engine.poll();
        engine.disconnect();
        expect(!engine.canReleaseControl(), "disconnect must retain accepted operations");
        drain(engine);
        expect(engine.canReleaseControl(), "drained connection must release control");
        expect(engine.takeOutput().empty(), "disconnected output discarded");
    }
    vna_scpi::ScpiEngine next(fixture.service);
    expect(next.hasControl(), "new controller after barrier");
    expect(command(next, "SYST:ERR?").find("-113,") == 0U, "global error FIFO survives client replacement");
    next.setTransportOutputPending(true);
    expect(command(next, "*STB?") == "16\n", "MAV includes transport output");
}

void softwareBounds()
{
    NativeFixture fixture(driver);
    vna_scpi::ScpiEngine engine(fixture.service);
    expect(!engine.submitLine(std::string(16385U, 'A')), "line bound");
    for (unsigned index = 0; index < 32U; ++index) {
        expect(engine.submitLine("*IDN?"), "bounded input accepted");
    }
    expect(!engine.submitLine("*IDN?"), "input queue bound");
    drain(engine);
    while (!engine.takeOutput().empty()) {
    }
    command(engine, "SENS1:SWE:POIN 10001;:SENS1:SWE:MODE SING;*OPC?");
    const auto data = command(engine, "CALC1:DATA? RDATA");
    expect(data.size() < 1024U * 1024U && numbers(data).size() == 20002U, "full bounded ASCII array");
}

void blockedFiniteAndWatchBudget()
{
    vna_service_test::Fixture fixture;
    vna_scpi::ScpiEngine engine(*fixture.service);
    command(engine, "*CLS;SENS1:SWE:MODE SING;*OPC?");
    const auto old = fixture.core->readView().frame;
    fixture.device->arm();
    expect(engine.submitLine("SENS1:SWE:MODE SING"), "blocked single");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->hasEntered()) {
        engine.poll();
        expect(std::chrono::steady_clock::now() < deadline, "controlled scan never entered");
        std::this_thread::yield();
    }
    expect(fixture.core->readView().frame == old, "blocked scan cannot publish");
    expect(!fixture.service->measurementData().ok, "pending scan cannot reuse old run frame");
    for (unsigned index = 0; index < 40U; ++index) {
        expect(engine.submitLine("*OPC"), "OPC message accepted");
        engine.poll();
    }
    expect(fixture.service->errorCount() == 8U, "pending OPC watches must be bounded at 32");
    expect(fixture.service->popError().code == -363, "watch overflow error");
    expect(engine.submitLine("*OPC?"), "pending query");
    engine.poll();
    engine.disconnect();
    for (unsigned tick = 0; tick < 10U; ++tick) {
        engine.poll();
    }
    expect(!engine.canReleaseControl(), "disconnect must not release blocked device I/O");
    fixture.device->release();
    drain(engine);
    expect(engine.canReleaseControl(), "blocked task completed and released");
    expect(fixture.core->readView().frame != old, "new complete frame must eventually replace old");
}

void failedSingleCannotReturnOldFrame()
{
    vna_service_test::Fixture fixture;
    vna_scpi::ScpiEngine engine(*fixture.service);
    command(engine, "*CLS;SENS1:SWE:MODE SING;*OPC?");
    const auto old = fixture.core->readView().frame;
    fixture.device->arm(true);
    expect(engine.submitLine("SENS1:SWE:MODE SING;*OPC?"), "failing single");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->hasEntered()) {
        engine.poll();
        expect(std::chrono::steady_clock::now() < deadline, "failure gate");
        std::this_thread::yield();
    }
    fixture.device->release();
    drain(engine);
    expect(engine.takeOutput() == "+1\n", "failed operation must still terminate OPC barrier");
    expect(fixture.core->readView().frame == old, "failed single must retain old immutable frame");
    expect(command(engine, "CALC1:DATA? RDATA").empty(), "failed single cannot query old curve");
    expect(fixture.service->errorCount() >= 2U, "operation and data failure diagnostics");
}

void malformedHeaders()
{
    NativeFixture fixture(driver);
    vna_scpi::ScpiEngine engine(fixture.service);
    command(engine, "*CLS");
    expect(command(engine, "?").empty(), "bare query has no fake output");
    expect(command(engine, "SYST:ERR?").find("-102,") == 0U, "bare query syntax error");
    command(engine, "INIT:");
    expect(command(engine, "SYST:ERR?").find("-102,") == 0U, "trailing colon must not execute INIT");
}

void invalidReceiverCannotProduceCurve()
{
    vna_service_test::Fixture fixture;
    vna_scpi::ScpiEngine engine(*fixture.service);
    command(engine, "*CLS;CALC1:PAR:DEF:EXT 'HealthyB','b1,1'");
    fixture.device->setInvalidSamples(true);
    expect(command(engine, "SENS1:SWE:MODE SING;*OPC?") == "+1\n", "Invalid-data scan still terminates");
    const auto held = fixture.core->readView().frame;
    expect(command(engine, "CALC1:DATA? RDATA").empty(), "Invalid receiver must not return a partial or fake curve");
    expect(command(engine, "SYST:ERR?").find("-230,") == 0U, "Invalid receiver gives standard data error");
    command(engine, "CALC1:PAR:SEL 'HealthyB'");
    const auto healthy = numbers(command(engine, "CALC1:DATA? RDATA"));
    expect(healthy.size() == 402U, "Another receiver on the same frame remains readable");
    for (const double sample : healthy) {
        near(sample, 1.11);
    }
    expect(command(engine, "SYST:ERR?").find("0,") == 0U, "Healthy receiver adds no error");
    expect(fixture.core->readView().frame == held, "Receiver selection cannot rewrite the held frame");
    fixture.device->setInvalidSamples(false);
    command(engine, "CALC1:PAR:SEL 'DefaultA'");
    expect(command(engine, "SENS1:SWE:MODE SING;*OPC?") == "+1\n", "Following valid scan completes");
    const auto restored = numbers(command(engine, "CALC1:DATA? RDATA"));
    expect(restored.size() == 402U, "Valid scan restores a complete receiver curve");
    for (const double sample : restored) {
        near(sample, 1.11);
    }
    expect(fixture.core->readView().frame != held, "Valid scan publishes a new snapshot");
}

void configurationPreservesDirectSettings()
{
    vna_service_test::Fixture fixture;
    auto &service = *fixture.service;
    expect(service.tryAcquireControl(), "typed control");
    vna_service::Measurement alternate;
    alternate.name = "Alternate";
    alternate.receiverKind = 'b';
    const auto defined = service.defineMeasurement(alternate);
    while (defined.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.poll();
    }
    expect(defined.get().ok, "alternate receiver definition");
    expect(service.selectMeasurement("Alternate") && service.setCurrentTriggerScope(true), "initial direct settings");
    fixture.device->armPlan();
    const auto configured = service.setPoints(301U);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.device->hasPlanEntered()) {
        service.poll();
        expect(std::chrono::steady_clock::now() < deadline, "configuration gate not entered");
        std::this_thread::yield();
    }
    expect(!service.selectMeasurement("DefaultA"), "SEL must reject an in-flight candidate snapshot");
    expect(!service.setCurrentTriggerScope(false), "scope must reject an in-flight candidate snapshot");
    expect(service.popError().code == -221 && service.popError().code == -221, "direct setting conflicts");
    fixture.device->release();
    while (configured.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.poll();
    }
    expect(configured.get().ok, "configuration completion");
    expect(service.settings().selectedMeasurement == "Alternate" && service.settings().currentTriggerScope,
        "successful prior settings cannot be overwritten by candidate commit");
    expect(
        service.selectMeasurement("DefaultA") && service.setCurrentTriggerScope(false), "direct settings after commit");
    service.releaseControl();
    expect(!service.selectMeasurement("Alternate") && !service.setCurrentTriggerScope(true),
        "direct writes require control");
    expect(service.settings().selectedMeasurement == "DefaultA" && !service.settings().currentTriggerScope,
        "unauthorized direct settings must not change state");
}

void embeddedNulNumeric()
{
    NativeFixture fixture(driver);
    vna_scpi::ScpiEngine engine(fixture.service);
    command(engine, "*CLS");
    const auto revision = fixture.service.settings().revision;
    const auto originalStart = fixture.service.settings().startHz;
    std::string input = "SENS1:FREQ:STAR 2";
    input.push_back('\0');
    input += "garbage";
    expect(command(engine, input).empty(), "invalid numeric write must not reply ACK");
    expect(fixture.service.settings().revision == revision && fixture.service.settings().startHz == originalStart,
        "NUL cannot truncate numeric suffix and mutate configuration");
    expect(command(engine, "SYST:ERR?").find("-104,") == 0U, "embedded NUL numeric data error");
    double value = 0;
    expect(!vna_scpi::numeric(std::string("2\0garbage", 9U), value, true), "numeric helper checks entire input bytes");
}
}  // namespace

int main(int argc, char **argv)
{
    vna_test::ScopedSimulatorEnvironment environment("golden", NULL);
    if (argc != 2) {
        std::cerr << "native simulator library path required\n";
        return 2;
    }
    driver = argv[1];
    unsigned failed = 0;
    run("parser and compound responses", parserAndCompoundResponse, failed);
    run("configuration and revision", configurationAndRevision, failed);
    run("single frame and receiver mapping", singleFrameAndReceivers, failed);
    run("pending barriers and errors", pendingBarriersAndErrors, failed);
    run("busy admission and old run", busyAdmissionAndOldRun, failed);
    run("disconnect and persistent instrument errors", disconnectAndInstrumentErrors, failed);
    run("software bounds", softwareBounds, failed);
    run("blocked finite operation and OPC budget", blockedFiniteAndWatchBudget, failed);
    run("failed single preserves old frame but rejects data", failedSingleCannotReturnOldFrame, failed);
    run("malformed query headers", malformedHeaders, failed);
    run("invalid receiver rejects curve, valid receiver and next scan survive", invalidReceiverCannotProduceCurve,
        failed);
    run("configuration and direct settings cannot overwrite", configurationPreservesDirectSettings, failed);
    run("embedded NUL numeric suffix", embeddedNulNumeric, failed);
    return failed == 0U ? 0 : 1;
}
