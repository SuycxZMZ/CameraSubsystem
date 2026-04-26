#ifndef WEB_PREVIEW_CAMERA_SUBSCRIBER_CLIENT_H
#define WEB_PREVIEW_CAMERA_SUBSCRIBER_CLIENT_H

#include "web_preview/gateway_config.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#include "camera_subsystem/ipc/camera_data_ipc.h"

namespace web_preview {

struct CameraFrame
{
    camera_subsystem::ipc::CameraDataFrameHeader header{};
    std::vector<uint8_t> payload;
};

class CameraSubscriberClient
{
public:
    using FrameCallback = std::function<void(CameraFrame&&)>;
    using StatusCallback = std::function<void(const std::string&)>;

    CameraSubscriberClient();
    ~CameraSubscriberClient();

    bool Start(const GatewayConfig& config,
               FrameCallback frame_callback,
               StatusCallback status_callback);
    void Stop();
    bool IsRunning() const;

private:
    bool ConnectControl();
    bool ConnectData();
    bool Subscribe();
    void Unsubscribe();
    void ReadLoop();

    bool SendControlRequest(uint32_t command, std::string* message);
    static bool ReadFull(int fd, void* buffer, size_t length);
    static bool WriteFull(int fd, const void* buffer, size_t length);

    GatewayConfig config_;
    FrameCallback frame_callback_;
    StatusCallback status_callback_;

    std::atomic<bool> running_;
    int control_fd_;
    int data_fd_;
    std::thread read_thread_;
};

} // namespace web_preview

#endif // WEB_PREVIEW_CAMERA_SUBSCRIBER_CLIENT_H
