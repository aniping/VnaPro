/*
 * Copyright (c) 2026 Huawei Device Co., Ltd.
 */

#include "web/web_endpoint.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "network/network_runtime.h"
#include "network/socket_owner.h"
#include "network/socket_poll_set.h"
#include "vna_log/logger.h"
#include "web/calibration_json.h"
#include "web/http_api.h"
#include "web/json_encoder.h"
#include "web/static_file.h"

namespace VnaWeb {
using VnaServer::Network::INVALID_SOCKET_HANDLE;
using VnaServer::Network::SetNonBlocking;
using VnaServer::Network::Socket;
using VnaServer::Network::SocketError;
using VnaServer::Network::SocketOwner;
using VnaServer::Network::WouldBlock;

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t HEADERS_LIMIT = 8192U;
constexpr std::size_t BODY_LIMIT = 16384U;
// 完整扫频帧单独扩大输出预算，控制响应与校准导出沿用各自上限。
constexpr std::size_t FRAME_JSON_BYTE_LIMIT = 256U * 1024U * 1024U;
constexpr std::size_t CONNECTION_LIMIT = 8U;
constexpr std::size_t RECEIVE_BUDGET = 4096U;
constexpr std::size_t SEND_BUDGET = 32768U;
constexpr unsigned ACCEPT_BUDGET = 4U;
constexpr unsigned JSON_ENCODING_BUDGET = 256U;
constexpr std::size_t CRLF_LENGTH = 2U;
constexpr std::size_t HEADER_BOUNDARY_LENGTH = 4U;
constexpr unsigned ASCII_CONTROL_LIMIT = 32U;
constexpr unsigned ASCII_DELETE = 127U;
constexpr unsigned HEX_RADIX = 16U;
constexpr unsigned HEX_ALPHA_OFFSET = 10U;
constexpr unsigned DECIMAL_RADIX = 10U;
const std::chrono::seconds g_requestTimeout{10};
const std::chrono::seconds g_writeProgressTimeout{10};
const std::string HEADER_PUNCTUATION = "!#$%&'*+-.^_`|~";

std::string Lower(std::string value)
{
    for (char &character : value) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return value;
}

std::string Trim(const std::string &value)
{
    const auto begin = value.find_first_not_of(" \t");
    if (begin == std::string::npos) {
        return std::string();
    }
    return value.substr(begin, value.find_last_not_of(" \t") - begin + 1U);
}

bool HeaderCharacter(unsigned char value) noexcept
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
           HEADER_PUNCTUATION.find(static_cast<char>(value)) != std::string::npos;
}

unsigned HexDigit(char value)
{
    if (value >= '0' && value <= '9') {
        return static_cast<unsigned>(value - '0');
    }
    if (value >= 'a' && value <= 'f') {
        return static_cast<unsigned>(value - 'a' + HEX_ALPHA_OFFSET);
    }
    if (value >= 'A' && value <= 'F') {
        return static_cast<unsigned>(value - 'A' + HEX_ALPHA_OFFSET);
    }
    throw std::runtime_error("Invalid URL escape");
}

