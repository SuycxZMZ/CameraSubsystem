#include "web_preview/web_server.h"

#include "web_preview/base64.h"
#include "web_preview/sha1.h"

#include <algorithm>
#include <arpa/inet.h>
#include <cerrno>
#include <cctype>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <netinet/in.h>
#include <sstream>
#include <sys/socket.h>
#include <sys/stat.h>
#include <unistd.h>

namespace web_preview {
namespace {

constexpr const char* kWebSocketGuid = "258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
constexpr size_t kMaxHttpRequestSize = 16U * 1024U;
constexpr size_t kMaxControlPayloadSize = 64U * 1024U;

void CloseFd(int* fd)
{
    if (fd && *fd >= 0)
    {
        shutdown(*fd, SHUT_RDWR);
        close(*fd);
        *fd = -1;
    }
}

bool ReadFull(int fd, void* buffer, size_t length)
{
    size_t total = 0;
    auto* out = reinterpret_cast<uint8_t*>(buffer);
    while (total < length)
    {
        const ssize_t n = recv(fd, out + total, length - total, 0);
        if (n == 0)
        {
            return false;
        }
        if (n < 0)
        {
            if (errno == EINTR)
            {
                continue;
            }
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

uint16_t ReadNetwork16(const uint8_t* data)
{
    return static_cast<uint16_t>((static_cast<uint16_t>(data[0]) << 8U) |
                                 static_cast<uint16_t>(data[1]));
}

uint64_t ReadNetwork64(const uint8_t* data)
{
    uint64_t value = 0;
    for (int i = 0; i < 8; ++i)
    {
        value = (value << 8U) | static_cast<uint64_t>(data[i]);
    }
    return value;
}

void AppendNetwork16(std::vector<uint8_t>* out, uint16_t value)
{
    out->push_back(static_cast<uint8_t>((value >> 8U) & 0xffU));
    out->push_back(static_cast<uint8_t>(value & 0xffU));
}

void AppendNetwork64(std::vector<uint8_t>* out, uint64_t value)
{
    for (int i = 7; i >= 0; --i)
    {
        out->push_back(static_cast<uint8_t>((value >> (i * 8)) & 0xffU));
    }
}

std::string Trim(std::string value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front())))
    {
        value.erase(value.begin());
    }
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back())))
    {
        value.pop_back();
    }
    return value;
}

std::string ToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

bool FileExists(const std::string& path)
{
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

bool ReadFile(const std::string& path, std::string* content)
{
    if (!content)
    {
        return false;
    }
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open())
    {
        return false;
    }
    std::ostringstream ss;
    ss << in.rdbuf();
    *content = ss.str();
    return true;
}

std::string PixelFormatToString(WebPixelFormat format)
{
    switch (format)
    {
        case WebPixelFormat::kJpeg:
            return "JPEG";
        case WebPixelFormat::kRgb:
            return "RGB";
        case WebPixelFormat::kRgba:
            return "RGBA";
        case WebPixelFormat::kNv12:
            return "NV12";
        case WebPixelFormat::kYuyv:
            return "YUYV";
        case WebPixelFormat::kUyvy:
            return "UYVY";
        default:
            return "UNKNOWN";
    }
}

} // namespace

WebServer::WebServer()
    : running_(false)
    , server_fd_(-1)
{
}

WebServer::~WebServer()
{
    Stop();
}

