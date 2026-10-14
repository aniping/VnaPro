/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/frame_array_json.h"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "web/display_json.h"
#include "web/json_encoder.h"

namespace {
using VnaWeb::Json;

void Require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

const Json &Member(const Json &value, const char *name)
{
    const Json *member = value.Find(name);
    Require(member != nullptr, std::string("缺少紧凑字段：") + name);
    return *member;
}

void CheckEnvelope(const Json &value, const char *codec, std::size_t count)
{
    Require(value.type == Json::Type::OBJECT, "紧凑数组必须使用对象信封");
    Require(Member(value, "codec").string == codec, "数组编码标识错误");
    Require(Member(value, "count").number == static_cast<double>(count), "紧凑数组不能丢失任何元素");
    Require(Member(value, "data").type == Json::Type::STRING, "字节内容必须使用 Base64 字符串");
}

std::vector<unsigned char> DecodeBytes(const Json &value)
{
    const std::string alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::vector<unsigned char> bytes;
    unsigned bits = 0U;
    unsigned available = 0U;
    for (const char character : Member(value, "data").string) {
        if (character == '=') {
            break;
        }
        const auto digit = alphabet.find(character);
        Require(digit != std::string::npos, "Base64 不允许无效字符");
        bits = (bits << 6U) | static_cast<unsigned>(digit);
        available += 6U;
        if (available >= 8U) {
            available -= 8U;
            bytes.push_back(static_cast<unsigned char>((bits >> available) & 0xffU));
        }
    }
    return bytes;
}

std::uint64_t ReadLittleEndian(const std::vector<unsigned char> &bytes, std::size_t index)
{
    Require(index * 8U + 8U <= bytes.size(), "双精度字节长度不足");
    std::uint64_t result = 0U;
    for (unsigned byte = 0U; byte < 8U; ++byte) {
        result |= static_cast<std::uint64_t>(bytes[index * 8U + byte]) << (byte * 8U);
    }
    return result;
}

void FloatingPointBits()
{
    Json values = Json::MakeArray();
    const std::vector<double> input = {0.0, -0.0, 1.0, -1.0, 0.5, std::numeric_limits<double>::max(),
        std::numeric_limits<double>::lowest(), std::numeric_limits<double>::min(),
        std::numeric_limits<double>::denorm_min(), std::nextafter(1.0, 2.0)};
    for (const double value : input) {
        values.array.push_back(Json(value));
    }
    values.array.emplace_back();
    const Json packed = VnaWeb::FrameArrayJson::Pack(std::move(values));
    CheckEnvelope(packed, "f64le", input.size() + 1U);
    const auto bytes = DecodeBytes(packed);
    Require(bytes.size() == (input.size() + 1U) * 8U, "每个数值都必须保留八字节");
    for (std::size_t index = 0U; index < input.size(); ++index) {
        std::uint64_t expected = 0U;
        std::memcpy(&expected, &input[index], sizeof(expected));
        Require(ReadLittleEndian(bytes, index) == expected, "数值传输必须逐位无损，不能量化、舍入或抽点");
    }
    Require(ReadLittleEndian(bytes, 1U) == UINT64_C(0x8000000000000000), "负零必须保留符号位");
    Require(ReadLittleEndian(bytes, input.size()) == UINT64_C(0x7ff8000000000000), "null 使用固定 quiet NaN");

    Json fixture = Json::MakeArray();
    for (std::size_t index = 0U; index < 5U; ++index) {
        fixture.array.push_back(Json(input[index]));
    }
    fixture.array.emplace_back();
    const Json fixturePacked = VnaWeb::FrameArrayJson::Pack(std::move(fixture));
    Require(Member(fixturePacked, "data").string == "AAAAAAAAAAAAAAAAAAAAgAAAAAAAAPA/AAAAAAAA8L8AAAAAAADgPwAAAAAAAPh/",
        "双精度 Base64 必须与独立小端跨语言样本一致");

    Json nulls = Json::MakeArray();
    nulls.array.resize(2U);
    CheckEnvelope(VnaWeb::FrameArrayJson::Pack(std::move(nulls)), "f64le", 2U);
    for (const double invalid : {std::numeric_limits<double>::infinity(), -std::numeric_limits<double>::infinity(),
             std::numeric_limits<double>::quiet_NaN()}) {
        Json invalidArray = Json::MakeArray();
        invalidArray.array.push_back(Json(invalid));
        bool rejected = false;
        try {
            VnaWeb::FrameArrayJson::Pack(std::move(invalidArray));
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        Require(rejected, "非有限数值必须在进入传输协议前被显式转为 null");
    }
}

void BooleanAndDictionary()
{
    const std::vector<std::string> expectedBase64 = {"AQ==", "AQA=", "AQAB", "AQABAQ=="};
    for (std::size_t count = 1U; count <= expectedBase64.size(); ++count) {
        Json flags = Json::MakeArray();
        for (std::size_t index = 0U; index < count; ++index) {
            flags.array.push_back(Json(index != 1U));
        }
        const Json packed = VnaWeb::FrameArrayJson::Pack(std::move(flags));
        CheckEnvelope(packed, "u8", count);
        Require(Member(packed, "data").string == expectedBase64[count - 1U], "Base64 长度与补齐必须符合标准");
    }
    Json states = Json::MakeArray();
    for (const char *state : {"VALID", "无效", "VALID", "ZERO"}) {
        states.array.push_back(Json(state));
    }
    const Json packed = VnaWeb::FrameArrayJson::Pack(std::move(states));
    CheckEnvelope(packed, "enum8", 4U);
    Require(Member(packed, "data").string == "AAEAAg==", "状态编码按首次出现顺序分配");
    const Json &dictionary = Member(packed, "dictionary");
    Require(
        dictionary.type == Json::Type::ARRAY && dictionary.array.size() == 3U, "新建字典保持普通数组，不能递归再打包");
    Require(dictionary.array[0U].string == "VALID" && dictionary.array[1U].string == "无效" &&
                dictionary.array[2U].string == "ZERO",
        "字典必须保留原始 UTF-8 状态值和顺序");

    Json many = Json::MakeArray();
    for (unsigned index = 0U; index < 256U; ++index) {
        many.array.push_back(Json(std::to_string(index)));
    }
    const Json boundary = VnaWeb::FrameArrayJson::Pack(many);
    CheckEnvelope(boundary, "enum8", 256U);
    Require(DecodeBytes(boundary).back() == 255U, "单字节字典支持完整的 256 个条目");
    many.array.push_back(Json("256"));
    const Json unchanged = VnaWeb::FrameArrayJson::Pack(many);
    Require(VnaWeb::StringifyJson(unchanged) == VnaWeb::StringifyJson(many), "字典超过容量时保留原数组");
}

void TreeAndMarkerCollections()
{
    Json markers = Json::MakeArray();
    for (unsigned marker = 1U; marker <= 2U; ++marker) {
        Json item = Json::MakeObject();
        item["number"] = Json(marker);
        item["frequencyHz"] = Json(1e9 + marker);
        item["value"] = Json(-20.0 + marker);
        item["valueState"] = Json("VALID");
        markers.array.push_back(std::move(item));
    }
    Require(VnaWeb::StringifyJson(VnaWeb::FrameArrayJson::Pack(markers)) == VnaWeb::StringifyJson(markers),
        "Marker 对象集合和标量读数必须保持不变");
    Json mixed = Json::MakeArray();
    mixed.array.push_back(Json(1.0));
    mixed.array.push_back(Json("VALID"));
    mixed.array.push_back(Json(true));
    mixed.array.emplace_back();
    Json child = Json::MakeObject();
    child["values"] = Json::MakeArray();
    child["values"].array.push_back(Json(0.5));
    mixed.array.push_back(std::move(child));
    Json root = Json::MakeObject();
    root["name"] = Json("Trc17");
    root["markers"] = markers;
    root["empty"] = Json::MakeArray();
    root["mixed"] = std::move(mixed);
    const Json packed = VnaWeb::FrameArrayJson::Pack(std::move(root));
    Require(Member(packed, "name").string == "Trc17", "对象名称不应改变");
    Require(Member(packed, "empty").type == Json::Type::ARRAY && Member(packed, "empty").array.empty(),
        "空数组不能转换成无类型信封");
    const Json &collection = Member(packed, "mixed");
    Require(collection.type == Json::Type::ARRAY && collection.array.size() == 5U, "混合数组容器不变");
    Require(collection.array[0U].number == 1.0 && collection.array[1U].string == "VALID" &&
                collection.array[2U].boolean && collection.array[3U].type == Json::Type::NULL_VALUE,
        "混合数组内的标量不应被转换");
    CheckEnvelope(Member(collection.array[4U], "values"), "f64le", 1U);
    Require(VnaWeb::StringifyJson(Member(packed, "markers")) == VnaWeb::StringifyJson(markers),
        "嵌套 Marker 集合保持 JSON 兼容");
}

void DirectReaders()
{
    using VnaWeb::FrameArrayJson;
    const std::vector<VnaWeb::FrameArrayNumber> numbers = {{0.0, true}, {-0.0, true}, {1.0, true}, {-1.0, true},
        {0.5, true}, {std::numeric_limits<double>::quiet_NaN(), false}};
    const auto readNumber = [&numbers](std::size_t index) {
        return numbers[index];
    };
    const auto direct = FrameArrayJson::Numbers(numbers.size(), readNumber, true);
    Require(Member(direct, "data").string == "AAAAAAAAAAAAAAAAAAAAgAAAAAAAAPA/AAAAAAAA8L8AAAAAAADgPwAAAAAAAPh/",
        "直接读取必须保留独立跨语言样本的完整位模式");
    const auto plain = FrameArrayJson::Numbers(numbers.size(), readNumber);
    Require(plain.type == Json::Type::ARRAY && std::signbit(plain.array[1U].number) &&
                plain.array.back().type == Json::Type::NULL_VALUE,
        "默认普通数值数组保持负零/null");
    Require(
        VnaWeb::StringifyJson(FrameArrayJson::Pack(plain)) == VnaWeb::StringifyJson(direct), "直接编码与原压缩等价");
    const auto flags = FrameArrayJson::Booleans(
        4U,
        [](std::size_t index) {
            return index != 1U;
        },
        true);
    Require(Member(flags, "data").string == "AQABAQ==", "直接布尔读取保留 u8 与 Base64 补齐");
    const std::vector<std::string> states = {"finite", std::string("a\0b", 3U), "finite", "无效"};
    const auto readState = [&states](std::size_t index) {
        return states[index];
    };
    const auto strings = FrameArrayJson::Strings(states.size(), readState, true);
    Require(
        Member(strings, "data").string == "AAEAAg==" && Member(strings, "dictionary").array[1U].string == states[1U],
        "状态字典不能截断内嵌 NUL，UTF-8 与首次出现顺序不变");
    Require(VnaWeb::StringifyJson(strings) ==
                VnaWeb::StringifyJson(FrameArrayJson::Pack(FrameArrayJson::Strings(states.size(), readState))),
        "直接状态与普通状态的压缩等价");
    const auto many = [](std::size_t index) {
        return std::to_string(index);
    };
    CheckEnvelope(FrameArrayJson::Strings(256U, many, true), "enum8", 256U);
    Require(VnaWeb::StringifyJson(FrameArrayJson::Strings(257U, many, true)) ==
                VnaWeb::StringifyJson(FrameArrayJson::Strings(257U, many)),
        "超过字典容量不能截断或溢出");
    unsigned calls = 0U;
    Require(FrameArrayJson::Numbers(
                0U,
                [&calls](std::size_t) {
                    ++calls;
                    return VnaWeb::FrameArrayNumber{};
                },
                true)
                .array.empty(),
        "空直接序列保持空数组");
    Require(calls == 0U, "空数组不能调用 reader");
    for (const double invalid : {std::numeric_limits<double>::infinity(), std::numeric_limits<double>::quiet_NaN()}) {
        bool rejected = false;
        try {
            FrameArrayJson::Numbers(
                1U,
                [invalid](std::size_t) {
                    return VnaWeb::FrameArrayNumber{invalid, true};
                },
                true);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        Require(rejected, "直接编码不能将非有限有效数伪造为合法数值");
    }
    bool rejected = false;
    try {
        FrameArrayJson::Numbers(std::numeric_limits<std::size_t>::max(), readNumber, true);
    } catch (const std::length_error &) {
        rejected = true;
    }
    Require(rejected, "字节长度乘法必须在分配或 reader 调用前校验溢出");
}

void DirectProjection()
{
    VnaDisplay::ProjectedTrace trace;
    trace.available = true;
    trace.points = {{-0.0, 0.5, true, VnaDisplay::DisplayValueState::FINITE},
        {-std::numeric_limits<double>::infinity(), 0.0, false, VnaDisplay::DisplayValueState::NEGATIVE_INFINITY},
        {std::numeric_limits<double>::quiet_NaN(), 1.0, true, VnaDisplay::DisplayValueState::FINITE},
        {1.0, 0.0, false, static_cast<VnaDisplay::DisplayValueState>(999)}};
    trace.markerPoints = trace.points;
    trace.impedancePoints = {{-0.0, 0.5, VnaDisplay::ImpedanceState::FINITE},
        {0.0, 0.0, VnaDisplay::ImpedanceState::OPEN}, {0.0, 0.0, static_cast<VnaDisplay::ImpedanceState>(999)}};
    trace.admittancePoints = {{-0.0, 0.5, VnaDisplay::AdmittanceState::FINITE},
        {0.0, 0.0, VnaDisplay::AdmittanceState::SHORT}, {0.0, 0.0, static_cast<VnaDisplay::AdmittanceState>(999)}};
    trace.marker.available = true;
    trace.marker.valid = true;
    trace.marker.real = -0.0;
    trace.markers.push_back(trace.marker);
    const std::vector<std::size_t> indexes = {0U, 1U, 2U, 3U, 4U};
    for (const auto format : {VnaDisplay::DisplayFormat::DB, VnaDisplay::DisplayFormat::POLAR,
             VnaDisplay::DisplayFormat::SMITH, VnaDisplay::DisplayFormat::INV_SMITH}) {
        for (const bool available : {true, false}) {
            trace.available = available;
            const auto ordinary = VnaWeb::DisplayJson::Projection(trace, format, indexes);
            const auto packed = VnaWeb::DisplayJson::Projection(trace, format, indexes, true);
            Require(VnaWeb::StringifyJson(packed) == VnaWeb::StringifyJson(VnaWeb::FrameArrayJson::Pack(ordinary)),
                "直接投影编码需保持特殊数值、未知状态、缺失索引及 Marker 的完整传输等价");
        }
    }
}
}  // 匿名命名空间

int main()
{
    try {
        FloatingPointBits();
        BooleanAndDictionary();
        TreeAndMarkerCollections();
        DirectReaders();
        DirectProjection();
        std::cout << "无损帧数组编码测试通过\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