std::string DecodePath(const std::string &target)
{
    if (target.empty() || target.front() != '/' || target.find('#') != std::string::npos) {
        throw std::runtime_error("Invalid request target");
    }
    const std::string raw = target.substr(0, target.find('?'));
    std::string result;
    for (std::size_t index = 0; index < raw.size(); ++index) {
        unsigned char character = static_cast<unsigned char>(raw[index]);
        if (character == '%') {
            if (index + 2U >= raw.size()) {
                throw std::runtime_error("Incomplete URL escape");
            }
            character = static_cast<unsigned char>((HexDigit(raw[index + 1U]) * HEX_RADIX) + HexDigit(raw[index + 2U]));
            index += 2U;
        }
        if (character < ASCII_CONTROL_LIMIT || character == ASCII_DELETE || character == '\\') {
            throw std::runtime_error("Unsafe request path");
        }
        result.push_back(static_cast<char>(character));
    }
    const std::string measurementPrefix = "/api/v1/measurements/";
    const bool measurementName = result.find(measurementPrefix) == 0U && result.size() > measurementPrefix.size() &&
                                 result.find('/', measurementPrefix.size()) == std::string::npos;
    // 冒号是合法测量名称字符；仅单个测量 API 路径允许它，静态文件仍禁止 Windows 盘符路径。
    if (result.find(':') != std::string::npos && !measurementName) {
        throw std::runtime_error("Unsafe request path");
    }
    std::istringstream segments(result);
    std::string segment;
    while (std::getline(segments, segment, '/')) {
        if (segment == "." || segment == "..") {
            throw std::runtime_error("Path traversal is forbidden");
        }
    }
    return result;
}

std::string Reason(unsigned status)
{
    switch (status) {
        case 200:
            return "OK";
        case 202:
            return "Accepted";
        case 400:
            return "Bad Request";
        case 403:
            return "Forbidden";
        case 404:
            return "Not Found";
        case 405:
            return "Method Not Allowed";
        case 408:
            return "Request Timeout";
        case 409:
            return "Conflict";
        case 410:
            return "Gone";
        case 413:
            return "Payload Too Large";
        case 415:
            return "Unsupported Media Type";
        case 417:
            return "Expectation Failed";
        case 429:
            return "Too Many Requests";
        case 431:
            return "Request Header Fields Too Large";
        case 503:
            return "Service Unavailable";
        default:
            return "Internal Server Error";
    }
}

std::string ResponseHeader(unsigned status, const std::string &contentType, std::size_t length)
{
    return "HTTP/1.1 " + std::to_string(status) + " " + Reason(status) + "\r\nContent-Type: " + contentType +
           "\r\nContent-Length: " + std::to_string(length) +
           "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n";
}

std::string MimeType(const std::string &path)
{
    const auto dot = path.rfind('.');
    const std::string extension = dot == std::string::npos ? std::string() : Lower(path.substr(dot));
    if (extension == ".html") {
        return "text/html; charset=utf-8";
    }
    if (extension == ".js" || extension == ".mjs") {
        return "text/javascript; charset=utf-8";
    }
    if (extension == ".css") {
        return "text/css; charset=utf-8";
    }
    if (extension == ".svg") {
        return "image/svg+xml";
    }
    if (extension == ".png") {
        return "image/png";
    }
    if (extension == ".ico") {
        return "image/x-icon";
    }
    return "application/octet-stream";
}

std::size_t ParseRequestLine(const std::string &input, HttpRequest &request)
{
    const std::size_t firstEnd = input.find("\r\n");
    std::istringstream first(input.substr(0, firstEnd));
    std::string target;
    std::string version;
    std::string extra;
    if (!(first >> request.method >> target >> version) || (first >> extra) || version != "HTTP/1.1") {
        throw std::runtime_error("Expected HTTP/1.1 request line");
    }
    for (unsigned char character : request.method) {
        if (!HeaderCharacter(character)) {
            throw std::runtime_error("Invalid HTTP method");
        }
    }
    request.path = DecodePath(target);
    const std::size_t queryStart = target.find('?');
    if (queryStart != std::string::npos) {
        request.query = target.substr(queryStart + 1U);
    }
    return firstEnd + CRLF_LENGTH;
}

void ParseHeaderLine(const std::string &line, HttpRequest &request)
{
    const std::size_t colon = line.find(':');
    if (colon == std::string::npos || colon == 0U) {
        throw std::runtime_error("Invalid header field");
    }
    const std::string name = Lower(line.substr(0, colon));
    for (unsigned char character : name) {
        if (!HeaderCharacter(character)) {
            throw std::runtime_error("Invalid header name");
        }
    }
    const std::string value = Trim(line.substr(colon + 1U));
    for (unsigned char character : value) {
        if ((character < ASCII_CONTROL_LIMIT && character != '\t') || character == ASCII_DELETE) {
            throw std::runtime_error("Invalid header value");
        }
    }
    if (!request.headers.emplace(name, value).second) {
        throw std::runtime_error("Duplicate header field");
    }
}

