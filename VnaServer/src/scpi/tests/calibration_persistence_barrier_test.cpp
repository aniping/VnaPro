/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>

#include "fixture.h"
#include "scpi/scpi_engine.h"

namespace {
void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Drain(VnaScpi::ScpiEngine &engine)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (engine.HasPending()) {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Persistence barrier did not terminate");
        std::this_thread::yield();
    }
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &text)
{
    Require(engine.SubmitLine(text), "SCPI message rejected");
    Drain(engine);
    return engine.TakeOutput();
}

VnaService::CalibrationSetPtr MakeSet(VnaService::InstrumentService &service)
{
    auto set = std::make_shared<VnaService::CalibrationSet>();
    set->name = "Saved non-unit coefficients";
    set->deviceIdentity = service.Identity();
    set->ifBandwidthHz = service.Settings().ifBandwidthHz;
    auto data = std::make_shared<VnaCalibration::CalibrationData>();
    data->frequencyHz = service.StimulusAxis().values;
    data->onePortTerms.assign(data->frequencyHz.size(), {{0.01, 0.02}, {0.0, 0.0}, {1.3, 0.04}});
    set->powerDbm.assign(data->frequencyHz.size(), service.Settings().powerDbm);
    set->data = data;
    return set;
}

void SavedPoolIsCommittedOnlyAfterDurableCompletion()
{
    VnaServiceTest::Fixture fixture;
    std::promise<VnaService::OperationResult> firstSave;
    auto pending = firstSave.get_future().share();
    unsigned saves = 0U;
    fixture.service.SetCalibrationSaveHandler(
        [&](unsigned channel, std::vector<VnaService::CalibrationSetPtr> sets, std::uint64_t selected) {
            Require(channel == 1U && sets.size() == 1U && sets.front()->id == 1U,
                "Saver must receive the immutable candidate with its assigned stable ID");
            Require(selected == (saves == 0U ? 0U : 1U), "Saver must receive the candidate selection");
            ++saves;
            return pending;
        });
    VnaScpi::ScpiEngine engine(fixture.service);
    VnaService::CalibrationChange change;
    change.kind = VnaService::CalibrationChangeKind::IMPORT_SET;
    change.importedSet = MakeSet(fixture.service);
    const auto operation = fixture.service.ApplyCalibration(change);
    Require(engine.SubmitLine("*OPC?"), "OPC accepted behind a calibration import");
    for (unsigned count = 0U; count < 10U; ++count) {
        engine.Poll();
    }
    Require(saves == 1U && operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready &&
                fixture.service.Calibration().sets.empty() && engine.TakeOutput().empty(),
        "Import and OPC cannot succeed before persistence completion or expose an uncommitted candidate");
    VnaScpi::ScpiEngine observer(fixture.service);
    Require(Command(observer, "*IDN?").find("VnaPro,") == 0U,
        "A pending disk write must not block ordinary observer queries");
    firstSave.set_value({true, 0, ""});
    Drain(engine);
    Require(operation.get().ok && engine.TakeOutput() == "+1\n" && fixture.service.Calibration().sets.size() == 1U,
        "Successful durable completion commits the pool and resolves OPC");

    std::promise<VnaService::OperationResult> failedSave;
    pending = failedSave.get_future().share();
    Require(engine.SubmitLine("SENS1:CORR:CSET:ACT 'Saved non-unit coefficients',0;*OPC?"), "Activation accepted");
    for (unsigned count = 0U; count < 10U; ++count) {
        engine.Poll();
    }
    Require(saves == 2U && fixture.service.Calibration().selectedSetId == 0U && engine.TakeOutput().empty(),
        "Pending activation must preserve the old committed selection");
    failedSave.set_value({false, -310, "simulated disk failure"});
    Drain(engine);
    Require(engine.TakeOutput() == "+1\n", "OPC reports completion even when the operation failed");
    Require(Command(engine, "SYST:ERR?").find("-310,") == 0U && fixture.service.Calibration().selectedSetId == 0U &&
                !fixture.service.Calibration().enabled && fixture.service.Calibration().sets.size() == 1U,
        "Save failure must enter the SCPI FIFO and preserve the previous valid pool and selection");
}
}  // 匿名命名空间

int main()
{
    try {
        SavedPoolIsCommittedOnlyAfterDurableCompletion();
        std::cout << "PASS asynchronous calibration persistence and OPC/error commit barriers\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL " << error.what() << '\n';
        return 1;
    }
}
