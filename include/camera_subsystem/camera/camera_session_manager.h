/**
 * @file camera_session_manager.h
 * @brief Camera 会话管理器（核心发布端独占设备 + 按订阅启停）
 * @author CameraSubsystem Team
 * @date 2026-03-01
 */

#ifndef CAMERA_SUBSYSTEM_CAMERA_CAMERA_SESSION_MANAGER_H
#define CAMERA_SUBSYSTEM_CAMERA_CAMERA_SESSION_MANAGER_H

#include "camera_subsystem/ipc/camera_channel_contract.h"

#include <cstdint>
#include <functional>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

namespace camera_subsystem
{
namespace camera
{

class CameraSessionManager
{
public:
    using SessionStartCallback = std::function<bool(const ipc::CameraEndpoint&)>;
    using SessionStopCallback = std::function<void(const ipc::CameraEndpoint&)>;

    struct SessionSnapshot
    {
        ipc::CameraEndpoint endpoint;
        uint32_t sub_publisher_count;
        uint32_t subscriber_count;
        uint32_t total_count;
        bool is_streaming;
    };

    CameraSessionManager(SessionStartCallback start_callback, SessionStopCallback stop_callback);

    bool RegisterCorePublisher(const std::string& core_publisher_id);
    bool UnregisterCorePublisher(const std::string& core_publisher_id);

    bool IsCorePublisherRegistered() const;
    std::string GetCorePublisherId() const;

    bool Subscribe(const std::string& client_id,
                   ipc::CameraClientRole role,
                   const ipc::CameraEndpoint& endpoint);

    bool Unsubscribe(const std::string& client_id, const ipc::CameraEndpoint& endpoint);

    uint32_t GetSubscriberCount(const ipc::CameraEndpoint& endpoint) const;
    bool HasActiveSession(const ipc::CameraEndpoint& endpoint) const;
    std::vector<SessionSnapshot> ListSessions() const;

private:
    struct EndpointKey
    {
        uint32_t camera_id;
        uint32_t bus_type;
        uint32_t bus_index;
        std::string device_path;

        bool operator==(const EndpointKey& other) const;
    };

    struct EndpointKeyHasher
    {
        size_t operator()(const EndpointKey& key) const;
    };

    struct SessionRecord
    {
        ipc::CameraEndpoint endpoint;
        std::unordered_map<std::string, ipc::CameraClientRole> members;
        uint32_t sub_publisher_count;
        uint32_t subscriber_count;
        bool is_streaming;
    };

    static EndpointKey BuildEndpointKey(const ipc::CameraEndpoint& endpoint);
    static ipc::CameraEndpoint NormalizeEndpoint(const ipc::CameraEndpoint& endpoint);
    static bool IsRoleSubscribable(ipc::CameraClientRole role);

    void IncrementRoleCount(SessionRecord* session, ipc::CameraClientRole role);
    void DecrementRoleCount(SessionRecord* session, ipc::CameraClientRole role);

    mutable std::mutex mutex_;
    std::string core_publisher_id_;
    SessionStartCallback start_callback_;
    SessionStopCallback stop_callback_;
    std::unordered_map<EndpointKey, SessionRecord, EndpointKeyHasher> sessions_;
};

} // namespace camera
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CAMERA_CAMERA_SESSION_MANAGER_H
