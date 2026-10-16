/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <chrono>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include "fixture.h"
#include "scpi/scpi_engine.h"
#include "scpi/scpi_parser.h"

namespace {
void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &line)
{
    Require(engine.SubmitLine(line), "SCPI activation message rejected");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "SCPI activation timed out");
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

std::shared_ptr<VnaService::CalibrationSet> Set(VnaService::InstrumentService &service, const std::string &name)
{
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->port1 = 1U;
    data->frequencyHz = service.StimulusAxis().values;
    data->onePortTerms.assign(data->frequencyHz.size(), {{0.02, 0.01}, {-0.1, 0.03}, {0.92, -0.08}});
    auto set = std::make_shared<VnaService::CalibrationSet>();
    set->name = name;
    set->deviceIdentity = service.Identity();
    set->ifBandwidthHz = service.Settings().ifBandwidthHz;
    set->powerDbm.assign(data->frequencyHz.size(), service.Settings().powerDbm);
    set->data = std::move(data);
    return set;
}

bool Import(VnaService::InstrumentService &service, VnaService::CalibrationSetPtr set)
{
    VnaService::CalibrationChange change;
    change.kind = VnaService::CalibrationChangeKind::IMPORT_SET;
    change.importedSet = std::move(set);
    const auto operation = service.ApplyCalibration(std::move(change), 0U, "SCPI");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Calibration import timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return operation.get().ok;
}

void NamedActivationUsesRealCorrection()
{
    VnaServiceTest::Fixture fixture(Profile());
    std::vector<double> samples(16U, 0.0);
    for (unsigned receiver = 0U; receiver < 4U; ++receiver) {
        samples[receiver * 4U] = 1.0;
        samples[receiver * 4U + 2U] = 0.2776235288454596;
        samples[receiver * 4U + 3U] = -0.3626684156056705;
    }
    fixture.device->SetSamples(std::move(samples));
    VnaScpi::ScpiEngine engine(fixture.service);
    Command(engine, "CALC1:PAR:DEF:EXT 'Reflect','S11';:CALC1:PAR:SEL 'Reflect';:SENS1:SWE:MODE SING;*OPC?");
    const std::string name = "Opaque 200\\300;A,\"B";
    Require(Import(fixture.service, Set(fixture.service, name)), "Real external set import failed");
    const auto id = fixture.service.Calibration().sets.back().id;
    const auto settings = fixture.service.Settings();
    const auto frame = fixture.core.ReadView().frame;
    Require(!fixture.service.Calibration().enabled, "Import must not activate correction");
    Require(Command(engine, "SENS1:CORR:CSET:ACT " + VnaScpi::Quote(name) + ",0;*OPC?") == "+1\n",
        "Opaque named activation must complete through the operation barrier");
    const auto selected = fixture.service.Calibration();
    Require(
        selected.selectedSetId == id && selected.enabled && selected.compatible &&
            Command(engine, "SENS1:CORR?;:SENS1:CORR:CSET:ACT?") == "1;" + VnaScpi::Quote(std::to_string(id)) + "\n",
        "ACT must select and enable the real set");
    Require(Command(engine, "SENS1:CORR:CSET:CAT?").find(VnaScpi::Quote(name)) != std::string::npos,
        "Catalog must expose the actual escaped set name");
    const auto corrected = Command(engine, "CALC1:DATA? SDATA");
    const auto comma = corrected.find(',');
    Require(std::abs(std::stod(corrected) - 0.3) < 1e-12 &&
                std::abs(std::stod(corrected.substr(comma + 1U)) + 0.4) < 1e-12 &&
                corrected != Command(engine, "CALC1:DATA? RDATA"),
        "Named activation must apply imported coefficients to actual raw data");
    Require(fixture.service.Settings().revision == settings.revision &&
                fixture.service.Settings().acquisitionRevision == settings.acquisitionRevision &&
                fixture.service.Settings().points == settings.points && fixture.core.ReadView().frame == frame,
        "Activation must preserve the physical stimulus and held DUT frame");
    Command(engine, "SENS1:CORR OFF;:SENS1:CORR:CSET:ACT " + std::to_string(id) + ";*OPC?");
    Require(Command(engine, "SENS1:CORR?") == "1\n", "Legacy id activation must also enable correction");
}