bool WebServer::Start(const GatewayConfig& config)
{
    if (running_.load())
    {
        return true;
    }

    config_ = config;
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd_ < 0)
    {
        std::cerr << "http socket failed: " << strerror(errno) << "\n";
        return false;
    }

    int yes = 1;
    (void)setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));

    sockaddr_in addr;
    std::memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons(config_.http_port);
    if (config_.bind_host == "0.0.0.0")
    {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);
    }
    else if (inet_pton(AF_INET, config_.bind_host.c_str(), &addr.sin_addr) != 1)
    {
        std::cerr << "invalid bind host: " << config_.bind_host << "\n";
        CloseFd(&server_fd_);
        return false;
    }

    if (bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0)
    {
        std::cerr << "http bind failed: " << strerror(errno) << "\n";
        CloseFd(&server_fd_);
        return false;
    }

    if (listen(server_fd_, 32) < 0)
    {
        std::cerr << "http listen failed: " << strerror(errno) << "\n";
        CloseFd(&server_fd_);
        return false;
    }

    running_.store(true);
    accept_thread_ = std::thread(&WebServer::AcceptLoop, this);
    return true;
}

void WebServer::Stop()
{
    if (!running_.exchange(false))
    {
        CloseFd(&server_fd_);
        return;
    }

    CloseFd(&server_fd_);
    if (accept_thread_.joinable())
    {
        accept_thread_.join();
    }

    std::lock_guard<std::mutex> lock(clients_mutex_);
    for (const auto& client : clients_)
    {
        client->alive.store(false);
        if (client->fd >= 0)
        {
            shutdown(client->fd, SHUT_RDWR);
            close(client->fd);
            client->fd = -1;
        }
    }
    clients_.clear();
}

void WebServer::BroadcastBinary(const std::vector<uint8_t>& packet)
{
    std::vector<std::shared_ptr<Client>> clients;
    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients = clients_;
    }

    for (const auto& client : clients)
    {
        if (!client->alive.load())
        {
            continue;
        }
        std::lock_guard<std::mutex> send_lock(client->send_mutex);
        if (!SendWebSocketFrame(client->fd, 0x2, packet.data(), packet.size()))
        {
            client->alive.store(false);
            shutdown(client->fd, SHUT_RDWR);
        }
    }

    RemoveDeadClients();
}

void WebServer::UpdateStats(const StreamStats& stats)
{
    std::lock_guard<std::mutex> lock(stats_mutex_);
    stats_ = stats;
}

void WebServer::AcceptLoop()
{
    while (running_.load())
    {
        sockaddr_in client_addr;
        socklen_t client_len = sizeof(client_addr);
        const int client_fd =
            accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len);
        if (client_fd < 0)
        {
            if (running_.load() && errno != EINTR)
            {
                std::cerr << "http accept failed: " << strerror(errno) << "\n";
            }
            continue;
        }

        std::thread(&WebServer::HandleConnection, this, client_fd).detach();
    }
}

void WebServer::HandleConnection(int client_fd)
{
    std::string request;
    request.reserve(4096);
    char buffer[1024];
    while (request.find("\r\n\r\n") == std::string::npos &&
           request.size() < kMaxHttpRequestSize)
    {
        const ssize_t n = recv(client_fd, buffer, sizeof(buffer), 0);
        if (n <= 0)
        {
            close(client_fd);
            return;
        }
        request.append(buffer, static_cast<size_t>(n));
    }

    if (request.find("Upgrade: websocket") != std::string::npos ||
        request.find("upgrade: websocket") != std::string::npos)
    {
        auto client = std::make_shared<Client>();
        client->fd = client_fd;
        HandleWebSocket(client, request);
        return;
    }

    (void)ServeHttp(client_fd, request);
    close(client_fd);
}

