#include "web/WebEndpoint.h"

#include <algorithm>
#include <cctype>
#include <chrono>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "network/NetworkRuntime.h"
#include "network/SocketOwner.h"
#include "web/HttpApi.h"
#include "web/JsonEncoder.h"
#include "web/StaticFile.h"

namespace vna_web {
using vna_server::network::CloseSocket;
using vna_server::network::kInvalidSocket;
using vna_server::network::NetworkRuntime;
using vna_server::network::SetNonBlocking;
using vna_server::network::Socket;
using vna_server::network::SocketError;
using vna_server::network::SocketLength;
using vna_server::network::SocketOwner;
using vna_server::network::WouldBlock;

namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kHeadersLimit = 8192U;
constexpr std::size_t kBodyLimit = 16384U;
constexpr std::size_t kConnectionLimit = 8U;
constexpr unsigned kReceiveBudget = 4096U;
constexpr unsigned kSendBudget = 32768U;

std::string lower(std::string value)
{
    for (char& character : value)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return value;
}

std::string trim(const std::string& value)
{
    const auto begin = value.find_first_not_of(" \t");
    if (begin == std::string::npos)
        return std::string();
    return value.substr(begin, value.find_last_not_of(" \t") - begin + 1U);
}

bool headerCharacter(unsigned char value)
{
    return (value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') || (value >= '0' && value <= '9') ||
           std::string("!#$%&'*+-.^_`|~").find(static_cast<char>(value)) != std::string::npos;
}

unsigned hexDigit(char value)
{
    if (value >= '0' && value <= '9')
        return static_cast<unsigned>(value - '0');
    if (value >= 'a' && value <= 'f')
        return static_cast<unsigned>(value - 'a' + 10);
    if (value >= 'A' && value <= 'F')
        return static_cast<unsigned>(value - 'A' + 10);
    throw std::runtime_error("Invalid URL escape");
}

std::string decodePath(const std::string& target)
{
    if (target.empty() || target.front() != '/' || target.find('#') != std::string::npos)
        throw std::runtime_error("Invalid request target");
    const std::string raw = target.substr(0, target.find('?'));
    std::string result;
    for (std::size_t index = 0; index < raw.size(); ++index) {
        unsigned char character = static_cast<unsigned char>(raw[index]);
        if (character == '%') {
            if (index + 2U >= raw.size())
                throw std::runtime_error("Incomplete URL escape");
            character = static_cast<unsigned char>(hexDigit(raw[index + 1U]) * 16U + hexDigit(raw[index + 2U]));
            index += 2U;
        }
        if (character < 32U || character == 127U || character == '\\' || character == ':')
            throw std::runtime_error("Unsafe request path");
        result.push_back(static_cast<char>(character));
    }
    std::istringstream segments(result);
    std::string segment;
    while (std::getline(segments, segment, '/'))
        if (segment == "." || segment == "..")
            throw std::runtime_error("Path traversal is forbidden");
    return result;
}

std::string reason(unsigned status)
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

std::string responseHeader(unsigned status, const std::string& contentType, std::size_t length)
{
    return "HTTP/1.1 " + std::to_string(status) + " " + reason(status) + "\r\nContent-Type: " + contentType +
           "\r\nContent-Length: " + std::to_string(length) +
           "\r\nConnection: close\r\nCache-Control: no-store\r\nX-Content-Type-Options: nosniff\r\n\r\n";
}

std::string mimeType(const std::string& path)
{
    const auto dot = path.rfind('.');
    const std::string extension = dot == std::string::npos ? std::string() : lower(path.substr(dot));
    if (extension == ".html")
        return "text/html; charset=utf-8";
    if (extension == ".js" || extension == ".mjs")
        return "text/javascript; charset=utf-8";
    if (extension == ".css")
        return "text/css; charset=utf-8";
    if (extension == ".svg")
        return "image/svg+xml";
    if (extension == ".png")
        return "image/png";
    if (extension == ".ico")
        return "image/x-icon";
    return "application/octet-stream";
}

}  // namespace

class WebEndpoint::Impl {
public:
    struct Client {
        explicit Client(Socket value) : socket(value), started(Clock::now()), progress(started)
        {
        }

        SocketOwner socket;
        Clock::time_point started, progress;
        std::string input, output, fileOutput;
        HttpRequest request;
        std::size_t headerLength = 0, bodyLength = 0, sent = 0;
        bool parsed = false, dispatched = false;
        unsigned status = 200;
        std::string contentType = "application/json; charset=utf-8";
        std::unique_ptr<JsonEncoder> encoder;
        std::unique_ptr<StaticFile> file;
    };

