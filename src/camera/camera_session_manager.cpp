/**
 * @file camera_session_manager.cpp
 * @brief Camera 会话管理器实现
 * @author CameraSubsystem Team
 * @date 2026-03-01
 */

#include "camera_subsystem/camera/camera_session_manager.h"

#include "camera_subsystem/platform/platform_logger.h"

#include <exception>
#include <functional>

namespace camera_subsystem
{
namespace camera
{

CameraSessionManager::CameraSessionManager(SessionStartCallback start_callback,
                                           SessionStopCallback stop_callback)
    : core_publisher_id_()
    , start_callback_(std::move(start_callback))
    , stop_callback_(std::move(stop_callback))
    , sessions_()
{
}

bool CameraSessionManager::RegisterCorePublisher(const std::string& core_publisher_id)
{
    if (core_publisher_id.empty())
    {
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);
    if (core_publisher_id_.empty())
    {
        core_publisher_id_ = core_publisher_id;
        return true;
    }

    return core_publisher_id_ == core_publisher_id;
}

bool CameraSessionManager::UnregisterCorePublisher(const std::string& core_publisher_id)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (core_publisher_id_.empty() || core_publisher_id_ != core_publisher_id)
    {
        return false;
    }

    if (!sessions_.empty())
    {
        platform::PlatformLogger::Log(core::LogLevel::kWarning,
                                      "camera_session_manager",
                                      "Unregister core publisher rejected, active sessions=%zu",
                                      sessions_.size());
        return false;
    }

    core_publisher_id_.clear();
    return true;
}

bool CameraSessionManager::IsCorePublisherRegistered() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return !core_publisher_id_.empty();
}

std::string CameraSessionManager::GetCorePublisherId() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return core_publisher_id_;
}

bool CameraSessionManager::Subscribe(const std::string& client_id,
                                     ipc::CameraClientRole role,
                                     const ipc::CameraEndpoint& endpoint)
{
    if (client_id.empty() || !IsRoleSubscribable(role))
    {
        return false;
    }

    const ipc::CameraEndpoint normalized = NormalizeEndpoint(endpoint);
    const EndpointKey key = BuildEndpointKey(normalized);

    std::lock_guard<std::mutex> lock(mutex_);

    if (core_publisher_id_.empty())
    {
        platform::PlatformLogger::Log(core::LogLevel::kWarning,
                                      "camera_session_manager",
                                      "Subscribe rejected: core publisher not registered");
        return false;
    }

    auto it = sessions_.find(key);
    if (it == sessions_.end())
    {
        SessionRecord record;
        record.endpoint = normalized;
        record.members.clear();
        record.sub_publisher_count = 0;
        record.subscriber_count = 0;
        record.is_streaming = false;

        it = sessions_.emplace(key, std::move(record)).first;
    }

    SessionRecord& session = it->second;

    auto member_it = session.members.find(client_id);
    if (member_it != session.members.end())
    {
        if (member_it->second == role)
        {
            return true;
        }

        DecrementRoleCount(&session, member_it->second);
        member_it->second = role;
        IncrementRoleCount(&session, role);
        return true;
    }

    if (!session.is_streaming)
    {
        bool start_ok = true;
        if (start_callback_)
        {
            try
            {
                start_ok = start_callback_(session.endpoint);
            }
            catch (const std::exception& e)
            {
                start_ok = false;
                platform::PlatformLogger::Log(core::LogLevel::kError,
                                              "camera_session_manager",
                                              "Start callback exception: %s",
                                              e.what());
            }
            catch (...)
            {
                start_ok = false;
                platform::PlatformLogger::Log(core::LogLevel::kError,
                                              "camera_session_manager",
                                              "Start callback exception: unknown");
            }
        }

        if (!start_ok)
        {
            if (session.members.empty())
            {
                sessions_.erase(it);
            }
            platform::PlatformLogger::Log(core::LogLevel::kError,
                                          "camera_session_manager",
                                          "Start callback failed for camera_id=%u path=%s",
                                          session.endpoint.camera_id,
                                          session.endpoint.device_path);
            return false;
        }
        session.is_streaming = true;
    }

    session.members.emplace(client_id, role);
    IncrementRoleCount(&session, role);
    return true;
}

bool CameraSessionManager::Unsubscribe(const std::string& client_id,
                                       const ipc::CameraEndpoint& endpoint)
{
    if (client_id.empty())
    {
        return false;
    }

    const ipc::CameraEndpoint normalized = NormalizeEndpoint(endpoint);
    const EndpointKey key = BuildEndpointKey(normalized);

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(key);
    if (it == sessions_.end())
    {
        return false;
    }

    SessionRecord& session = it->second;
    auto member_it = session.members.find(client_id);
    if (member_it == session.members.end())
    {
        return false;
    }

    DecrementRoleCount(&session, member_it->second);
    session.members.erase(member_it);

    if (!session.members.empty())
    {
        return true;
    }

    if (session.is_streaming && stop_callback_)
    {
        try
        {
            stop_callback_(session.endpoint);
        }
        catch (const std::exception& e)
        {
            platform::PlatformLogger::Log(core::LogLevel::kError,
                                          "camera_session_manager",
                                          "Stop callback exception: %s",
                                          e.what());
        }
        catch (...)
        {
            platform::PlatformLogger::Log(core::LogLevel::kError,
                                          "camera_session_manager",
                                          "Stop callback exception: unknown");
        }
    }

    sessions_.erase(it);
    return true;
}

