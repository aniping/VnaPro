/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */
#include <chrono>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "fixture.h"
#include "scpi/scpi_engine.h"
#include "scpi/scpi_parser.h"
#include "scpi/scpi_query_policy.h"

namespace {
void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

void Finish(VnaService::InstrumentService &service, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        service.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "Channel operation timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    Require(operation.get().ok, "Channel setup failed");
}

std::string Command(VnaScpi::ScpiEngine &engine, const std::string &line)
{
    Require(engine.SubmitLine(line), "SCPI message was rejected: " + line);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    do {
        engine.Poll();
        Require(std::chrono::steady_clock::now() < deadline, "SCPI message timed out");
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    } while (engine.HasPending());
    return engine.TakeOutput();
}

std::vector<double> Numbers(std::string text)
{
    for (auto &character : text) {
        if (character == ';') {
            character = ',';
        }
    }
    std::vector<double> values;
    std::stringstream stream(text);
    std::string value;
    while (std::getline(stream, value, ',')) {
        values.push_back(std::stod(value));
    }
    return values;
}

void IndependentTargetsAndPower()
{
    VnaServiceTest::Fixture fixture;
    VnaScpi::ScpiEngine engine(fixture.service);
    VnaService::ChannelCreationOptions options;
    Finish(fixture.service, fixture.service.CreateChannel(options, fixture.service.Settings().revision,
                                fixture.service.Display().revision, "SCPI"));
    const auto trace2 = fixture.service.SelectedMeasurement(2U);
    const auto original = fixture.service.Settings(1U);
    Command(engine,
        "SENSe2:FREQuency:STARt 20MHz;STOP 30MHz;:SENS2:SWE:POIN 9;:SOURce2:POWer:LEVel:IMMediate:AMPLitude -12.5");
    Require(fixture.service.Settings(1U).startHz == original.startHz &&
                fixture.service.Settings(1U).stopHz == original.stopHz &&
                fixture.service.Settings(1U).powerDbm == original.powerDbm,
        "Numbered source and sense commands must not change another channel");
    Require(Numbers(Command(engine, "SOUR2:POW?;:SOUR2:POW:LEV? MIN;:SOUR2:POW? MAX")) ==
                std::vector<double>{-12.5, -120.0, 100.0},
        "Power aliases and MIN/MAX queries must read the addressed channel");
    Command(engine, "CALC1:PAR:SEL 'Trc1';:CALC2:FORM REAL;:CALC2:MARK1 ON;:CALC2:MARK1:X MAX");
    Require(Command(engine, "CALC2:PAR:SEL?") == VnaScpi::Quote(trace2) + "\n" &&
                Command(engine, "CALC1:FORM?;:CALC2:FORM?") == "MLOG;REAL\n" &&
                std::stod(Command(engine, "CALC2:MARK1:X?")) == 30000000.0,
        "CALC channel suffixes use their remembered trace and local stimulus range");
    Require(fixture.service.ActiveChannelId() == 1U,
        "Targeted configuration and readback must not change active channel selection");
    Require(Command(engine, "TRIG:SCOP?") == "CURR\n", "Multi-channel scope readback cannot advertise ALL support");
    Command(engine, "TRIG:SCOP ALL");
    Require(
        Command(engine, "SYST:ERR?").find("-221,") == 0U, "Unsupported all-channel trigger scope must fail explicitly");
    Command(engine, "SENS2:CORR:COLL:METH OSM,1;*OPC?");
    Require(Command(engine, "SENS1:CORR:COLL:STAT?;:SENS2:CORR:COLL:STAT?") == "IDLE;READY\n",
        "Numbered calibration commands must keep independent channel workspaces");
    Command(engine, "SENS2:CORR:COLL:ABORT;*OPC?");
    Command(engine, "SENS1:SWE:MODE SING;*OPC?");
    Command(engine, "SENS2:SWE:MODE SING;*OPC?");
    const auto axis = Numbers(Command(engine, "SENS2:X?"));
    Require(axis.size() == 9U && axis.front() == 20000000.0 && axis.back() == 30000000.0,
        "Numbered stimulus data must come from the addressed channel frame");
    Require(Numbers(Command(engine, "CALC2:DATA? RDATA")).size() == 18U,
        "Numbered data query must use the channel-local selected measurement");
    const auto frame = fixture.service.ChannelSnapshot(2U).core.frame;
    Require(frame && frame->plan.powerDbm.size() == 9U && frame->plan.powerDbm.front() == -12.5,
        "SCPI power must be present in the physical channel plan");
    Command(engine, "SOUR2:POW 101");
    Require(Command(engine, "SYST:ERR?").find("-222,") == 0U && fixture.service.Settings(2U).powerDbm == -12.5,
        "Invalid source power must fail without committing a new plan");
    Command(engine, "SENS99:FREQ:STAR 40MHz");
    Require(
        Command(engine, "SYST:ERR?").find("-222,") == 0U && fixture.service.Settings(1U).startHz == original.startHz,
        "Unknown channel must fail rather than fall back to channel one");
    Command(engine, "CALC1:PAR:SEL '" + trace2 + "'");
    Require(Command(engine, "SYST:ERR?").find("-221,") == 0U && fixture.service.ActiveChannelId() == 1U,
        "A measurement owned by another channel cannot be selected through CALC1");
    VnaScpi::ScpiEngine observer(fixture.service);
    Require(Numbers(Command(observer, "SOUR2:POW?;:SENS2:FREQ:STAR?")) == std::vector<double>{-12.5, 20000000.0},
        "Observer policy must allow safe numbered source and sense queries");
}

void RoutedHeadersKeepWrittenParents()
{
    std::vector<std::string> parent;
    const auto parsed = VnaScpi::ParseCommand("SOURce2:POWer:LEVel -7", parent);
    const auto routed = VnaScpi::RouteChannelCommand(parsed);
    const auto relative = VnaScpi::RouteChannelCommand(VnaScpi::ParseCommand("IMMediate?", parent));
    Require(parsed.path == "SOUR2:POW:LEV" && routed.path == "SOUR1:POW:LEV" && routed.channelId == 2U &&
                relative.path == "SOUR1:POW:IMM" && relative.channelId == 2U,
        "Channel routing must preserve the actual relative SCPI header parent");
    parent.clear();
    const auto source = VnaScpi::RouteChannelCommand(VnaScpi::ParseCommand("SOUR:POW?", parent));
    Require(source.channelId == 1U && source.path == "SOUR1:POW", "Omitted source suffix defaults to channel one");
    parent.clear();
    const auto invalid = VnaScpi::RouteChannelCommand(VnaScpi::ParseCommand("SENS0:FREQ:STAR?", parent));
    Require(invalid.errorCode == -222 && !VnaScpi::IsObserverQuery(invalid), "Zero channel suffix must be rejected");
}
}  // 匿名命名空间

int main()
{
    try {
        RoutedHeadersKeepWrittenParents();
        IndependentTargetsAndPower();
        std::cout << "Channel SCPI tests passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
