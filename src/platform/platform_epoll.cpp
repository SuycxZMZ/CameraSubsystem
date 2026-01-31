/**
 * @file platform_epoll.cpp
 * @brief 平台 Epoll 封装实现
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#include "camera_subsystem/platform/platform_epoll.h"
#include <cstring>
#include <unistd.h>

namespace camera_subsystem
{
namespace platform
{

PlatformEpoll::PlatformEpoll() : epoll_fd_(-1)
{
}

PlatformEpoll::~PlatformEpoll()
{
    Close();
}

bool PlatformEpoll::Create()
{
    if (epoll_fd_ >= 0)
    {
        return false;
    }

    epoll_fd_ = epoll_create1(0);
    return epoll_fd_ >= 0;
}

bool PlatformEpoll::Add(int fd, uint32_t events, uint64_t data)
{
    if (epoll_fd_ < 0 || fd < 0)
    {
        return false;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;
    ev.data.u64 = data;

    int ret = epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &ev);
    return ret == 0;
}

bool PlatformEpoll::Modify(int fd, uint32_t events)
{
    if (epoll_fd_ < 0 || fd < 0)
    {
        return false;
    }

    struct epoll_event ev;
    memset(&ev, 0, sizeof(ev));
    ev.events = events;

    int ret = epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &ev);
    return ret == 0;
}

bool PlatformEpoll::Remove(int fd)
{
    if (epoll_fd_ < 0 || fd < 0)
    {
        return false;
    }

    int ret = epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    return ret == 0;
}

int PlatformEpoll::Wait(int timeout_ms, struct epoll_event* events, int max_events)
{
    if (epoll_fd_ < 0 || !events || max_events <= 0)
    {
        return -1;
    }

    int ret = epoll_wait(epoll_fd_, events, max_events, timeout_ms);
    return ret;
}

void PlatformEpoll::Close()
{
    if (epoll_fd_ >= 0)
    {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool PlatformEpoll::IsCreated() const
{
    return epoll_fd_ >= 0;
}

int PlatformEpoll::GetFd() const
{
    return epoll_fd_;
}

} // namespace platform
} // namespace camera_subsystem
