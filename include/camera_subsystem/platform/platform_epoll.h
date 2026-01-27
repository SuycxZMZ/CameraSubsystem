/**
 * @file platform_epoll.h
 * @brief 平台 Epoll 封装
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#ifndef CAMERA_SUBSYSTEM_PLATFORM_PLATFORM_EPOLL_H
#define CAMERA_SUBSYSTEM_PLATFORM_PLATFORM_EPOLL_H

#include <cstdint>
#include <sys/epoll.h>

namespace camera_subsystem {
namespace platform {

/**
 * @brief 平台 Epoll 类
 *
 * 封装 Epoll 操作,提供统一的事件监听接口。
 * 支持添加、修改、删除文件描述符,以及等待事件。
 */
class PlatformEpoll
{
public:
    /**
     * @brief 最大事件数量
     */
    static const int kMaxEvents = 32;

    /**
     * @brief 构造函数
     */
    PlatformEpoll();

    /**
     * @brief 析构函数
     *
     * @note 会自动调用 Close()
     */
    ~PlatformEpoll();

    /**
     * @brief 创建 Epoll 实例
     * @return 成功返回 true,失败返回 false
     *
     * @note 该函数不是线程安全的,不能在多线程中同时调用
     */
    bool Create();

    /**
     * @brief 添加文件描述符到 Epoll
     * @param fd 文件描述符
     * @param events 事件类型 (EPOLLIN, EPOLLOUT, EPOLLPRI 等)
     * @param data 用户数据 (通常是文件描述符或指针)
     * @return 成功返回 true,失败返回 false
     *
     * @note 该函数是线程安全的
     */
    bool Add(int fd, uint32_t events, uint64_t data);

    /**
     * @brief 修改文件描述符的事件
     * @param fd 文件描述符
     * @param events 事件类型
     * @return 成功返回 true,失败返回 false
     *
     * @note 该函数是线程安全的
     */
    bool Modify(int fd, uint32_t events);

    /**
     * @brief 从 Epoll 移除文件描述符
     * @param fd 文件描述符
     * @return 成功返回 true,失败返回 false
     *
     * @note 该函数是线程安全的
     */
    bool Remove(int fd);

    /**
     * @brief 等待事件
     * @param timeout_ms 超时时间 (毫秒),-1 表示无限等待
     * @param events 输出事件数组
     * @param max_events 最大事件数
     * @return 返回的事件数量,失败返回 -1
     *
     * @note 该函数是线程安全的
     * @note 调用者需要确保 events 数组有足够的空间
     */
    int Wait(int timeout_ms, struct epoll_event* events, int max_events = kMaxEvents);

    /**
     * @brief 关闭 Epoll 实例
     *
     * @note 该函数是线程安全的
     * @note 关闭后不能再使用该实例
     */
    void Close();

    /**
     * @brief 检查 Epoll 实例是否已创建
     * @return true 表示已创建,false 表示未创建
     *
     * @note 该函数是线程安全的
     */
    bool IsCreated() const;

    /**
     * @brief 获取 Epoll 文件描述符
     * @return Epoll 文件描述符,未创建返回 -1
     *
     * @note 该函数是线程安全的
     */
    int GetFd() const;

private:
    int epoll_fd_;
};

} // namespace platform
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_PLATFORM_PLATFORM_EPOLL_H