void WebServer::HandleWebSocket(std::shared_ptr<Client> client, const std::string& request)
{
    const std::string key = ExtractHeader(request, "sec-websocket-key");
    if (key.empty())
    {
        close(client->fd);
        return;
    }

    const std::string accept_input = key + kWebSocketGuid;
    const auto digest = Sha1(accept_input);
    const std::string accept_value = Base64Encode(digest.data(), digest.size());

    std::ostringstream response;
    response << "HTTP/1.1 101 Switching Protocols\r\n"
             << "Upgrade: websocket\r\n"
             << "Connection: Upgrade\r\n"
             << "Sec-WebSocket-Accept: " << accept_value << "\r\n\r\n";

    const std::string response_text = response.str();
    if (!WriteFull(client->fd, response_text.data(), response_text.size()))
    {
        close(client->fd);
        return;
    }

    {
        std::lock_guard<std::mutex> lock(clients_mutex_);
        clients_.push_back(client);
    }

    {
        const std::string status = BuildStatusJson();
        std::lock_guard<std::mutex> send_lock(client->send_mutex);
        (void)SendWebSocketFrame(client->fd, 0x1,
                                 reinterpret_cast<const uint8_t*>(status.data()),
                                 status.size());
    }

    client->read_thread = std::thread(&WebServer::ClientReadLoop, this, client);
    client->read_thread.detach();
}

bool WebServer::ServeHttp(int client_fd, const std::string& request)
{
    std::istringstream ss(request);
    std::string method;
    std::string url;
    std::string version;
    ss >> method >> url >> version;

    if (method != "GET")
    {
        const std::string body = "method not allowed\n";
        const std::string response =
            "HTTP/1.1 405 Method Not Allowed\r\nContent-Length: " +
            std::to_string(body.size()) + "\r\nContent-Type: text/plain\r\n\r\n" + body;
        return WriteFull(client_fd, response.data(), response.size());
    }

    if (url == "/status")
    {
        const std::string body = BuildStatusJson();
        const std::string response =
            "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
            "\r\nContent-Type: application/json\r\nCache-Control: no-store\r\n\r\n" + body;
        return WriteFull(client_fd, response.data(), response.size());
    }

    std::string body;
    std::string content_type = "text/html";
    const std::string path = ResolvePath(url);
    if (!path.empty() && FileExists(path) && ReadFile(path, &body))
    {
        content_type = ContentTypeForPath(path);
    }
    else if (url == "/" || url == "/index.html")
    {
        body = BuildFallbackIndex();
        content_type = "text/html";
    }
    else
    {
        body = "not found\n";
        const std::string response =
            "HTTP/1.1 404 Not Found\r\nContent-Length: " + std::to_string(body.size()) +
            "\r\nContent-Type: text/plain\r\n\r\n" + body;
        return WriteFull(client_fd, response.data(), response.size());
    }

    const std::string response =
        "HTTP/1.1 200 OK\r\nContent-Length: " + std::to_string(body.size()) +
        "\r\nContent-Type: " + content_type +
        "\r\nCache-Control: no-store\r\n\r\n" + body;
    return WriteFull(client_fd, response.data(), response.size());
}

bool WebServer::SendWebSocketFrame(int fd, uint8_t opcode, const uint8_t* data, size_t size)
{
    if (fd < 0)
    {
        return false;
    }

    std::vector<uint8_t> header;
    header.reserve(14);
    header.push_back(static_cast<uint8_t>(0x80U | (opcode & 0x0fU)));
    if (size <= 125)
    {
        header.push_back(static_cast<uint8_t>(size));
    }
    else if (size <= 0xffffU)
    {
        header.push_back(126);
        AppendNetwork16(&header, static_cast<uint16_t>(size));
    }
    else
    {
        header.push_back(127);
        AppendNetwork64(&header, static_cast<uint64_t>(size));
    }

    return WriteFull(fd, header.data(), header.size()) &&
           (size == 0 || WriteFull(fd, data, size));
}

