#ifndef WEB_PREVIEW_WEB_SERVER_H
#define WEB_PREVIEW_WEB_SERVER_H

#include "web_preview/frame_pipeline.h"
#include "web_preview/gateway_config.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace web_preview {

class WebServer
{
public:
    WebServer();
    ~WebServer();

    bool Start(const GatewayConfig& config);
    void Stop();
    void BroadcastBinary(const std::vector<uint8_t>& packet);
    void UpdateStats(const StreamStats& stats);

private:
    struct Client
    {
        int fd = -1;
        std::mutex send_mutex;
        std::atomic<bool> alive{true};
        std::thread read_thread;
    };

    void AcceptLoop();
    void HandleConnection(int client_fd);
    void HandleWebSocket(std::shared_ptr<Client> client, const std::string& request);
    bool ServeHttp(int client_fd, const std::string& request);
    bool SendWebSocketFrame(int fd, uint8_t opcode, const uint8_t* data, size_t size);
    void ClientReadLoop(std::shared_ptr<Client> client);
    void RemoveDeadClients();

    // Codec server control
    int ConnectCodecServer();
    std::string SendCodecCommand(const std::string& json_line,
                                 const std::string& stream_id);
    std::string HandleRecordCommand(const std::string& payload);

    std::string BuildFallbackIndex() const;
    std::string BuildStatusJson() const;
    std::string ResolvePath(const std::string& url_path) const;

    static bool WriteFull(int fd, const void* buffer, size_t length);
    static std::string ExtractHeader(const std::string& request, const std::string& name);
    static std::string ContentTypeForPath(const std::string& path);
    static std::string UrlDecodePath(const std::string& path);

    GatewayConfig config_;
    std::atomic<bool> running_;
    int server_fd_;
    std::thread accept_thread_;

    std::mutex clients_mutex_;
    std::vector<std::shared_ptr<Client>> clients_;

    mutable std::mutex stats_mutex_;
    StreamStats stats_;

    // Codec server command serialization.
    std::mutex codec_mutex_;
};

} // namespace web_preview

#endif // WEB_PREVIEW_WEB_SERVER_H
