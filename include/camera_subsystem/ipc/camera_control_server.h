/**
 * @file camera_control_server.h
 * @brief Camera 控制面服务端（核心发布端进程）
 * @author CameraSubsystem Team
 * @date 2026-03-01
 */

#ifndef CAMERA_SUBSYSTEM_IPC_CAMERA_CONTROL_SERVER_H
#define CAMERA_SUBSYSTEM_IPC_CAMERA_CONTROL_SERVER_H

#include "camera_subsystem/camera/camera_session_manager.h"
#include "camera_subsystem/ipc/camera_control_ipc.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace camera_subsystem
{
namespace ipc
{

/**
 * @brief 控制面服务端
 *
 * 核心发布端进程持有该服务端，通过 Unix Domain Socket 接收
 * 子发布端/订阅端的订阅控制请求，并驱动 CameraSessionManager。
 */
class CameraControlServer
{
public:
    explicit CameraControlServer(camera::CameraSessionManager* session_manager);
    ~CameraControlServer();

    bool Start(const std::string& socket_path = kDefaultCameraControlSocketPath);
    void Stop();
    bool IsRunning() const;
    std::string GetSocketPath() const;
    int GetLastErrorNo() const;
    std::string GetLastErrorStage() const;
    std::string GetLastErrorMessage() const;

private:
    struct ClientSubscription
    {
        std::string client_id;
        CameraEndpoint endpoint;
    };

    void AcceptLoop();
    void ClientLoop(int client_fd);
    void CleanupClientSubscriptions(int client_fd);

    CameraControlResponse ProcessRequest(int client_fd, const CameraControlRequest& request);

    bool AddClientSubscriptionLocked(int client_fd,
                                     const std::string& client_id,
                                     const CameraEndpoint& endpoint);
    void RemoveClientSubscriptionLocked(int client_fd,
                                        const std::string& client_id,
                                        const CameraEndpoint& endpoint);

    static bool EndpointEquals(const CameraEndpoint& lhs, const CameraEndpoint& rhs);
    static bool ReadFull(int fd, void* buffer, size_t length);
    static bool WriteFull(int fd, const void* buffer, size_t length);
    void SetLastError(const std::string& stage, int error_no, const std::string& message);

    camera::CameraSessionManager* session_manager_;
    int server_fd_;
    std::string socket_path_;
    std::atomic<bool> is_running_;
    std::thread accept_thread_;

    mutable std::mutex clients_mutex_;
    std::unordered_set<int> client_fds_;
    std::unordered_map<int, std::vector<ClientSubscription>> client_subscriptions_;
    std::vector<std::thread> client_threads_;

    mutable std::mutex error_mutex_;
    int last_error_no_;
    std::string last_error_stage_;
    std::string last_error_message_;
};

} // namespace ipc
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_IPC_CAMERA_CONTROL_SERVER_H
