#ifndef CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_CAMERA_FRAME_SUBSCRIBER_H
#define CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_CAMERA_FRAME_SUBSCRIBER_H

#include "camera_subsystem/ipc/camera_data_ipc.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

namespace camera_subsystem::extensions::detection_server {

struct CameraFrameSubscriberConfig
{
    std::string control_socket = "/tmp/camera_subsystem_control.sock";
    std::string data_socket = "/tmp/camera_subsystem_data.sock";
    std::string device_path = "/dev/video45";
    std::string stream_id = "usb0";
    std::string client_id = "camera_detection_server";
    uint32_t camera_id = 0;
    uint32_t max_frame_size = 64U * 1024U * 1024U;
    std::function<void(const camera_subsystem::ipc::CameraDataFrameHeader&,
                       const std::vector<uint8_t>&)>
        frame_callback;
};

struct CameraFrameSubscriberStats
{
    uint64_t input_frames = 0;
    uint64_t input_bytes = 0;
    uint64_t read_failures = 0;
    uint64_t invalid_frames = 0;
};

class CameraFrameSubscriber
{
  public:
    CameraFrameSubscriber() = default;
    ~CameraFrameSubscriber();

    CameraFrameSubscriber(const CameraFrameSubscriber&) = delete;
    CameraFrameSubscriber& operator=(const CameraFrameSubscriber&) = delete;

    bool Start(const CameraFrameSubscriberConfig& config);
    void Stop();
    bool IsRunning() const;
    CameraFrameSubscriberStats GetStats() const;
    int GetLastErrorNo() const;
    std::string GetLastErrorStage() const;
    std::string GetLastErrorMessage() const;

  private:
    bool ConnectDataSocket();
    bool ConnectControlSocket();
    bool SendControlRequest(uint32_t command);
    void ReadLoop();
    bool ReadFull(int fd, void* buffer, size_t length);
    void SetLastError(const std::string& stage, int error_no, const std::string& message);

    CameraFrameSubscriberConfig config_;
    int data_fd_ = -1;
    int control_fd_ = -1;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_subscribed_{false};
    std::thread reader_thread_;

    std::atomic<uint64_t> input_frames_{0};
    std::atomic<uint64_t> input_bytes_{0};
    std::atomic<uint64_t> read_failures_{0};
    std::atomic<uint64_t> invalid_frames_{0};
    mutable std::mutex error_mutex_;
    int last_error_no_ = 0;
    std::string last_error_stage_;
    std::string last_error_message_;
};

} // namespace camera_subsystem::extensions::detection_server

#endif // CAMERA_SUBSYSTEM_EXTENSIONS_DETECTION_SERVER_CAMERA_FRAME_SUBSCRIBER_H
