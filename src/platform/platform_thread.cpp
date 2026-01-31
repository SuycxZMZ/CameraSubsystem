/**
 * @file platform_thread.cpp
 * @brief 平台线程封装实现
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#include "camera_subsystem/platform/platform_thread.h"
#include <cstring>
#include <memory>
#include <sched.h>
#include <sys/sysinfo.h>
#include <unistd.h>

namespace camera_subsystem
{
namespace platform
{

PlatformThread::PlatformThread(const std::string& name, ThreadFunc func)
    : thread_name_(name), thread_func_(func), native_thread_(), is_running_(false),
      should_stop_(false), is_detached_(false)
{
}

PlatformThread::~PlatformThread()
{
    if (!is_detached_)
    {
        should_stop_ = true;
        Join();
    }
}

bool PlatformThread::Start()
{
    if (is_running_)
    {
        return false;
    }

    should_stop_ = false;
    is_running_ = true;
    is_detached_ = false;

    try
    {
        native_thread_ = std::make_unique<std::thread>(&PlatformThread::ThreadEntry, this);
        return true;
    }
    catch (const std::exception& e)
    {
        is_running_ = false;
        return false;
    }
}

void PlatformThread::Join()
{
    if (is_detached_)
    {
        return;
    }

    if (native_thread_ && native_thread_->joinable())
    {
        native_thread_->join();
    }

    is_running_ = false;
}

void PlatformThread::Detach()
{
    if (is_detached_)
    {
        return;
    }

    if (native_thread_ && native_thread_->joinable())
    {
        native_thread_->detach();
    }

    is_detached_ = true;
}

bool PlatformThread::IsRunning() const
{
    return is_running_.load();
}

std::thread::id PlatformThread::GetThreadId() const
{
    if (!is_running_)
    {
        return std::thread::id();
    }

    if (!native_thread_)
    {
        return std::thread::id();
    }

    return native_thread_->get_id();
}

const std::string& PlatformThread::GetThreadName() const
{
    return thread_name_;
}

bool PlatformThread::SetPriority(int priority)
{
    if (!is_running_ || !native_thread_)
    {
        return false;
    }

#ifdef __linux__
    // 设置线程优先级
    sched_param param;
    memset(&param, 0, sizeof(param));
    param.sched_priority = priority;

    // 获取当前调度策略
    int policy = sched_getscheduler(0);

    // 如果是 SCHED_OTHER，需要切换到 SCHED_RR 或 SCHED_FIFO
    if (policy == SCHED_OTHER)
    {
        policy = SCHED_RR;
    }

    int ret = pthread_setschedparam(native_thread_->native_handle(), policy, &param);
    return ret == 0;
#else
    return false;
#endif
}

bool PlatformThread::SetCpuAffinity(const std::vector<int>& cpu_ids)
{
    if (!is_running_ || !native_thread_ || cpu_ids.empty())
    {
        return false;
    }

#ifdef __linux__
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);

    for (int cpu_id : cpu_ids)
    {
        if (cpu_id >= 0 && cpu_id < sysconf(_SC_NPROCESSORS_ONLN))
        {
            CPU_SET(cpu_id, &cpuset);
        }
    }

    int ret = pthread_setaffinity_np(native_thread_->native_handle(), sizeof(cpu_set_t), &cpuset);
    return ret == 0;
#else
    return false;
#endif
}

void PlatformThread::RequestStop()
{
    should_stop_ = true;
}

bool PlatformThread::IsStopRequested() const
{
    return should_stop_.load();
}

void PlatformThread::ThreadEntry()
{
    // 设置线程名称（仅前16个字符有效）
    if (!thread_name_.empty())
    {
#ifdef __linux__
        pthread_setname_np(pthread_self(), thread_name_.substr(0, 16).c_str());
#endif
    }

    // 执行线程函数
    if (thread_func_)
    {
        thread_func_();
    }

    is_running_ = false;
}

} // namespace platform
} // namespace camera_subsystem
