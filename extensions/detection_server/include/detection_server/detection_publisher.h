#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_PUBLISHER_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_PUBLISHER_H

#include "detection_server/detection_types.h"

#include <atomic>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace camera_subsystem::extensions::detection_server {

struct DetectionPublisherConfig
{
    std::string result_socket = "/tmp/camera_subsystem_detection_result.sock";
};

struct DetectionPublisherStats
{
    uint64_t published_results = 0;
    uint64_t publish_failures = 0;
    uint64_t dropped_clients = 0;
    size_t connected_clients = 0;
};

class DetectionPublisher
{
  public:
    DetectionPublisher() = default;
    ~DetectionPublisher();

    DetectionPublisher(const DetectionPublisher&) = delete;
    DetectionPublisher& operator=(const DetectionPublisher&) = delete;

    bool Start(const DetectionPublisherConfig& config);
    void Stop();
    bool PublishResult(const DetectionResult& result);
    DetectionPublisherStats GetStats() const;
    bool IsRunning() const;

    static std::string SerializeDetectionResultJsonLine(const DetectionResult& result);

  private:
    void AcceptLoop();
    void CloseAllClients();

    DetectionPublisherConfig config_;
    int listen_fd_ = -1;
    std::atomic<bool> is_running_{false};
    std::thread accept_thread_;

    mutable std::mutex clients_mutex_;
    std::vector<int> client_fds_;

    std::atomic<uint64_t> published_results_{0};
    std::atomic<uint64_t> publish_failures_{0};
    std::atomic<uint64_t> dropped_clients_{0};
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_DETECTION_PUBLISHER_H
