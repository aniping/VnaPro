/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#pragma once

#include <string>

#include "web/http_api.h"

namespace VnaCalibrationProtocolTest {
using VnaWeb::HttpApi;
using VnaWeb::HttpResponse;
using VnaWeb::Json;

// 协议测试共用实际 HTTP 请求与完成屏障，断言失败时抛出异常。
void Require(bool value, const std::string &message);
const Json &Field(const Json &value, const std::string &key);
HttpResponse Request(HttpApi &api, const std::string &path, const Json &body = Json(), const std::string &token = "");
void Complete(HttpApi &api, const HttpResponse &accepted, const std::string &token);
Json Action(HttpApi &api, const std::string &action, const std::string &id);

// 对已有校准执行二进制/CSV 往返、权限、幂等及失败保原状态验证。
void FileTransportsPreserveWriteContract(HttpApi &api, const std::string &token, const std::string &setId);

}  // 命名空间 VnaCalibrationProtocolTest
