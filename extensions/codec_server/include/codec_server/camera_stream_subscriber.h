#ifndef CODEC_SERVER_CAMERA_STREAM_SUBSCRIBER_H
#define CODEC_SERVER_CAMERA_STREAM_SUBSCRIBER_H

#include <atomic>
#include <cstdint>
#include <string>
#include <thread>

namespace camera_subsystem::extensions::codec_server {

struct CameraStreamSubscriberConfig
{
    std::string control_socket = "/tmp/camera_subsystem_control.sock";
    std::string data_socket = "/tmp/camera_subsystem_data.sock";
    std::string device_path = "/dev/video45";
    std::string client_id = "camera_codec_server";
    uint32_t camera_id = 0;
    uint32_t max_frame_size = 64U * 1024U * 1024U;
};

struct CameraStreamSubscriberStats
{
    uint64_t input_frames = 0;
    uint64_t input_bytes = 0;
    uint64_t read_failures = 0;
    uint64_t invalid_frames = 0;
};

class CameraStreamSubscriber
{
public:
    CameraStreamSubscriber() = default;
    ~CameraStreamSubscriber();

    CameraStreamSubscriber(const CameraStreamSubscriber&) = delete;
    CameraStreamSubscriber& operator=(const CameraStreamSubscriber&) = delete;

    bool Start(const CameraStreamSubscriberConfig& config);
    void Stop();
    bool IsRunning() const;
    CameraStreamSubscriberStats GetStats() const;

private:
    bool ConnectDataSocket();
    bool ConnectControlSocket();
    bool SendControlRequest(uint32_t command);
    void ReadLoop();
    bool ReadFull(int fd, void* buffer, size_t length);

    CameraStreamSubscriberConfig config_;
    int data_fd_ = -1;
    int control_fd_ = -1;
    std::atomic<bool> is_running_{false};
    std::atomic<bool> is_subscribed_{false};
    std::thread reader_thread_;

    std::atomic<uint64_t> input_frames_{0};
    std::atomic<uint64_t> input_bytes_{0};
    std::atomic<uint64_t> read_failures_{0};
    std::atomic<uint64_t> invalid_frames_{0};
};

} // namespace camera_subsystem::extensions::codec_server

#endif // CODEC_SERVER_CAMERA_STREAM_SUBSCRIBER_H
