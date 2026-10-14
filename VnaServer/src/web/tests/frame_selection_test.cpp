/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/frame_selection.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fixture.h"
#include "web/http_api.h"
#include "web/json_encoder.h"

namespace {
void Require(bool value, const std::string &message)
{
    if (!value) {
        throw std::runtime_error(message);
    }
}

std::vector<VnaService::Measurement> Catalog()
{
    std::vector<VnaService::Measurement> catalog;
    for (const auto &name : {"Raw", "S11", "S21", "S12", "反射 & +%"}) {
        VnaService::Measurement measurement;
        measurement.name = name;
        catalog.push_back(measurement);
    }
    return catalog;
}

void SelectionContract()
{
    const auto catalog = Catalog();
    Require(VnaWeb::SelectFrameMeasurements("", catalog).size() == catalog.size(), "默认帧不能截断第五条迹线");
    const auto selected = VnaWeb::SelectFrameMeasurements("trace=%E5%8F%8D%E5%B0%84%20%26%20%2B%25&trace=S12", catalog);
    Require(selected.size() == 2U && selected[0].name == catalog[4].name && selected[1].name == "S12",
        "显式选择支持中文和特殊字符，顺序与请求一致且可读取第五条目录项");
    Require(VnaWeb::SelectFrameMeasurements("display=none", catalog).empty(), "空显示集合有明确语义");
    const std::vector<std::string> invalid = {"trace=", "trace=missing", "trace=S11&trace=S11", "trace=S11&",
        "other=S11", "display=none&trace=S11", "trace=S11&display=none", "trace=%", "trace=%GG", "trace=%00",
        "trace=Raw&trace=S11&trace=S21&trace=S12&trace=missing"};
    for (const auto &query : invalid) {
        bool rejected = false;
        try {
            VnaWeb::SelectFrameMeasurements(query, catalog);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        Require(rejected, "拒绝非法查询: " + query);
    }
}

void SelectionIncludesAllBudgetedTraces()
{
    std::vector<VnaService::Measurement> catalog;
    std::string query;
    for (std::size_t number = 1U; number <= VnaDisplay::MAX_DISPLAY_TRACES; ++number) {
        VnaService::Measurement measurement;
        measurement.name = "Trc" + std::to_string(number);
        catalog.push_back(measurement);
        if (!query.empty()) {
            query += '&';
        }
        query += "trace=" + measurement.name;
    }
    const auto selected = VnaWeb::SelectFrameMeasurements(query, catalog);
    Require(selected.size() == VnaDisplay::MAX_DISPLAY_TRACES &&
                selected.back().name == "Trc" + std::to_string(VnaDisplay::MAX_DISPLAY_TRACES),
        "显式帧查询必须覆盖目录预算内所有迹线");
    Require(VnaWeb::SelectFrameMeasurements("", catalog).size() == selected.size(), "默认查询与显式满集合容量一致");
    bool rejected = false;
    try {
        VnaWeb::SelectFrameMeasurements(query + "&trace=Trc1", catalog);
    } catch (const std::invalid_argument &) {
        rejected = true;
    }
    Require(rejected, "超过项目迹线预算的查询字段必须拒绝");
}

void ValidateBeforeFirstFrame()
{
    VnaServiceTest::Fixture fixture(VnaService::InstrumentProfile{});
    VnaWeb::HttpApi api(fixture.service);
    VnaWeb::HttpRequest request;
    request.method = "GET";
    request.path = "/api/v1/frame";
    request.query = "trace=missing";
    Require(api.Handle(request).status == 400U, "没有已完成帧时也必须拒绝不存在的轨迹");
    request.query = "display=none";
    Require(api.Handle(request).status == 200U, "采集前空集合合法，仍返回无帧状态");
    request.path = "/api/v1/status";
    request.query.clear();
    const auto status = api.Handle(request);
    const auto *capabilities = status.json.Find("capabilities");
    Require(capabilities && capabilities->Find("portCount")->number == 4.0 &&
                capabilities->Find("minFrequencyHz")->number == 1e7 &&
                capabilities->Find("maxFrequencyHz")->number == 8e9,
        "状态返回实际服务能力");
}

const VnaWeb::Json &Field(const VnaWeb::Json &value, const std::string &name)
{
    const auto field = value.Find(name);
    Require(field != nullptr, "帧缺少字段：" + name);
    return *field;
}

VnaService::OperationResult Finish(VnaServiceTest::Fixture &fixture, const VnaService::Operation &operation)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (operation.wait_for(std::chrono::seconds(0)) != std::future_status::ready) {
        Require(std::chrono::steady_clock::now() < deadline, "等待帧预算验收事务超时");
        fixture.service.Poll();
    }
    return operation.get();
}

void ManySmithTracesRespectAggregateTransportBudget()
{
    VnaServiceTest::Fixture fixture;
    auto &service = fixture.service;
    Require(service.TryAcquireControl(), "取得多迹线传输验收控制权");
    auto measurement = service.Settings().measurements.front();
    measurement.receiverPort = 1U;
    Require(Finish(fixture, service.UpdateMeasurement(measurement)).ok && Finish(fixture, service.SetPoints(10001U)).ok,
        "准备最大扫频点数的反射测量");
    std::vector<double> samples(16U, 1.11);
    samples[2U] = 0.2718281828459045;
    samples[3U] = 0.3141592653589793;
    fixture.device->SetSamples(samples);
    Require(Finish(fixture, service.RunSingle("SCPI")).ok, "发布最大点数完整帧");
    VnaWeb::HttpApi api(service);
    VnaWeb::HttpRequest request;
    request.method = "GET";
    request.path = "/api/v1/frame";
    for (std::size_t number = 1U; number <= VnaDisplay::MAX_DISPLAY_TRACES; ++number) {
        if (number > 1U) {
            Require(Finish(fixture, service.CloneMeasurement(
                                        "Trc1", service.Settings().revision, service.Display().revision, "SCPI"))
                        .ok,
                "添加传输预算内的反射副本");
        }
        VnaDisplay::DisplayChange format;
        format.kind = VnaDisplay::DisplayChangeKind::FORMAT;
        format.format = VnaDisplay::DisplayFormat::SMITH;
        Require(Finish(fixture, service.ApplyDisplay(format)).ok, "配置当前副本的 Smith 投影");
        if (number != 1U && number != 4U && number != 6U && number != 32U && number != VnaDisplay::MAX_DISPLAY_TRACES) {
            continue;
        }
        const auto response = api.Handle(request);
        Require(response.status == 200U && Field(response.json, "current").boolean, "预算内全部迹线使用当前完整帧");
        const auto count = static_cast<std::size_t>(Field(response.json, "displayPointCount").number);
        const auto expected = std::min<std::size_t>(10001U, 40004U / number);
        Require(count == expected && Field(response.json, "originalPointCount").number == 10001.0 &&
                    Field(response.json, "traces").array.size() == number,
            "扩大迹线容量不能扩大总采样传输预算或截断迹线");
        const auto &indexes = Field(response.json, "sampleIndexes").array;
        Require(indexes.size() == count && indexes.front().number == 0.0 && indexes.back().number == 10000.0,
            "所有迹线共用等长原始采样下标，并保留真实频轴首尾");
        for (const auto &trace : Field(response.json, "traces").array) {
            const auto &projection = Field(trace, "projection");
            Require(Field(trace, "real").array.size() == count && Field(trace, "imag").array.size() == count &&
                        Field(projection, "impedanceRealValues").array.size() == count &&
                        Field(projection, "impedanceImagValues").array.size() == count,
                "IQ 与 Smith 阻抗投影必须共同按总预算抽点");
        }
        Require(VnaWeb::StringifyJson(response.json, 16U * 1024U * 1024U).size() < 16U * 1024U * 1024U,
            "最大点数与全部 Smith 迹线的响应必须在现有 JSON 上限内可编码");
    }
}
}  // 匿名命名空间

int main()
{
    try {
        SelectionContract();
        SelectionIncludesAllBudgetedTraces();
        ValidateBeforeFirstFrame();
        ManySmithTracesRespectAggregateTransportBudget();
        std::cout << "PASS 显示集合查询与能力合同\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
