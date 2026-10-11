/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/frame_selection.h"

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#include "fixture.h"
#include "web/http_api.h"

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
    Require(VnaWeb::SelectFrameMeasurements("", catalog).size() == 4U, "旧请求保留前四条兼容性");
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
}  // 匿名命名空间

int main()
{
    try {
        SelectionContract();
        ValidateBeforeFirstFrame();
        std::cout << "PASS 显示集合查询与能力合同\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
