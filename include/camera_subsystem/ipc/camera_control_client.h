/**
 * @file camera_control_client.h
 * @brief Camera 控制面客户端（子发布端/订阅端进程）
 * @author CameraSubsystem Team
 * @date 2026-03-01
 */

#ifndef CAMERA_SUBSYSTEM_IPC_CAMERA_CONTROL_CLIENT_H
#define CAMERA_SUBSYSTEM_IPC_CAMERA_CONTROL_CLIENT_H

#include "camera_subsystem/ipc/camera_control_ipc.h"

#include <string>

namespace camera_subsystem
{
namespace ipc
{

class CameraControlClient
{
public:
    CameraControlClient();
    ~CameraControlClient();

    bool Connect(const std::string& socket_path = kDefaultCameraControlSocketPath);
    void Disconnect();
    bool IsConnected() const;

    bool Subscribe(const std::string& client_id,
                   CameraClientRole role,
                   const CameraEndpoint& endpoint,
                   CameraControlResponse* response);

    bool Unsubscribe(const std::string& client_id,
                     CameraClientRole role,
                     const CameraEndpoint& endpoint,
                     CameraControlResponse* response);

    bool Ping(CameraControlResponse* response);

private:
    bool SendRequest(const CameraControlRequest& request, CameraControlResponse* response);

    static bool ReadFull(int fd, void* buffer, size_t length);
    static bool WriteFull(int fd, const void* buffer, size_t length);

    int socket_fd_;
    std::string socket_path_;
};

} // namespace ipc
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_IPC_CAMERA_CONTROL_CLIENT_H
