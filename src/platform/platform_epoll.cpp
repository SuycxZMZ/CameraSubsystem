#include "camera_subsystem/platform/platform_epoll.h"

#include <unistd.h>

namespace camera_subsystem {
namespace platform {

PlatformEpoll::PlatformEpoll()
    : epoll_fd_(-1)
{
}

PlatformEpoll::~PlatformEpoll()
{
    Close();
}

bool PlatformEpoll::Create()
{
    if (epoll_fd_ != -1)
    {
        return true;
    }

    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    return epoll_fd_ != -1;
}

bool PlatformEpoll::Add(int fd, uint32_t events, uint64_t data)
{
    if (epoll_fd_ == -1 || fd < 0)
    {
        return false;
    }

    struct epoll_event event {};
    event.events = events;
    event.data.u64 = data;

    return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == 0;
}

bool PlatformEpoll::Modify(int fd, uint32_t events)
{
    if (epoll_fd_ == -1 || fd < 0)
    {
        return false;
    }

    struct epoll_event event {};
    event.events = events;
    event.data.u64 = static_cast<uint64_t>(fd);

    return epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, fd, &event) == 0;
}

bool PlatformEpoll::Remove(int fd)
{
    if (epoll_fd_ == -1 || fd < 0)
    {
        return false;
    }

    return epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr) == 0;
}

int PlatformEpoll::Wait(int timeout_ms, struct epoll_event* events, int max_events)
{
    if (epoll_fd_ == -1 || events == nullptr || max_events <= 0)
    {
        return -1;
    }

    return epoll_wait(epoll_fd_, events, max_events, timeout_ms);
}

void PlatformEpoll::Close()
{
    if (epoll_fd_ != -1)
    {
        close(epoll_fd_);
        epoll_fd_ = -1;
    }
}

bool PlatformEpoll::IsCreated() const
{
    return epoll_fd_ != -1;
}

int PlatformEpoll::GetFd() const
{
    return epoll_fd_;
}

} // namespace platform
} // namespace camera_subsystem

