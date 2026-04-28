#ifndef CODEC_SERVER_CODEC_CONTROL_SERVER_H
#define CODEC_SERVER_CODEC_CONTROL_SERVER_H

#include "codec_server/recording_session_manager.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace camera_subsystem::extensions::codec_server {

class CodecControlServer
{
public:
    explicit CodecControlServer(RecordingSessionManager* session_manager);
    ~CodecControlServer();

    CodecControlServer(const CodecControlServer&) = delete;
    CodecControlServer& operator=(const CodecControlServer&) = delete;

    bool Start(const std::string& socket_path);
    void Stop();
    bool IsRunning() const;

private:
    void AcceptLoop();
    void ClientLoop(int client_fd);
    CodecControlStatus HandleRequest(const CodecControlRequest& request);

    RecordingSessionManager* session_manager_ = nullptr;
    int server_fd_ = -1;
    std::string socket_path_;
    std::atomic<bool> is_running_{false};
    std::thread accept_thread_;
    std::mutex clients_mutex_;
    std::vector<int> client_fds_;
    std::vector<std::thread> client_threads_;
};

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_CODEC_CONTROL_SERVER_H
