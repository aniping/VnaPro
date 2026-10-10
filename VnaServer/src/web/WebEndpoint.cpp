#include "web/WebEndpoint.h"

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
// clang-format off
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
// clang-format on
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

#include <algorithm>
#include <cctype>
#include <cerrno>
#include <chrono>
#include <iostream>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <utility>
#include <vector>

#include "web/HttpApi.h"

namespace vna_web {
namespace {
using Clock = std::chrono::steady_clock;
constexpr std::size_t kHeadersLimit = 8192U;
constexpr std::size_t kBodyLimit = 16384U;
constexpr std::size_t kOutputLimit = 4U * 1024U * 1024U;
constexpr std::size_t kConnectionLimit = 8U;
constexpr unsigned kReceiveBudget = 4096U;
constexpr unsigned kSendBudget = 32768U;

#ifdef _WIN32
using Socket = SOCKET;
using SocketLength = int;
constexpr Socket kInvalidSocket = INVALID_SOCKET;
int socketError()
{
    return WSAGetLastError();
}
bool wouldBlock(int error)
{
    return error == WSAEWOULDBLOCK || error == WSAEINTR;
}
void closeSocket(Socket value)
{
    closesocket(value);
}
bool nonBlocking(Socket value)
{
    u_long enabled = 1;
    return ioctlsocket(value, FIONBIO, &enabled) == 0;
}
struct NetworkRuntime {
    NetworkRuntime()
    {
        WSADATA data;
        if (WSAStartup(MAKEWORD(2, 2), &data) != 0)
            throw std::runtime_error("Web WSAStartup failed");
    }
    ~NetworkRuntime() { WSACleanup(); }
};
#else
using Socket = int;
using SocketLength = socklen_t;
constexpr Socket kInvalidSocket = -1;
int socketError()
{
    return errno;
}
bool wouldBlock(int error)
{
    return error == EAGAIN || error == EWOULDBLOCK || error == EINTR;
}
void closeSocket(Socket value)
{
    close(value);
}
bool nonBlocking(Socket value)
{
    const int flags = fcntl(value, F_GETFL, 0);
    return flags >= 0 && fcntl(value, F_SETFL, flags | O_NONBLOCK) == 0;
}
struct NetworkRuntime {};
#endif

class SocketOwner {
public:
    explicit SocketOwner(Socket value = kInvalidSocket) : value_(value) {}
    ~SocketOwner() { reset(); }
    SocketOwner(const SocketOwner&) = delete;
    SocketOwner& operator=(const SocketOwner&) = delete;
    Socket get() const { return value_; }
    void reset(Socket value = kInvalidSocket)
    {
        if (value_ != kInvalidSocket)
            closeSocket(value_);
        value_ = value;
    }

private:
    Socket value_;
};

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

// Retain the verified file handle throughout incremental reading. Checking a
// path and reopening it would allow a link replacement between those actions.
class StaticFile {
public:
    StaticFile(const std::string& root, const std::string& path)
    {
        if (root.empty())
            throw std::runtime_error("Static files are disabled");
        std::string relative = path == "/" ? "index.html" : path.substr(1U);
        if (relative.empty() || relative.back() == '/')
            throw std::runtime_error("Static file not found");
#ifdef _WIN32
        HANDLE directory =
            CreateFileA(root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
        if (directory == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Static root unavailable");
        std::string rootPath;
        try {
            rootPath = finalPath(directory);
        } catch (...) {
            CloseHandle(directory);
            throw;
        }
        CloseHandle(directory);
        std::replace(relative.begin(), relative.end(), '/', '\\');
        const std::string fullPath = root + "\\" + relative;
        file_ = CreateFileA(fullPath.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
            nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        if (file_ == INVALID_HANDLE_VALUE)
            throw std::runtime_error("Static file not found");
        try {
            const std::string actual = lower(finalPath(file_));
            const std::string prefix = lower(rootPath) + "\\";
            if (actual.compare(0, prefix.size(), prefix) != 0)
                throw std::runtime_error("Static file escapes root");
            LARGE_INTEGER length;
            BY_HANDLE_FILE_INFORMATION information;
            if (!GetFileSizeEx(file_, &length) || !GetFileInformationByHandle(file_, &information) ||
                (information.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) != 0U || length.QuadPart < 0 ||
                static_cast<unsigned long long>(length.QuadPart) > kOutputLimit)
                throw std::runtime_error("Static file is unavailable or oversized");
            size_ = static_cast<std::size_t>(length.QuadPart);
        } catch (...) {
            CloseHandle(file_);
            file_ = INVALID_HANDLE_VALUE;
            throw;
        }
#else
        int directory = open(root.c_str(), O_RDONLY | O_DIRECTORY);
        if (directory < 0)
            throw std::runtime_error("Static root unavailable");
        std::istringstream parts(relative);
        std::vector<std::string> segments;
        std::string segment;
        while (std::getline(parts, segment, '/')) {
            if (segment.empty()) {
                close(directory);
                throw std::runtime_error("Invalid static path");
            }
            segments.push_back(segment);
        }
        for (std::size_t index = 0; index < segments.size(); ++index) {
            const bool final = index + 1U == segments.size();
            const int next =
                openat(directory, segments[index].c_str(), O_RDONLY | O_NOFOLLOW | (final ? 0 : O_DIRECTORY));
            close(directory);
            directory = next;
            if (directory < 0)
                throw std::runtime_error("Static file not found");
        }
        struct stat information;
        if (fstat(directory, &information) != 0 || !S_ISREG(information.st_mode) || information.st_size < 0 ||
            static_cast<unsigned long long>(information.st_size) > kOutputLimit) {
            close(directory);
            throw std::runtime_error("Invalid static file");
        }
        file_ = directory;
        size_ = static_cast<std::size_t>(information.st_size);
#endif
    }
    ~StaticFile()
    {
#ifdef _WIN32
        if (file_ != INVALID_HANDLE_VALUE)
            CloseHandle(file_);
#else
        if (file_ >= 0)
            close(file_);
#endif
    }
    std::size_t size() const { return size_; }
    bool advance(std::string& output)
    {
        if (output.size() == size_)
            return true;
        char buffer[16384];
        const unsigned maximum = static_cast<unsigned>((std::min)(size_ - output.size(), sizeof(buffer)));
#ifdef _WIN32
        DWORD count = 0;
        if (!ReadFile(file_, buffer, maximum, &count, nullptr) || count == 0U)
            throw std::runtime_error("Static file read failed");
#else
        const ssize_t count = read(file_, buffer, maximum);
        if (count <= 0)
            throw std::runtime_error("Static file read failed");
#endif
        output.append(buffer, static_cast<std::size_t>(count));
        return output.size() == size_;
    }

private:
    std::size_t size_ = 0;
#ifdef _WIN32
    HANDLE file_ = INVALID_HANDLE_VALUE;
    static std::string finalPath(HANDLE file)
    {
        std::vector<char> buffer(32768U);
        const DWORD count =
            GetFinalPathNameByHandleA(file, buffer.data(), static_cast<DWORD>(buffer.size()), FILE_NAME_NORMALIZED);
        if (count == 0U || count >= buffer.size())
            throw std::runtime_error("Cannot verify static file path");
        std::string result(buffer.data(), count);
        while (!result.empty() && (result.back() == '\\' || result.back() == '/'))
            result.pop_back();
        return result;
    }
#else
    int file_ = -1;
#endif
};
}  // namespace

class WebEndpoint::Impl {
public:
    struct Client {
        explicit Client(Socket value) : socket(value), started(Clock::now()), progress(started) {}
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
    Impl(vna_service::InstrumentService& service, const WebOptions& value) : api(service), options(value)
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
            listen(listener.get(), static_cast<int>(kConnectionLimit)) != 0 || !nonBlocking(listener.get()))
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
            return wouldBlock(socketError());
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
            return sent < 0 && wouldBlock(socketError());
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
                if (clients.size() >= kConnectionLimit || !nonBlocking(socket)) {
                    closeSocket(socket);
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
