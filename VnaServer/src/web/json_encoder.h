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

namespace vna_web {
// Traversal records borrow nodes owned by the encoder; reallocation keeps those references stable.
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

// Keeps the document stable while the bounded traversal borrows its nodes.
class JsonEncoder final {
public:
    static constexpr unsigned DEFAULT_ENCODING_BUDGET = 256U;

    // Create an idle encoder; Reset supplies a document before encoding.
    JsonEncoder() noexcept = default;
    // Take a document copy for incremental encoding.
    explicit JsonEncoder(Json value);
    ~JsonEncoder() = default;
    JsonEncoder(const JsonEncoder &) = delete;
    JsonEncoder &operator=(const JsonEncoder &) = delete;
    JsonEncoder(JsonEncoder &&) = delete;
    JsonEncoder &operator=(JsonEncoder &&) = delete;
    // Discard previous output and take a stable document; Reset is required after Take or an error.
    void Reset(Json value);
    // Encode at most budget node steps; output is capped at 4 MiB before each append.
    // Return true once complete. Invalid state/UTF-8 or allocation failures throw; reset before retrying.
    bool Advance(unsigned budget = DEFAULT_ENCODING_BUDGET);
    // Move complete output exactly once; throw if incomplete, idle, failed, or already taken.
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

// Encode a complete document synchronously for bounded request fingerprints.
std::string StringifyJson(const Json &value);
}  // namespace vna_web
#endif
