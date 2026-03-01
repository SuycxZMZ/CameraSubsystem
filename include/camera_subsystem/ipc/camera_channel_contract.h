/**
 * @file camera_channel_contract.h
 * @brief 发布端/订阅端通信协定（控制面 + 数据面）基础定义
 * @author CameraSubsystem Team
 * @date 2026-03-01
 */

#ifndef CAMERA_SUBSYSTEM_IPC_CAMERA_CHANNEL_CONTRACT_H
#define CAMERA_SUBSYSTEM_IPC_CAMERA_CHANNEL_CONTRACT_H

#include <cstdint>
#include <cstring>

#ifndef CAMERA_SUBSYSTEM_DEFAULT_CAMERA
#define CAMERA_SUBSYSTEM_DEFAULT_CAMERA "/dev/video0"
#endif

namespace camera_subsystem
{
namespace ipc
{

constexpr uint32_t kCameraDevicePathMaxLength = 128;
constexpr uint32_t kCameraClientNameMaxLength = 64;

enum class CameraBusType : uint32_t
{
    kDefault = 0,
    kMipi = 1,
    kUsb = 2,
    kVirtual = 3,
    kPlatformPrivate = 255
};

enum class CameraClientRole : uint32_t
{
    kCorePublisher = 0,
    kSubPublisher = 1,
    kSubscriber = 2
};

/**
 * @brief Camera 路由端点描述（POD，便于跨模块/跨进程序列化）
 */
struct CameraEndpoint
{
    uint32_t camera_id;
    CameraBusType bus_type;
    uint32_t bus_index;
    char device_path[kCameraDevicePathMaxLength];
    uint8_t reserved[32];
};

/**
 * @brief 控制面订阅/退订请求（最小定义）
 */
struct CameraSubscriptionRequest
{
    CameraClientRole role;
    CameraEndpoint endpoint;
    char client_name[kCameraClientNameMaxLength];
    uint8_t reserved[32];
};

inline CameraEndpoint MakeDefaultCameraEndpoint(uint32_t camera_id = 0)
{
    CameraEndpoint endpoint;
    endpoint.camera_id = camera_id;
    endpoint.bus_type = CameraBusType::kDefault;
    endpoint.bus_index = 0;
    std::memset(endpoint.device_path, 0, sizeof(endpoint.device_path));
    std::strncpy(endpoint.device_path, CAMERA_SUBSYSTEM_DEFAULT_CAMERA,
                 sizeof(endpoint.device_path) - 1);
    std::memset(endpoint.reserved, 0, sizeof(endpoint.reserved));
    return endpoint;
}

inline CameraEndpoint MakeCameraEndpoint(uint32_t camera_id,
                                         CameraBusType bus_type,
                                         uint32_t bus_index,
                                         const char* device_path)
{
    CameraEndpoint endpoint;
    endpoint.camera_id = camera_id;
    endpoint.bus_type = bus_type;
    endpoint.bus_index = bus_index;
    std::memset(endpoint.device_path, 0, sizeof(endpoint.device_path));

    if (device_path && device_path[0] != '\0')
    {
        std::strncpy(endpoint.device_path, device_path, sizeof(endpoint.device_path) - 1);
    }
    else
    {
        std::strncpy(endpoint.device_path, CAMERA_SUBSYSTEM_DEFAULT_CAMERA,
                     sizeof(endpoint.device_path) - 1);
    }

    std::memset(endpoint.reserved, 0, sizeof(endpoint.reserved));
    return endpoint;
}

inline bool IsEndpointValid(const CameraEndpoint& endpoint)
{
    return endpoint.device_path[0] != '\0';
}

} // namespace ipc
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_IPC_CAMERA_CHANNEL_CONTRACT_H