void ParseHeaderFields(const std::string &input, std::size_t end, std::size_t cursor, HttpRequest &request)
{
    while (cursor < end) {
        const std::size_t lineEnd = input.find("\r\n", cursor);
        if (lineEnd == std::string::npos || lineEnd > end) {
            throw std::runtime_error("Invalid header line");
        }
        ParseHeaderLine(input.substr(cursor, lineEnd - cursor), request);
        cursor = lineEnd + CRLF_LENGTH;
    }
}

void ValidateHeaderFraming(const HttpRequest &request)
{
    if (request.headers.count("host") == 0U || request.headers.at("host").empty()) {
        throw std::runtime_error("HTTP/1.1 Host is required");
    }
    if (request.headers.count("transfer-encoding") != 0U) {
        throw std::runtime_error("Transfer-Encoding is unsupported");
    }
}

}  // 匿名命名空间

WebEndpoint::WebEndpoint(VnaService::InstrumentService &service, const WebOptions &options)
    : runtime_("Web WSAStartup failed"), api_(service), options_(options)
{
    if (!options_.enabled) {
        return;
    }
    listener_.Reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
    if (listener_.Get() == INVALID_SOCKET_HANDLE) {
        throw std::runtime_error("Cannot create Web listener");
    }
    actualPort_ = VnaServer::Network::BindListener(listener_.Get(), options_.bindAddress, options_.port);
    VnaLog::Info(VnaLog::Module::WEB, "Web 已监听，address=http://{}:{}", options_.bindAddress, actualPort_);
}

void WebEndpoint::Respond(HttpConnection &client, HttpResponse response)
{
    // 只记录已解析路径和结果，不输出头、控制令牌或请求正文。
    VnaLog::Debug(VnaLog::Module::WEB, "HTTP method={} path={} status={}", client.request.method, client.request.path,
        response.status);
    client.dispatched = true;
    client.input.clear();
    client.status = response.status;
    client.contentType = "application/json; charset=utf-8";
    std::size_t outputLimit = JsonEncoder::DEFAULT_OUTPUT_LIMIT;
    if (client.request.method == "GET" && client.request.path == "/api/v1/frame") {
        outputLimit = FRAME_JSON_BYTE_LIMIT;
    } else if (client.request.method == "GET" &&
               client.request.path.compare(0U, 25U, "/api/v1/calibration/sets/") == 0) {
        outputLimit = CALIBRATION_JSON_BYTE_LIMIT;
    }
    client.encoder.Reset(std::move(response.json), outputLimit);
    client.encoderActive = true;
    client.file.Close();
    client.fileActive = false;
    client.fileOutput.clear();
    client.progress = Clock::now();
}

void WebEndpoint::Error(HttpConnection &client, unsigned status, const std::string &message)
{
    Respond(client, HttpError(status, "http_error", message));
}

bool WebEndpoint::ParseHeaders(HttpConnection &client)
{
    const std::size_t end = client.input.find("\r\n\r\n");
    if (end == std::string::npos) {
        if (client.input.size() > HEADERS_LIMIT) {
            Error(client, 431, "Request headers exceed 8 KiB");
        }
        return false;
    }
    client.headerLength = end + HEADER_BOUNDARY_LENGTH;
    if (client.headerLength > HEADERS_LIMIT) {
        Error(client, 431, "Request headers exceed 8 KiB");
        return false;
    }
    const std::size_t cursor = ParseRequestLine(client.input, client.request);
    ParseHeaderFields(client.input, end, cursor, client.request);
    ValidateHeaderFraming(client.request);
    if (client.request.headers.count("expect") != 0U) {
        Error(client, 417, "Expect is unsupported");
        return false;
    }
    if (!ParseContentLength(client) || !ValidateContentType(client)) {
        return false;
    }
    client.parsed = true;
    return true;
}

