#include "camera_subsystem/core/frame_lease.h"

#include <utility>

namespace camera_subsystem {
namespace core {

// ---------------------------------------------------------------------------
// HeapFrameLease
// ---------------------------------------------------------------------------

HeapFrameLease::HeapFrameLease(std::shared_ptr<BufferGuard> guard)
    : guard_(std::move(guard))
{
}

HeapFrameLease::~HeapFrameLease()
{
    // 析构时自动 release，确保 BufferGuard 归还 BufferPool
    Release();
}

void HeapFrameLease::Release()
{
    // CAS 保证幂等：只有第一次调用才执行 reset
    bool expected = false;
    if (released_.compare_exchange_strong(expected, true))
    {
        guard_.reset(); // shared_ptr 析构，BufferGuard 归还 BufferPool
    }
}

bool HeapFrameLease::IsReleased() const
{
    return released_.load();
}

uint32_t HeapFrameLease::BufferId() const
{
    return guard_ ? guard_->Id() : 0;
}

std::shared_ptr<BufferGuard> HeapFrameLease::Guard() const
{
    return guard_;
}

// ---------------------------------------------------------------------------
// DmaBufFrameLease
// ---------------------------------------------------------------------------

DmaBufFrameLease::DmaBufFrameLease(uint32_t buffer_id, ReleaseCallback release_callback)
    : buffer_id_(buffer_id)
    , release_callback_(std::move(release_callback))
{
}

DmaBufFrameLease::~DmaBufFrameLease()
{
    // 析构时自动 release，触发 QBUF 回调
    Release();
}

void DmaBufFrameLease::Release()
{
    // CAS 保证幂等：只有第一次调用才触发 release callback
    // callback 内部通过 weak_ptr<RequeueContext> 检查 CameraSource 是否仍存活
    bool expected = false;
    if (released_.compare_exchange_strong(expected, true) && release_callback_)
    {
        release_callback_(buffer_id_); // 触发 CameraSource::RequeueBuffer
    }
}

bool DmaBufFrameLease::IsReleased() const
{
    return released_.load();
}

uint32_t DmaBufFrameLease::BufferId() const
{
    return buffer_id_;
}

} // namespace core
} // namespace camera_subsystem
