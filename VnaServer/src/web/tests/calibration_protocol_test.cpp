/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include <chrono>
#include <cmath>
#include <iostream>
#include <memory>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "calibration_history_test.h"
#include "calibration_protocol_test_support.h"
#include "fixture.h"
#include "scpi/scpi_engine.h"
#include "web/calibration_binary.h"
#include "web/calibration_file_json.h"
#include "web/calibration_json.h"
#include "web/http_api.h"
#include "web/json_encoder.h"

namespace {
using namespace VnaCalibrationProtocolTest;
using VnaWeb::HttpApi;
using VnaWeb::HttpResponse;
using VnaWeb::Json;
constexpr double RAW_REAL = 0.2776235288454596;
constexpr double RAW_IMAG = -0.3626684156056705;

void Near(double value, double expected)
{
    Require(std::isfinite(value) && std::abs(value - expected) < 1e-12, "Calibration numeric path differs");
}

std::string Query(VnaScpi::ScpiEngine &engine, const std::string &line)
{
    Require(engine.SubmitLine(line), "SCPI calibration command rejected");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "SCPI calibration timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (engine.HasPending());
    return engine.TakeOutput();
}

VnaService::InstrumentProfile Profile()
{
    VnaService::InstrumentProfile profile;
    profile.defaultPoints = 3U;
    return profile;
}

void SetReflection(VnaServiceTest::Fixture &fixture, double real, double imag)
{
    std::vector<double> iq(16U, 0.0);
    for (unsigned receiver = 0U; receiver < 4U; ++receiver) {
        iq[receiver * 4U] = 1.0;
        iq[receiver * 4U + 2U] = real;
        iq[receiver * 4U + 3U] = imag;
    }
    fixture.device->SetSamples(std::move(iq));
}

Json SetDocument(VnaServiceTest::Fixture &fixture)
{
    const auto frame = fixture.service.Snapshot().core.frame;
    Require(static_cast<bool>(frame), "Calibration fixture has a raw frame");
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->port1 = 1U;
    data->frequencyHz = frame->plan.frequencyHz;
    const VnaCalibration::OnePortTerms terms{{0.02, 0.01}, {-0.1, 0.03}, {0.92, -0.08}};
    data->onePortTerms.assign(data->frequencyHz.size(), terms);
    VnaService::CalibrationSet set;
    set.id = 1U;
    set.name = "Fixed external vector";
    set.deviceIdentity = fixture.service.Identity();
    set.ifBandwidthHz = frame->plan.ifBandwidthRaw;
    set.powerDbm = frame->plan.powerDbm;
    set.data = data;
    return VnaWeb::CalibrationFileJson::Export(set, "binary");
}

std::string ImportAndEnableSet(HttpApi &api, const std::string &token, const Json &document)
{
    auto imported = Action(api, "importFile", "import");
    for (const auto *field : {"format", "encoding", "content"}) {
        imported[field] = Field(document, field);
    }
    Require(Request(api, "/api/v1/calibration", imported).status == 403U, "Observers cannot import calibration");
    const auto accepted = Request(api, "/api/v1/calibration", imported, token);
    Complete(api, accepted, token);
    Require(Field(Request(api, "/api/v1/calibration").json, "selectedSetId").string == "0", "Import is not activation");
    Require(Field(Request(api, "/api/v1/calibration", imported, token).json, "operationId").string ==
                Field(accepted.json, "operationId").string,
        "Retry precedes revision conflict");
    imported["requestId"] = Json("stale-import");
    Require(Request(api, "/api/v1/calibration", imported, token).status == 409U, "Stale calibration CAS rejected");
    const auto setId = Field(Field(Request(api, "/api/v1/calibration").json, "sets").array.front(), "id").string;
    auto choose = Action(api, "select", "choose");
    choose["setId"] = Json(setId);
    Complete(api, Request(api, "/api/v1/calibration", choose, token), token);
    auto enable = Action(api, "enable", "enable");
    enable["enabled"] = Json(true);
    Complete(api, Request(api, "/api/v1/calibration", enable, token), token);
    return setId;
}

void VerifyScpiCorrection(VnaService::InstrumentService &service)
{
    VnaScpi::ScpiEngine observer(service);
    Require(
        !observer.HasControl() && Query(observer, "SENSE1:CORRECTION:STATE?") == "1\n", "SCPI reads Web calibration");
    for (const auto kind : {"RDATA", "SDATA"}) {
        auto values = Query(observer, std::string("CALC1:DATA? ") + kind);
        for (char &character : values) {
            if (character == ',') {
                character = ' ';
            }
        }
        std::istringstream input(values);
        double real = 0.0;
        double imag = 0.0;
        input >> real >> imag;
        Near(real, std::string(kind) == "RDATA" ? RAW_REAL : 0.3);
        Near(imag, std::string(kind) == "RDATA" ? RAW_IMAG : -0.4);
    }
}

void WebAndScpiReadSameCorrection()
{
    VnaServiceTest::Fixture fixture(Profile());
    SetReflection(fixture, RAW_REAL, RAW_IMAG);
    HttpApi api(fixture.service);
    const auto token = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    auto define = Json::MakeObject();
    define["requestId"] = Json("reflect");
    define["expectedRevision"] = Field(Request(api, "/api/v1/status").json, "revision");
    define["name"] = Json("Reflect");
    define["parameter"] = Json("S11");
    Complete(api, Request(api, "/api/v1/measurements", define, token), token);
    auto select = Json::MakeObject();
    select["requestId"] = Json("select-reflect");
    select["expectedDisplayRevision"] = Field(Field(Request(api, "/api/v1/status").json, "display"), "revision");
    select["action"] = Json("select");
    select["traceName"] = Json("Reflect");
    Complete(api, Request(api, "/api/v1/display", select, token), token);
    auto single = Json::MakeObject();
    single["requestId"] = Json("single");
    single["action"] = Json("single");
    Complete(api, Request(api, "/api/v1/operations", single, token), token);
    const auto rawFrame = Request(api, "/api/v1/frame?trace=Reflect").json;
    const auto measurementRevision = Field(Request(api, "/api/v1/status").json, "revision").string;
    const auto setId = ImportAndEnableSet(api, token, SetDocument(fixture));
    const auto corrected = Request(api, "/api/v1/frame?trace=Reflect").json;
    const auto &trace = Field(corrected, "traces").array.front();
    Require(Field(trace, "calibration").string == "calibrated", "Frame labels actual correction");
    Near(Field(trace, "real").array.front().number, 0.3);
    Near(Field(trace, "imag").array.front().number, -0.4);
    Require(Field(corrected, "frameSeq").string == Field(rawFrame, "frameSeq").string &&
                Field(corrected, "calibrationRevision").string != Field(rawFrame, "calibrationRevision").string &&
                Field(Request(api, "/api/v1/status").json, "revision").string == measurementRevision,
        "Correction changes only calibration version and reuses raw frame");
    VerifyScpiCorrection(fixture.service);
    const auto document = Request(api, "/api/v1/calibration/sets/" + setId).json;
    Require(Field(document, "format").string == "binary" &&
                VnaWeb::CalibrationFileJson::Import(document)->powerDbm.front() == 0.,
        "Default download is binary and retains physical power in dBm");
    FileTransportsPreserveWriteContract(api, token, setId);
    auto enable = Action(api, "enable", "disable");
    enable["enabled"] = Json(false);
    Complete(api, Request(api, "/api/v1/calibration", enable, token), token);
    const auto disabled = Request(api, "/api/v1/frame?trace=Reflect").json;
    Require(Field(Field(disabled, "traces").array.front(), "calibration").string == "uncalibrated", "Disable label");
    Near(Field(Field(disabled, "traces").array.front(), "real").array.front().number, RAW_REAL);
}

void ScpiMechanicalWorkflow()
{
    VnaServiceTest::Fixture fixture(Profile());
    VnaScpi::ScpiEngine engine(fixture.service);
    Query(engine, "SENS1:CORR:COLL:METH");
    Require(Query(engine, "SYST:ERR?").find("-109,") == 0U, "Missing calibration parameter uses standard error");
    Query(engine, "SENS1:CORR:STAT? ON");
    Require(Query(engine, "SYST:ERR?").find("-108,") == 0U, "Excess calibration parameter uses standard error");
    Require(Query(engine, "TRIG:SOUR MAN;:SENS1:SWE:MODE CONT;*OPC?") == "+1\n", "arm entry manual CONT");
    const auto begin = Query(engine, "SENS1:CORR:COLL:METH:DEF OSM,1;*OPC?");
    Require(begin == "+1\n", "SCPI starts OSM: " + begin);
    Require(Query(engine, "SENS1:CORR:COLL:STAT?") == "READY\n", "SCPI calibration ready");
    const std::vector<std::pair<std::string, double>> standards{{"OPEN", 1.0}, {"SHORT", -1.0}, {"MATCH", 0.0}};
    for (const auto &standard : standards) {
        SetReflection(fixture, standard.second, 0.0);
        Require(Query(engine, "SENSE1:CORRECTION:COLLECT:ACQUIRE " + standard.first + ",1;*OPC?") == "+1\n",
            "SCPI captures required raw standard");
        Require(Query(engine, "SYST:ERR?").find("0,") == 0U, "SCPI capture succeeded");
    }
    const auto standardRun = fixture.core.ReadView().runId;
    Require(Query(engine, "SENS1:CORR:COLL:SAVE 'OSM kit';*OPC?") == "+1\n", "SCPI finishes OSM");
    Require(Query(engine, "TRIG:SOUR?;:SENS1:SWE:MODE?") == "MAN;CONT\n" &&
                fixture.core.ReadView().state == VnaCore::CoreState::RUNNING &&
                fixture.core.ReadView().runId > standardRun && !fixture.service.MeasurementData().ok,
        "SAVE OPC waits for manual rearming and cannot expose its old standard frame as current DUT data");
    Require(Query(engine, "SENS1:CORR?") == "1\n" && Query(engine, "SENS1:CORR:COLL:STAT?") == "IDLE\n",
        "Successful save enables correction and closes session");
    Require(Query(engine, "SENS1:CORR:CSET:CAT?").find("OSM kit") != std::string::npos, "SCPI catalog saved set");
    Require(Query(engine, "SENS1:CORR OFF;*OPC?") == "+1\n" && Query(engine, "SENS1:CORR?") == "0\n", "SCPI disables");
    Require(Query(engine, "SENS1:CORR:COLL:METH TOSM,2,4,ON;*OPC?") == "+1\n", "SCPI arbitrary physical pair");
    Require(fixture.service.Calibration().includeIsolation, "SCPI enables isolation acquisition");
    Require(Query(engine, "SENS1:CORR:COLL:ABORT;*OPC?") == "+1\n", "SCPI cancels collection");
    Require(!fixture.service.Calibration().collecting && Query(engine, "TRIG:SOUR?;:SENS1:SWE:MODE?") == "MAN;CONT\n" &&
                fixture.core.ReadView().state == VnaCore::CoreState::RUNNING,
        "SCPI cancellation closes the workspace and restores manual entry arming");
}

void FourPortFileRoundtrip(VnaServiceTest::Fixture &fixture)
{
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->method = VnaCalibration::CalibrationMethod::FOUR_PORT;
    data->port1 = 1U;
    data->port2 = 4U;
    data->frequencyHz = fixture.service.StimulusAxis().values;
    VnaCalibration::FourPortTerms terms;
    for (auto &cell : terms) {
        cell = {{0.0, 0.0}, {0.02, -0.01}, {1.2, 0.03}};
    }
    data->fourPortTerms.assign(data->frequencyHz.size(), terms);
    VnaService::CalibrationSet set;
    set.id = 1U;
    set.name = "Four-port roundtrip";
    set.deviceIdentity = fixture.service.Identity();
    set.ifBandwidthHz = fixture.service.Settings().ifBandwidthHz;
    set.powerDbm.assign(data->frequencyHz.size(), fixture.service.Settings().powerDbm);
    set.data = data;
    auto document = VnaWeb::CalibrationFileJson::Export(set, "binary");
    auto request = Json::MakeObject();
    request["action"] = Json("importFile");
    for (const auto *field : {"format", "encoding", "content"}) {
        request[field] = Field(document, field);
    }
    const auto imported = VnaWeb::CalibrationJson::Change(request).importedSet;
    Require(imported->data->method == VnaCalibration::CalibrationMethod::FOUR_PORT &&
                imported->data->fourPortTerms.size() == data->frequencyHz.size() &&
                imported->data->fourPortTerms.back()[15U].tracking == terms[15U].tracking,
        "Four-port coefficients survive export and import");
    auto content = Field(request, "content").string;
    content.resize(content.size() - 4U);
    request["content"] = Json(content);
    bool rejected = false;
    try {
        VnaWeb::CalibrationJson::Change(request);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    Require(rejected, "Truncated four-port coefficient rows cannot be imported");
}

void FourPortWorkflowAndFile()
{
    VnaServiceTest::Fixture fixture(Profile());
    VnaScpi::ScpiEngine engine(fixture.service);
    Require(Query(engine, "SENS1:CORR:COLL:METH TOSM,1,2,3,4;*OPC?") == "+1\n", "SCPI starts explicit four-port TOSM");
    const auto state = fixture.service.Calibration();
    Require(state.method == VnaCalibration::CalibrationMethod::FOUR_PORT && state.steps.size() == 18U,
        "Four-port TOSM requires twelve reflection standards and all six THRU pairs");
    Query(engine, "SENS1:CORR:COLL:SAVE 'incomplete';*OPC?");
    Require(Query(engine, "SYST:ERR?").find("0,") != 0U && fixture.service.Calibration().selectedSetId == 0U,
        "Incomplete four-port standards cannot create an active calibration");
    Query(engine, "SENS1:CORR:COLL:ABOR;*OPC?");
    Require(Query(engine, "SENS1:CORR:COLL:METH TOSM,1,2,3,4,ON;*OPC?") == "+1\n" &&
                fixture.service.Calibration().steps.size() == 24U,
        "Four-port measured isolation requires each of the six pairs");
    Query(engine, "SENS1:CORR:COLL:ABOR;*OPC?");
    Query(engine, "SENS1:CORR:COLL:METH TOSM,1,2,4,3;*OPC?");
    Require(Query(engine, "SYST:ERR?").find("-224,") == 0U && !fixture.service.Calibration().collecting,
        "Invalid four-port physical order cannot silently change the source/receiver mapping");

    FourPortFileRoundtrip(fixture);
}

void WebFinishAndCancelRestoreContinuousAcquisition()
{
    VnaServiceTest::Fixture fixture(Profile());
    HttpApi api(fixture.service);
    const auto token = Field(Request(api, "/api/v1/control/claim", Json::MakeObject()).json, "sessionId").string;
    auto run = Json::MakeObject();
    run["requestId"] = Json("entry-continuous");
    run["action"] = Json("continuous");
    Complete(api, Request(api, "/api/v1/operations", run, token), token);
    auto begin = Action(api, "begin", "restore-begin");
    begin["method"] = Json("onePort");
    begin["port1"] = Json(1U);
    Complete(api, Request(api, "/api/v1/calibration", begin, token), token);
    const auto state = Request(api, "/api/v1/calibration").json;
    const auto sessionId = Field(state, "sessionId");
    const std::vector<double> standards{1.0, -1.0, 0.0};
    for (std::size_t index = 0U; index < standards.size(); ++index) {
        SetReflection(fixture, standards[index], 0.0);
        auto capture = Action(api, "capture", "restore-standard-" + std::to_string(index));
        capture["sessionId"] = sessionId;
        capture["stepId"] = Field(Field(state, "steps").array[index], "id");
        Complete(api, Request(api, "/api/v1/calibration", capture, token), token);
    }
    const auto standardRun = fixture.core.ReadView().runId;
    SetReflection(fixture, 0.3, -0.4);
    auto finish = Action(api, "finish", "restore-finish");
    finish["sessionId"] = sessionId;
    finish["name"] = Json("Web restore kit");
    Complete(api, Request(api, "/api/v1/calibration", finish, token), token);
    const auto status = Request(api, "/api/v1/status").json;
    const auto &config = Field(status, "config");
    Require(Field(config, "triggerSource").string == "IMM" && Field(config, "sweepMode").string == "CONT" &&
                fixture.core.ReadView().state == VnaCore::CoreState::RUNNING &&
                fixture.core.ReadView().runId > standardRun,
        "HTTP completion observes actual continuous core restoration");
    const auto dutRun = fixture.core.ReadView().runId;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!fixture.core.ReadView().frame || fixture.core.ReadView().frame->runId != dutRun) {
        Request(api, "/api/v1/status");
        Require(std::chrono::steady_clock::now() < deadline, "restored HTTP CONT publishes a new DUT frame");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    begin = Action(api, "begin", "cancel-restore-begin");
    begin["method"] = Json("onePort");
    begin["port1"] = Json(1U);
    Complete(api, Request(api, "/api/v1/calibration", begin, token), token);
    auto cancel = Action(api, "cancel", "cancel-restore");
    cancel["sessionId"] = Field(Request(api, "/api/v1/calibration").json, "sessionId");
    Complete(api, Request(api, "/api/v1/calibration", cancel, token), token);
    Require(!fixture.service.Calibration().collecting && fixture.core.ReadView().state == VnaCore::CoreState::RUNNING &&
                fixture.core.ReadView().runId > dutRun &&
                fixture.service.Settings().sweepMode == VnaService::SweepMode::CONTINUOUS,
        "HTTP normal cancellation restores continuous acquisition before reporting completion");
}

}  // 匿名命名空间

int main()
{
    try {
        WebAndScpiReadSameCorrection();
        ScpiMechanicalWorkflow();
        FourPortWorkflowAndFile();
        WebFinishAndCancelRestoreContinuousAcquisition();
        VnaCalibrationProtocolTest::CheckHistoryBudget();
        std::cout << "PASS calibration HTTP/SCPI ownership, CAS, correction and mechanical workflow\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