void ImportActivationCandidates(VnaServiceTest::Fixture &fixture, VnaScpi::ScpiEngine &engine)
{
    auto mismatched = Set(fixture.service, "Different axis");
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*mismatched->data);
    data->frequencyHz.front() += 1.0;
    mismatched->data = data;
    Require(Import(fixture.service, mismatched), "Valid set on another axis should remain catalogued");
    auto fewerPoints = Set(fixture.service, "Different points");
    data = std::make_shared<VnaCalibration::CalibrationData>(*fewerPoints->data);
    data->frequencyHz.erase(data->frequencyHz.begin());
    data->onePortTerms.pop_back();
    fewerPoints->powerDbm.pop_back();
    fewerPoints->data = data;
    Require(Import(fixture.service, fewerPoints), "Valid set with another point count should remain catalogued");
    auto bandwidth = Set(fixture.service, "Different IFBW");
    bandwidth->ifBandwidthHz = 100.0;
    Require(Import(fixture.service, bandwidth), "Valid set with another IFBW should remain catalogued");
    Command(engine, "SENS1:CORR:CSET:ACT 'Different IFBW',0;*OPC?");
    Require(Command(engine, "SYST:ERR?").find("0,") == 0U && fixture.service.Calibration().effective &&
                !fixture.service.Calibration().applicability.warnings.empty(),
        "IFBW mismatch may activate with a persistent condition warning");
    Command(engine, "SENS1:CORR:CSET:ACT 'Real',0;*OPC?");
    auto power = Set(fixture.service, "Different power");
    power->powerDbm.front() = -1.0;
    Require(Import(fixture.service, power), "Valid set with another power should remain catalogued");
    auto foreign = Set(fixture.service, "Different device");
    foreign->deviceIdentity = "Another instrument";
    Require(Import(fixture.service, foreign), "Foreign set import should remain available for inspection");
    Require(Import(fixture.service, Set(fixture.service, "Duplicate")) &&
                Import(fixture.service, Set(fixture.service, "Duplicate")),
        "Existing storage permits duplicate names");
}

void CheckInvalidImportAndNewAxis(
    VnaServiceTest::Fixture &fixture, VnaScpi::ScpiEngine &engine, std::uint64_t selectedSetId)
{
    auto invalid = Set(fixture.service, "Invalid coefficients");
    auto data = std::make_shared<VnaCalibration::CalibrationData>(*invalid->data);
    data->onePortTerms.front().reflectionTracking = {};
    invalid->data = data;
    Require(!Import(fixture.service, invalid), "Invalid coefficients must never become an activatable set");
    auto invalidPort = Set(fixture.service, "Invalid port");
    data = std::make_shared<VnaCalibration::CalibrationData>(*invalidPort->data);
    data->port1 = 5U;
    invalidPort->data = data;
    Require(!Import(fixture.service, invalidPort), "Nonexistent physical ports must never become an activatable set");
    Require(Command(engine, "*CLS;:SENS1:SWE:POIN 5;:SENS1:CORR:CSET:ACT 'Real',0;*OPC?") == "+1\n" &&
                Command(engine, "SYST:ERR?").find("0,") == 0U && fixture.service.Settings().points == 5U &&
                fixture.service.Calibration().selectedSetId == selectedSetId &&
                fixture.service.Calibration().effective &&
                fixture.service.Calibration().applicability.frequencyMode ==
                    VnaService::CalibrationFrequencyMode::INTERPOLATED,
        "FIFO activation must prepare the newly committed axis and preserve the current stimulus");
}

void RejectedActivationIsAtomic()
{
    VnaServiceTest::Fixture fixture(Profile());
    VnaScpi::ScpiEngine engine(fixture.service);
    Require(Command(engine, "SENS1:CORR:CSET:ACT?") == "\"0\"\n", "No selected set has the zero id readback");
    Require(Import(fixture.service, Set(fixture.service, "Real")), "Compatible set import failed");
    Command(engine, "SENS1:CORR:CSET:ACT 'Real',0;*OPC?");
    ImportActivationCandidates(fixture, engine);
    const auto before = fixture.service.Calibration();
    const auto settings = fixture.service.Settings();
    const std::vector<std::pair<std::string, std::string>> rejected = {{"'Missing',0", "-222,"}, {"'real',0", "-222,"},
        {"'Different axis',0", "-221,"}, {"'Different points',0", "-221,"}, {"'Different power',0", "-221,"},
        {"'Different device',0", "-221,"}, {"'Duplicate',0", "-222,"}, {"'Real',1", "-224,"}, {"'Real',0,0", "-108,"}};
    for (const auto &item : rejected) {
        Require(Command(engine, "SENS1:CORR:CSET:ACT " + item.first + ";*OPC?") == "+1\n",
            "A rejected activation still terminates its completion barrier");
        Require(Command(engine, "SYST:ERR?").find(item.second) == 0U, "Activation error was not reported");
        const auto after = fixture.service.Calibration();
        Require(after.selectedSetId == before.selectedSetId && after.enabled && after.revision == before.revision &&
                    fixture.service.Settings().revision == settings.revision &&
                    fixture.service.Settings().acquisitionRevision == settings.acquisitionRevision,
            "Rejected activation must preserve calibration selection and physical configuration");
    }
    CheckInvalidImportAndNewAxis(fixture, engine, before.selectedSetId);
}
}  // 匿名命名空间

int main()
{
    try {
        NamedActivationUsesRealCorrection();
        RejectedActivationIsAtomic();
        std::cout << "Calibration activation SCPI tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
