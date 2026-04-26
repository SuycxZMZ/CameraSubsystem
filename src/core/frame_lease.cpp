#include "camera_subsystem/core/frame_lease.h"

#include <utility>

namespace camera_subsystem {
namespace core {

HeapFrameLease::HeapFrameLease(std::shared_ptr<BufferGuard> guard)
    : guard_(std::move(guard))
{
}

HeapFrameLease::~HeapFrameLease()
{
    Release();
}

void HeapFrameLease::Release()
{
    bool expected = false;
    if (released_.compare_exchange_strong(expected, true))
    {
        guard_.reset();
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

DmaBufFrameLease::DmaBufFrameLease(uint32_t buffer_id, ReleaseCallback release_callback)
    : buffer_id_(buffer_id)
    , release_callback_(std::move(release_callback))
{
}

DmaBufFrameLease::~DmaBufFrameLease()
{
    Release();
}

void DmaBufFrameLease::Release()
{
    bool expected = false;
    if (released_.compare_exchange_strong(expected, true) && release_callback_)
    {
        release_callback_(buffer_id_);
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