void WebServer::ClientReadLoop(std::shared_ptr<Client> client)
{
    while (running_.load() && client->alive.load())
    {
        uint8_t header[2];
        if (!ReadFull(client->fd, header, sizeof(header)))
        {
            break;
        }

        const uint8_t opcode = header[0] & 0x0fU;
        const bool masked = (header[1] & 0x80U) != 0;
        uint64_t payload_len = header[1] & 0x7fU;
        if (payload_len == 126)
        {
            uint8_t ext[2];
            if (!ReadFull(client->fd, ext, sizeof(ext)))
            {
                break;
            }
            payload_len = ReadNetwork16(ext);
        }
        else if (payload_len == 127)
        {
            uint8_t ext[8];
            if (!ReadFull(client->fd, ext, sizeof(ext)))
            {
                break;
            }
            payload_len = ReadNetwork64(ext);
        }

        if (payload_len > kMaxControlPayloadSize)
        {
            break;
        }

        uint8_t mask[4] = {0, 0, 0, 0};
        if (masked && !ReadFull(client->fd, mask, sizeof(mask)))
        {
            break;
        }

        std::vector<uint8_t> payload(static_cast<size_t>(payload_len));
        if (payload_len > 0 && !ReadFull(client->fd, payload.data(), payload.size()))
        {
            break;
        }

        if (masked)
        {
            for (size_t i = 0; i < payload.size(); ++i)
            {
                payload[i] ^= mask[i % 4U];
            }
        }

        if (opcode == 0x8)
        {
            break;
        }
        if (opcode == 0x9)
        {
            std::lock_guard<std::mutex> send_lock(client->send_mutex);
            (void)SendWebSocketFrame(client->fd, 0xA, payload.data(), payload.size());
        }
        else if (opcode == 0x1)
        {
            const std::string result =
                "{\"type\":\"command_result\",\"status\":\"not_supported\"}";
            std::lock_guard<std::mutex> send_lock(client->send_mutex);
            (void)SendWebSocketFrame(client->fd, 0x1,
                                     reinterpret_cast<const uint8_t*>(result.data()),
                                     result.size());
        }
    }

    client->alive.store(false);
    shutdown(client->fd, SHUT_RDWR);
    close(client->fd);
    client->fd = -1;
}

void WebServer::RemoveDeadClients()
{
    std::lock_guard<std::mutex> lock(clients_mutex_);
    clients_.erase(std::remove_if(clients_.begin(), clients_.end(), [](const auto& client) {
                       return !client->alive.load();
                   }),
                   clients_.end());
}

