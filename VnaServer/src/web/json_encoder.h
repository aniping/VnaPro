/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#ifndef VNAPRO_WEB_JSON_ENCODER_H
#define VNAPRO_WEB_JSON_ENCODER_H

#include <cstddef>
#include <functional>
#include <map>
#include <string>
#include <vector>

#include "web/json.h"

namespace VnaWeb {
// 遍历记录借用编码器持有的节点；记录容器重新分配时，节点引用仍然有效。
struct JsonEncodingFrame final {
    explicit JsonEncodingFrame(const Json &item) noexcept : value(std::cref(item)), member(item.object.begin())
    {
    }

    JsonEncodingFrame(const JsonEncodingFrame &) = default;
    JsonEncodingFrame &operator=(const JsonEncodingFrame &) = default;
    JsonEncodingFrame(JsonEncodingFrame &&) noexcept = default;
    JsonEncodingFrame &operator=(JsonEncodingFrame &&) noexcept = default;

    std::reference_wrapper<const Json> value;
    std::size_t index = 0U;
    bool opened = false;
    std::map<std::string, Json>::const_iterator member;
};

// 有界遍历借用文档节点期间，保持文档及节点地址稳定。
class JsonEncoder final {
public:
    static constexpr unsigned DEFAULT_ENCODING_BUDGET = 256U;

    // 创建空闲编码器；编码前须通过 Reset 提供文档。
    JsonEncoder() noexcept = default;
    // 持有文档副本，用于增量编码。
    explicit JsonEncoder(Json value);
    ~JsonEncoder() = default;
    JsonEncoder(const JsonEncoder &) = delete;
    JsonEncoder &operator=(const JsonEncoder &) = delete;
    JsonEncoder(JsonEncoder &&) = delete;
    JsonEncoder &operator=(JsonEncoder &&) = delete;
    // 丢弃先前输出并持有稳定的文档；调用 Take 或发生错误后，必须先调用 Reset。
    void Reset(Json value);
    // 每次最多推进 budget 个节点步骤；每次追加前检查输出不超过 256 MiB。
    // 完成时返回 true；状态或 UTF-8 无效、分配失败时抛出异常，重试前必须重置。
    bool Advance(unsigned budget = DEFAULT_ENCODING_BUDGET);
    // 仅允许移出完整输出一次；未完成、空闲、失败或已取出时抛出异常。
    std::string Take();

private:
    enum class State {
        IDLE,
        ACTIVE,
        COMPLETE,
        FAILED,
        TAKEN
    };

    void ProcessTop();
    void ProcessContainer(JsonEncodingFrame &frame);
    void AppendChild(JsonEncodingFrame &frame);
    void AppendScalar(const Json &value);
    Json value_;
    std::vector<JsonEncodingFrame> stack_;
    std::string output_;
    State state_ = State::IDLE;
};

// 同步编码完整文档，用于生成有界的请求指纹。
std::string StringifyJson(const Json &value);
}  // 命名空间 VnaWeb
#endif