bool WebEndpoint::ParseContentLength(HttpConnection &client)
{
    const std::size_t limit = client.request.method == "POST" && client.request.path == "/api/v1/calibration"
                                  ? CALIBRATION_JSON_BYTE_LIMIT
                                  : BODY_LIMIT;
    const auto length = client.request.headers.find("content-length");
    if (length != client.request.headers.end()) {
        if (length->second.empty()) {
            throw std::runtime_error("Empty Content-Length");
        }
        for (char digit : length->second) {
            if (digit < '0' || digit > '9') {
                throw std::runtime_error("Invalid Content-Length");
            }
            if (client.bodyLength > (limit / DECIMAL_RADIX)) {
                Error(client, 413, "Request body exceeds this route's size limit");
                return false;
            }
            client.bodyLength = (client.bodyLength * DECIMAL_RADIX) + static_cast<unsigned>(digit - '0');
            if (client.bodyLength > limit) {
                Error(client, 413, "Request body exceeds this route's size limit");
                return false;
            }
        }
    }
    if ((client.request.method == "POST" || client.request.method == "DELETE" || client.request.method == "PUT") &&
        length == client.request.headers.end()) {
        throw std::runtime_error("Content-Length is required for writes");
    }
    return true;
}

bool WebEndpoint::ValidateContentType(HttpConnection &client)
{
    if (client.bodyLength != 0U && client.request.path.find("/api/") == 0U) {
        const auto type = client.request.headers.find("content-type");
        if (type == client.request.headers.end() ||
            Lower(Trim(type->second.substr(0, type->second.find(';')))) != "application/json") {
            Error(client, 415, "API body requires application/json");
            return false;
        }
    }
    return true;
}

void WebEndpoint::Dispatch(HttpConnection &client)
{
    client.request.body = client.input.substr(client.headerLength, client.bodyLength);
    if (client.request.path.find("/api/") == 0U) {
        Respond(client, api_.Handle(client.request));
        return;
    }
    if (client.request.method != "GET") {
        Error(client, 405, "Static files require GET");
        return;
    }
    if (client.bodyLength != 0U) {
        Error(client, 400, "GET body is unsupported");
        return;
    }
    try {
        client.file.Open(options_.staticRoot, client.request.path);
        client.fileActive = true;
        client.contentType = MimeType(client.request.path == "/" ? "/index.html" : client.request.path);
        client.dispatched = true;
        client.input.clear();
        client.progress = Clock::now();
    } catch (const std::exception &) {
        Error(client, 404, "Static file not found");
    }
}

bool WebEndpoint::Receive(HttpConnection &client)
{
    char buffer[RECEIVE_BUDGET]{};
    const int count = VnaServer::Network::ReceiveSocket(client.socket.Get(), buffer, sizeof(buffer));
    if (count == 0) {
        return false;
    }
    if (count < 0) {
        return WouldBlock(SocketError());
    }
    client.input.append(buffer, static_cast<std::size_t>(count));
    try {
        if (!client.parsed && !ParseHeaders(client)) {
            return true;
        }
        if (client.dispatched) {
            return true;
        }
        const std::size_t complete = client.headerLength + client.bodyLength;
        if (client.input.size() > complete) {
            Error(client, 400, "One HTTP message per connection is supported");
            return true;
        }
        if (client.input.size() == complete) {
            Dispatch(client);
        }
    } catch (const std::exception &failure) {
        Error(client, 400, failure.what());
    }
    return true;
}

