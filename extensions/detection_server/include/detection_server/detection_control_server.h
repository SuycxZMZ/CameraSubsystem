#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_CONTROL_SERVER_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_CONTROL_SERVER_H

#include <atomic>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace camera_subsystem::extensions::detection_server {

class DetectionControlServer
{
  public:
    using RequestHandler = std::function<std::string(const std::string&)>;

    DetectionControlServer() = default;
    ~DetectionControlServer();

    DetectionControlServer(const DetectionControlServer&) = delete;
    DetectionControlServer& operator=(const DetectionControlServer&) = delete;

    bool Start(const std::string& socket_path, RequestHandler request_handler);
    void Stop();
    bool IsRunning() const;

  private:
    void AcceptLoop();
    void ClientLoop(int client_fd);
    static bool ReadLine(int fd, std::string* line);
    static bool WriteFull(int fd, const void* buffer, size_t length);

    int listen_fd_ = -1;
    std::string socket_path_;
    RequestHandler request_handler_;
    std::atomic<bool> is_running_{false};
    std::thread accept_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<int> client_fds_;
    std::vector<std::thread> client_threads_;
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_CONTROL_SERVER_H
