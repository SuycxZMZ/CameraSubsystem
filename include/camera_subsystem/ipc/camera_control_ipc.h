/**
 * @file camera_control_ipc.h
 * @brief Camera 控制面 IPC 协议定义（Unix Domain Socket）
 * @author CameraSubsystem Team
 * @date 2026-03-01
 */

#ifndef CAMERA_SUBSYSTEM_IPC_CAMERA_CONTROL_IPC_H
#define CAMERA_SUBSYSTEM_IPC_CAMERA_CONTROL_IPC_H

#include "camera_subsystem/ipc/camera_channel_contract.h"

#include <cstdint>
#include <cstring>

namespace camera_subsystem
{
namespace ipc
{

constexpr uint32_t kCameraControlMagic = 0x434D4950;   // "CMIP"
constexpr uint32_t kCameraControlVersion = 1;
constexpr uint32_t kCameraControlClientIdMaxLength = 64;
constexpr uint32_t kCameraControlMessageTextMaxLength = 128;
constexpr const char* kDefaultCameraControlSocketPath = "/tmp/camera_subsystem_control.sock";

enum class CameraControlCommand : uint32_t
{
    kUnknown = 0,
    kSubscribe = 1,
    kUnsubscribe = 2,
    kPing = 3
};

enum class CameraControlStatus : uint32_t
{
    kOk = 0,
    kInvalidMessage = 1,
    kInvalidRole = 2,
    kCorePublisherUnavailable = 3,
    kSessionOperationFailed = 4,
    kInternalError = 5
};

struct CameraControlRequest
{
    uint32_t magic;
    uint32_t version;
    CameraControlCommand command;
    CameraClientRole role;
    CameraEndpoint endpoint;
    char client_id[kCameraControlClientIdMaxLength];
    uint8_t reserved[32];
};

struct CameraControlResponse
{
    uint32_t magic;
    uint32_t version;
    CameraControlStatus status;
    uint32_t active_subscriber_count;
    char message[kCameraControlMessageTextMaxLength];
    uint8_t reserved[32];
};

inline CameraControlRequest MakeControlRequest(CameraControlCommand command,
                                               CameraClientRole role,
                                               const CameraEndpoint& endpoint,
                                               const char* client_id)
{
    CameraControlRequest request;
    request.magic = kCameraControlMagic;
    request.version = kCameraControlVersion;
    request.command = command;
    request.role = role;
    request.endpoint = endpoint;
    std::memset(request.client_id, 0, sizeof(request.client_id));
    if (client_id && client_id[0] != '\0')
    {
        std::strncpy(request.client_id, client_id, sizeof(request.client_id) - 1);
    }
    std::memset(request.reserved, 0, sizeof(request.reserved));
    return request;
}

inline CameraControlResponse MakeControlResponse(CameraControlStatus status,
                                                 uint32_t active_subscriber_count,
                                                 const char* message)
{
    CameraControlResponse response;
    response.magic = kCameraControlMagic;
    response.version = kCameraControlVersion;
    response.status = status;
    response.active_subscriber_count = active_subscriber_count;
    std::memset(response.message, 0, sizeof(response.message));
    if (message && message[0] != '\0')
    {
        std::strncpy(response.message, message, sizeof(response.message) - 1);
    }
    std::memset(response.reserved, 0, sizeof(response.reserved));
    return response;
}

inline bool IsControlRequestHeaderValid(const CameraControlRequest& request)
{
    return request.magic == kCameraControlMagic &&
           request.version == kCameraControlVersion;
}

inline bool IsControlResponseHeaderValid(const CameraControlResponse& response)
{
    return response.magic == kCameraControlMagic &&
           response.version == kCameraControlVersion;
}

} // namespace ipc
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_IPC_CAMERA_CONTROL_IPC_H