void WebEndpoint::EncodeOutput(HttpConnection &client)
{
    try {
        if (client.encoderActive && client.encoder.Advance(JSON_ENCODING_BUDGET)) {
            std::string body = client.encoder.Take();
            client.encoderActive = false;
            client.output = ResponseHeader(client.status, client.contentType, body.size()) + body;
        }
        if (client.fileActive && client.file.Advance(client.fileOutput)) {
            client.output =
                ResponseHeader(client.status, client.contentType, client.fileOutput.size()) + client.fileOutput;
            client.file.Close();
            client.fileActive = false;
            client.fileOutput.clear();
        }
    } catch (const std::exception &) {
        client.encoderActive = false;
        client.file.Close();
        client.fileActive = false;
        client.fileOutput.clear();
        const std::string body = "{\"error\":{\"code\":\"output_failed\",\"message\":\"Response encoding failed\"}}";
        client.output = ResponseHeader(500, "application/json", body.size()) + body;
    }
}

bool WebEndpoint::SendOutput(HttpConnection &client)
{
    EncodeOutput(client);
    if (client.output.empty()) {
        return true;
    }
    const std::size_t count = (std::min)(client.output.size() - client.sent, SEND_BUDGET);
    const int sent = VnaServer::Network::SendSocket(client.socket.Get(), client.output.data() + client.sent, count);
    if (sent <= 0) {
        return sent < 0 && WouldBlock(SocketError());
    }
    client.sent += static_cast<std::size_t>(sent);
    client.progress = Clock::now();
    return client.sent != client.output.size();
}

void WebEndpoint::AcceptConnections()
{
    if (listener_.Get() == INVALID_SOCKET_HANDLE) {
        return;
    }
    for (unsigned index = 0; index < ACCEPT_BUDGET; ++index) {
        SocketOwner acceptedSocket(accept(listener_.Get(), nullptr, nullptr));
        const Socket socket = acceptedSocket.Get();
        if (socket == INVALID_SOCKET_HANDLE) {
            break;
        }
        if (clients_.size() >= CONNECTION_LIMIT || !SetNonBlocking(socket)) {
            continue;
        }
        VnaServer::Network::ConfigureStreamSocket(socket);
        clients_.emplace_back(std::move(acceptedSocket));
    }
}

void WebEndpoint::PollClients()
{
    const auto time = Clock::now();
    for (auto client = clients_.begin(); client != clients_.end();) {
        if (!client->dispatched && (time - client->started) >= g_requestTimeout) {
            Error(*client, 408, "Request receive timeout");
        }
        const bool alive = client->dispatched
                               ? ((time - client->progress) < g_writeProgressTimeout && SendOutput(*client))
                               : Receive(*client);
        if (!alive) {
            client = clients_.erase(client);
        } else {
            ++client;
        }
    }
}

void WebEndpoint::Poll()
{
    api_.Poll();
    AcceptConnections();
    PollClients();
}

void WebEndpoint::AddPollEvents(VnaServer::Network::SocketPollSet &events) const noexcept
{
    events.AddReadable(listener_.Get());
    for (const auto &client : clients_) {
        if (!client.dispatched) {
            events.AddReadable(client.socket.Get());
        } else if (client.encoderActive || client.fileActive) {
            // 每轮仍只处理原有编码或文件分片，随后让其他连接和服务任务获得执行机会。
            events.RequestImmediate();
        } else if (client.sent < client.output.size()) {
            // 等待真实可写事件；背压中的连接不会因存在未发送字节而要求立即重试。
            events.AddWritable(client.socket.Get());
        }
    }
}

void WebEndpoint::BeginShutdown()
{
    if (closing_) {
        return;
    }
    closing_ = true;
    listener_.Reset();
    clients_.clear();
    api_.BeginShutdown();
}

bool WebEndpoint::Drained() const
{
    return clients_.empty() && api_.Drained();
}

std::uint16_t WebEndpoint::Port() const noexcept
{
    return actualPort_;
}

}  // 命名空间 VnaWeb