uint32_t CameraSessionManager::GetSubscriberCount(const ipc::CameraEndpoint& endpoint) const
{
    const EndpointKey key = BuildEndpointKey(NormalizeEndpoint(endpoint));

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(key);
    if (it == sessions_.end())
    {
        return 0;
    }

    const SessionRecord& session = it->second;
    return static_cast<uint32_t>(session.members.size());
}

bool CameraSessionManager::HasActiveSession(const ipc::CameraEndpoint& endpoint) const
{
    const EndpointKey key = BuildEndpointKey(NormalizeEndpoint(endpoint));

    std::lock_guard<std::mutex> lock(mutex_);
    auto it = sessions_.find(key);
    if (it == sessions_.end())
    {
        return false;
    }

    return it->second.is_streaming;
}

std::vector<CameraSessionManager::SessionSnapshot> CameraSessionManager::ListSessions() const
{
    std::vector<SessionSnapshot> snapshots;

    std::lock_guard<std::mutex> lock(mutex_);
    snapshots.reserve(sessions_.size());

    for (const auto& item : sessions_)
    {
        const SessionRecord& session = item.second;

        SessionSnapshot snapshot;
        snapshot.endpoint = session.endpoint;
        snapshot.sub_publisher_count = session.sub_publisher_count;
        snapshot.subscriber_count = session.subscriber_count;
        snapshot.total_count = static_cast<uint32_t>(session.members.size());
        snapshot.is_streaming = session.is_streaming;
        snapshots.push_back(snapshot);
    }

    return snapshots;
}

bool CameraSessionManager::EndpointKey::operator==(const EndpointKey& other) const
{
    return camera_id == other.camera_id &&
           bus_type == other.bus_type &&
           bus_index == other.bus_index &&
           device_path == other.device_path;
}

size_t CameraSessionManager::EndpointKeyHasher::operator()(const EndpointKey& key) const
{
    size_t seed = 0;

    auto hash_combine = [&seed](size_t value)
    {
        seed ^= value + 0x9e3779b9 + (seed << 6) + (seed >> 2);
    };

    hash_combine(std::hash<uint32_t>{}(key.camera_id));
    hash_combine(std::hash<uint32_t>{}(key.bus_type));
    hash_combine(std::hash<uint32_t>{}(key.bus_index));
    hash_combine(std::hash<std::string>{}(key.device_path));
    return seed;
}

CameraSessionManager::EndpointKey
CameraSessionManager::BuildEndpointKey(const ipc::CameraEndpoint& endpoint)
{
    EndpointKey key;
    key.camera_id = endpoint.camera_id;
    key.bus_type = static_cast<uint32_t>(endpoint.bus_type);
    key.bus_index = endpoint.bus_index;
    key.device_path = endpoint.device_path;
    return key;
}

ipc::CameraEndpoint CameraSessionManager::NormalizeEndpoint(const ipc::CameraEndpoint& endpoint)
{
    ipc::CameraEndpoint normalized = endpoint;
    normalized.device_path[sizeof(normalized.device_path) - 1] = '\0';

    if (normalized.device_path[0] == '\0')
    {
        normalized = ipc::MakeCameraEndpoint(endpoint.camera_id,
                                             endpoint.bus_type,
                                             endpoint.bus_index,
                                             CAMERA_SUBSYSTEM_DEFAULT_CAMERA);
    }

    return normalized;
}

bool CameraSessionManager::IsRoleSubscribable(ipc::CameraClientRole role)
{
    return role == ipc::CameraClientRole::kSubPublisher ||
           role == ipc::CameraClientRole::kSubscriber;
}

void CameraSessionManager::IncrementRoleCount(SessionRecord* session, ipc::CameraClientRole role)
{
    if (!session)
    {
        return;
    }

    if (role == ipc::CameraClientRole::kSubPublisher)
    {
        ++session->sub_publisher_count;
    }
    else if (role == ipc::CameraClientRole::kSubscriber)
    {
        ++session->subscriber_count;
    }
}

void CameraSessionManager::DecrementRoleCount(SessionRecord* session, ipc::CameraClientRole role)
{
    if (!session)
    {
        return;
    }

    if (role == ipc::CameraClientRole::kSubPublisher)
    {
        if (session->sub_publisher_count > 0)
        {
            --session->sub_publisher_count;
        }
    }
    else if (role == ipc::CameraClientRole::kSubscriber)
    {
        if (session->subscriber_count > 0)
        {
            --session->subscriber_count;
        }
    }
}

} // namespace camera
} // namespace camera_subsystem
