#include "camera_subsystem/platform/platform_thread.h"

#include <utility>

namespace camera_subsystem {
namespace platform {

PlatformThread::PlatformThread(const std::string& name, ThreadFunc func)
    : thread_name_(name)
    , thread_func_(std::move(func))
    , native_thread_(nullptr)
    , is_running_(false)
    , should_stop_(false)
    , is_detached_(false)
{
}

PlatformThread::~PlatformThread()
{
    if (native_thread_ && native_thread_->joinable() && !is_detached_)
    {
        Join();
    }
}

bool PlatformThread::Start()
{
    if (is_running_ || !thread_func_)
    {
        return false;
    }

    should_stop_ = false;
    is_running_ = true;
    is_detached_ = false;

    native_thread_ = std::make_unique<std::thread>(&PlatformThread::ThreadEntry, this);
    return true;
}

void PlatformThread::Join()
{
    if (native_thread_ && native_thread_->joinable() && !is_detached_)
    {
        native_thread_->join();
    }
    is_running_ = false;
}

void PlatformThread::Detach()
{
    if (native_thread_ && native_thread_->joinable())
    {
        native_thread_->detach();
        is_detached_ = true;
    }
}

bool PlatformThread::IsRunning() const
{
    return is_running_;
}

std::thread::id PlatformThread::GetThreadId() const
{
    if (!native_thread_)
    {
        return std::thread::id{};
    }
    return native_thread_->get_id();
}

const std::string& PlatformThread::GetThreadName() const
{
    return thread_name_;
}

bool PlatformThread::SetPriority(int /*priority*/)
{
    // 最小实现: 当前版本不直接操作系统优先级。
    return false;
}

bool PlatformThread::SetCpuAffinity(const std::vector<int>& /*cpu_ids*/)
{
    // 最小实现: 当前版本不直接设置 CPU 亲和性。
    return false;
}

void PlatformThread::RequestStop()
{
    should_stop_ = true;
}

bool PlatformThread::IsStopRequested() const
{
    return should_stop_;
}

void PlatformThread::ThreadEntry()
{
    try
    {
        thread_func_();
    }
    catch (...)
    {
        // 最小实现: 吃掉异常，避免跨线程传播导致 std::terminate。
    }

    is_running_ = false;
}

} // namespace platform
} // namespace camera_subsystem