    Impl(vna_service::InstrumentService& service, const WebOptions& value)
        : runtime("Web WSAStartup failed"), api(service), options(value)
    {
        if (!options.enabled)
            return;
        listener.reset(socket(AF_INET, SOCK_STREAM, IPPROTO_TCP));
        if (listener.get() == kInvalidSocket)
            throw std::runtime_error("Cannot create Web listener");
        sockaddr_in address{};
        address.sin_family = AF_INET;
        address.sin_port = htons(options.port);
        if (inet_pton(AF_INET, options.bindAddress.c_str(), &address.sin_addr) != 1)
            throw std::runtime_error("Web bind address must be an IPv4 literal");
#ifndef _WIN32
        int reuse = 1;
        setsockopt(listener.get(), SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
#endif
        if (bind(listener.get(), reinterpret_cast<const sockaddr*>(&address), sizeof(address)) != 0 ||
            listen(listener.get(), static_cast<int>(kConnectionLimit)) != 0 || !SetNonBlocking(listener.get()))
            throw std::runtime_error("Cannot bind Web listener");
        SocketLength length = sizeof(address);
        if (getsockname(listener.get(), reinterpret_cast<sockaddr*>(&address), &length) != 0)
            throw std::runtime_error("Cannot inspect Web listener");
        actualPort = ntohs(address.sin_port);
        std::cout << "WEB_LISTENING http://" << options.bindAddress << ':' << actualPort << std::endl;
    }

    NetworkRuntime runtime;
    SocketOwner listener;
    HttpApi api;
    WebOptions options;
    std::vector<std::unique_ptr<Client>> clients;
    std::uint16_t actualPort = 0;
    bool closing = false;

    void respond(Client& client, HttpResponse response)
    {
        client.dispatched = true;
        client.input.clear();
        client.status = response.status;
        client.contentType = "application/json; charset=utf-8";
        client.encoder.reset(new JsonEncoder(std::move(response.json)));
        client.file.reset();
        client.fileOutput.clear();
        client.progress = Clock::now();
    }

    void error(Client& client, unsigned status, const std::string& message)
    {
        respond(client, httpError(status, "http_error", message));
    }

    bool parseHeaders(Client& client)
    {
        const auto end = client.input.find("\r\n\r\n");
        if (end == std::string::npos) {
            if (client.input.size() > kHeadersLimit)
                error(client, 431, "Request headers exceed 8 KiB");
            return false;
        }
        client.headerLength = end + 4U;
        if (client.headerLength > kHeadersLimit) {
            error(client, 431, "Request headers exceed 8 KiB");
            return false;
        }
        const auto firstEnd = client.input.find("\r\n");
        std::istringstream first(client.input.substr(0, firstEnd));
        std::string target, version, extra;
        if (!(first >> client.request.method >> target >> version) || first >> extra || version != "HTTP/1.1")
            throw std::runtime_error("Expected HTTP/1.1 request line");
        for (unsigned char character : client.request.method)
            if (!headerCharacter(character))
                throw std::runtime_error("Invalid HTTP method");
        client.request.path = decodePath(target);
        std::size_t cursor = firstEnd + 2U;
        while (cursor < end) {
            const auto lineEnd = client.input.find("\r\n", cursor);
            if (lineEnd == std::string::npos || lineEnd > end)
                throw std::runtime_error("Invalid header line");
            const std::string line = client.input.substr(cursor, lineEnd - cursor);
            const auto colon = line.find(':');
            if (colon == std::string::npos || colon == 0U)
                throw std::runtime_error("Invalid header field");
            const std::string name = lower(line.substr(0, colon));
            for (unsigned char character : name)
                if (!headerCharacter(character))
                    throw std::runtime_error("Invalid header name");
            const std::string value = trim(line.substr(colon + 1U));
            for (unsigned char character : value)
                if ((character < 32U && character != '\t') || character == 127U)
                    throw std::runtime_error("Invalid header value");
            if (!client.request.headers.emplace(name, value).second)
                throw std::runtime_error("Duplicate header field");
            cursor = lineEnd + 2U;
        }
        if (client.request.headers.count("host") == 0U || client.request.headers.at("host").empty())
            throw std::runtime_error("HTTP/1.1 Host is required");
        if (client.request.headers.count("transfer-encoding") != 0U)
            throw std::runtime_error("Transfer-Encoding is unsupported");
        if (client.request.headers.count("expect") != 0U) {
            error(client, 417, "Expect is unsupported");
            return false;
        }
        const auto length = client.request.headers.find("content-length");
        if (length != client.request.headers.end()) {
            if (length->second.empty())
                throw std::runtime_error("Empty Content-Length");
            for (char digit : length->second) {
                if (digit < '0' || digit > '9')
                    throw std::runtime_error("Invalid Content-Length");
                if (client.bodyLength > kBodyLimit / 10U) {
                    error(client, 413, "Request body exceeds 16 KiB");
                    return false;
                }
                client.bodyLength = client.bodyLength * 10U + static_cast<unsigned>(digit - '0');
                if (client.bodyLength > kBodyLimit) {
                    error(client, 413, "Request body exceeds 16 KiB");
                    return false;
                }
            }
        }
        if ((client.request.method == "POST" || client.request.method == "DELETE") &&
            length == client.request.headers.end())
            throw std::runtime_error("Content-Length is required for writes");
        if (client.bodyLength != 0U && client.request.path.find("/api/") == 0U) {
            const auto type = client.request.headers.find("content-type");
            if (type == client.request.headers.end() ||
                lower(trim(type->second.substr(0, type->second.find(';')))) != "application/json") {
                error(client, 415, "API body requires application/json");
                return false;
            }
        }
        client.parsed = true;
        return true;
    }

    void dispatch(Client& client)
    {
        client.request.body = client.input.substr(client.headerLength, client.bodyLength);
        if (client.request.path.find("/api/") == 0U) {
            respond(client, api.handle(client.request));
            return;
        }
        if (client.request.method != "GET") {
            error(client, 405, "Static files require GET");
            return;
        }
        if (client.bodyLength != 0U) {
            error(client, 400, "GET body is unsupported");
            return;
        }
        try {
            client.file.reset(new StaticFile(options.staticRoot, client.request.path));
            client.contentType = mimeType(client.request.path == "/" ? "/index.html" : client.request.path);
            client.dispatched = true;
            client.input.clear();
            client.progress = Clock::now();
        } catch (const std::exception&) {
            error(client, 404, "Static file not found");
        }
    }

    bool receive(Client& client)
    {
        char buffer[kReceiveBudget];
        const int count = recv(client.socket.get(), buffer, sizeof(buffer), 0);
        if (count == 0)
            return false;
        if (count < 0)
            return WouldBlock(SocketError());
        client.input.append(buffer, static_cast<std::size_t>(count));
        try {
            if (!client.parsed && !parseHeaders(client))
                return true;
            if (client.dispatched)
                return true;
            const std::size_t complete = client.headerLength + client.bodyLength;
            if (client.input.size() > complete) {
                error(client, 400, "One HTTP message per connection is supported");
                return true;
            }
            if (client.input.size() == complete)
                dispatch(client);
        } catch (const std::exception& failure) {
            error(client, 400, failure.what());
        }
        return true;
    }

    bool sendOutput(Client& client)
    {
        try {
            if (client.encoder && client.encoder->advance(256U)) {
                std::string body = client.encoder->take();
                client.encoder.reset();
                client.output = responseHeader(client.status, client.contentType, body.size()) + body;
            }
            if (client.file && client.file->advance(client.fileOutput)) {
                client.output =
                    responseHeader(client.status, client.contentType, client.fileOutput.size()) + client.fileOutput;
                client.file.reset();
                client.fileOutput.clear();
            }
        } catch (const std::exception&) {
            client.encoder.reset();
            client.file.reset();
            client.fileOutput.clear();
            const std::string body =
                "{\"error\":{\"code\":\"output_failed\",\"message\":\"Response encoding failed\"}}";
            client.output = responseHeader(500, "application/json", body.size()) + body;
        }
        if (client.output.empty())
            return true;
        const auto count =
            static_cast<int>((std::min)(client.output.size() - client.sent, static_cast<std::size_t>(kSendBudget)));
#ifdef MSG_NOSIGNAL
        const int flags = MSG_NOSIGNAL;
#else
        const int flags = 0;
#endif
        const int sent = send(client.socket.get(), client.output.data() + client.sent, count, flags);
        if (sent <= 0)
            return sent < 0 && WouldBlock(SocketError());
        client.sent += static_cast<std::size_t>(sent);
        client.progress = Clock::now();
        return client.sent != client.output.size();
    }

    void poll()
    {
        api.poll();
        if (listener.get() != kInvalidSocket) {
            for (unsigned index = 0; index < 4U; ++index) {
                Socket socket = accept(listener.get(), nullptr, nullptr);
                if (socket == kInvalidSocket)
                    break;
                if (clients.size() >= kConnectionLimit || !SetNonBlocking(socket)) {
                    CloseSocket(socket);
                    continue;
                }
                clients.emplace_back(new Client(socket));
            }
        }
        const auto time = Clock::now();
        for (auto client = clients.begin(); client != clients.end();) {
            bool alive = true;
            if (!(*client)->dispatched && time - (*client)->started >= std::chrono::seconds(10))
                error(**client, 408, "Request receive timeout");
            if ((*client)->dispatched) {
                alive = time - (*client)->progress < std::chrono::seconds(10) && sendOutput(**client);
            } else
                alive = receive(**client);
            if (!alive)
                client = clients.erase(client);
            else
                ++client;
        }
    }
};

WebEndpoint::WebEndpoint(vna_service::InstrumentService& service, const WebOptions& options)
    : impl_(new Impl(service, options))
{
}

WebEndpoint::~WebEndpoint() = default;

void WebEndpoint::poll()
{
    impl_->poll();
}

void WebEndpoint::beginShutdown()
{
    if (impl_->closing)
        return;
    impl_->closing = true;
    impl_->listener.reset();
    impl_->clients.clear();
    impl_->api.beginShutdown();
}

bool WebEndpoint::drained() const
{
    return impl_->clients.empty() && impl_->api.drained();
}

std::uint16_t WebEndpoint::port() const
{
    return impl_->actualPort;
}
}  // namespace vna_web