std::string WebServer::BuildFallbackIndex() const
{
    return R"HTML(<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8" />
  <meta name="viewport" content="width=device-width, initial-scale=1" />
  <title>CameraSubsystem Web Preview</title>
  <style>
    body { margin: 0; font-family: sans-serif; background: #101114; color: #f5f5f5; }
    header { padding: 16px 20px; border-bottom: 1px solid #2d3035; }
    main { padding: 20px; display: grid; gap: 16px; }
    .panel { background: #181a1f; border: 1px solid #2d3035; border-radius: 8px; padding: 16px; }
    img { width: 100%; max-height: 72vh; object-fit: contain; background: #050505; border-radius: 6px; }
    .status { color: #aab0bb; font-size: 14px; margin-top: 10px; }
  </style>
</head>
<body>
  <header><strong>CameraSubsystem Web Preview</strong></header>
  <main>
    <section class="panel">
      <img id="preview" alt="Camera preview" />
      <div class="status" id="status">connecting...</div>
    </section>
  </main>
  <script>
    const statusEl = document.getElementById('status');
    const img = document.getElementById('preview');
    const ws = new WebSocket(`ws://${location.host}/ws`);
    ws.binaryType = 'arraybuffer';
    let frames = 0;
    let lastUrl = null;
    ws.onopen = () => { statusEl.textContent = 'connected'; };
    ws.onclose = () => { statusEl.textContent = 'closed'; };
    ws.onerror = () => { statusEl.textContent = 'error'; };
    ws.onmessage = (event) => {
      if (typeof event.data === 'string') {
        statusEl.textContent = event.data;
        return;
      }
      const data = new Uint8Array(event.data);
      const view = new DataView(event.data);
      const headerSize = view.getUint32(8, true);
      const width = view.getUint32(32, true);
      const height = view.getUint32(36, true);
      const payloadSize = view.getUint32(52, true);
      const payload = data.slice(headerSize, headerSize + payloadSize);
      const blob = new Blob([payload], { type: 'image/jpeg' });
      const url = URL.createObjectURL(blob);
      img.src = url;
      if (lastUrl) URL.revokeObjectURL(lastUrl);
      lastUrl = url;
      frames += 1;
      statusEl.textContent = `${width}x${height} JPEG frames=${frames}`;
    };
  </script>
</body>
</html>
)HTML";
}

std::string WebServer::BuildStatusJson() const
{
    StreamStats stats;
    {
        std::lock_guard<std::mutex> lock(stats_mutex_);
        stats = stats_;
    }

    std::ostringstream ss;
    ss << "{\"type\":\"status\",\"stream_id\":\"usb_camera_0\","
       << "\"status\":\"" << stats.status << "\","
       << "\"width\":" << stats.width << ","
       << "\"height\":" << stats.height << ","
       << "\"format\":\"" << PixelFormatToString(stats.pixel_format) << "\","
       << "\"input_frames\":" << stats.input_frames << ","
       << "\"published_frames\":" << stats.published_frames << ","
       << "\"dropped_frames\":" << stats.dropped_frames << ","
       << "\"unsupported_frames\":" << stats.unsupported_frames << "}";
    return ss.str();
}

std::string WebServer::ResolvePath(const std::string& url_path) const
{
    std::string path = url_path;
    const size_t query = path.find('?');
    if (query != std::string::npos)
    {
        path = path.substr(0, query);
    }
    path = UrlDecodePath(path);
    if (path.empty() || path == "/")
    {
        path = "/index.html";
    }
    if (path.find("..") != std::string::npos)
    {
        return std::string();
    }
    return config_.static_root + path;
}

bool WebServer::WriteFull(int fd, const void* buffer, size_t length)
{
    size_t total = 0;
    const auto* in = reinterpret_cast<const uint8_t*>(buffer);
    while (total < length)
    {
        const ssize_t n = send(fd, in + total, length - total, MSG_NOSIGNAL);
        if (n <= 0)
        {
            if (n < 0 && errno == EINTR)
            {
                continue;
            }
            return false;
        }
        total += static_cast<size_t>(n);
    }
    return true;
}

std::string WebServer::ExtractHeader(const std::string& request, const std::string& name)
{
    const std::string lower_name = ToLower(name);
    std::istringstream ss(request);
    std::string line;
    while (std::getline(ss, line))
    {
        if (!line.empty() && line.back() == '\r')
        {
            line.pop_back();
        }
        const size_t colon = line.find(':');
        if (colon == std::string::npos)
        {
            continue;
        }
        if (ToLower(line.substr(0, colon)) == lower_name)
        {
            return Trim(line.substr(colon + 1));
        }
    }
    return std::string();
}

std::string WebServer::ContentTypeForPath(const std::string& path)
{
    if (path.size() >= 5 && path.substr(path.size() - 5) == ".html")
    {
        return "text/html";
    }
    if (path.size() >= 3 && path.substr(path.size() - 3) == ".js")
    {
        return "application/javascript";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".css")
    {
        return "text/css";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".svg")
    {
        return "image/svg+xml";
    }
    if (path.size() >= 4 && path.substr(path.size() - 4) == ".png")
    {
        return "image/png";
    }
    return "application/octet-stream";
}

std::string WebServer::UrlDecodePath(const std::string& path)
{
    std::string out;
    out.reserve(path.size());
    for (size_t i = 0; i < path.size(); ++i)
    {
        if (path[i] == '%' && i + 2 < path.size())
        {
            const std::string hex = path.substr(i + 1, 2);
            char* end = nullptr;
            const long value = std::strtol(hex.c_str(), &end, 16);
            if (end != hex.c_str() && *end == '\0')
            {
                out.push_back(static_cast<char>(value));
                i += 2;
                continue;
            }
        }
        out.push_back(path[i]);
    }
    return out;
}

} // namespace web_preview
