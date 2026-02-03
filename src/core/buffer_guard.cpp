/**
 * @file buffer_guard.cpp
 * @brief BufferGuard 实现
 * @author CameraSubsystem Team
 * @date 2026-02-03
 */

#include "camera_subsystem/core/buffer_guard.h"

namespace camera_subsystem {
namespace core {

BufferGuard::BufferGuard()
    : pool_(nullptr)
    , block_(nullptr)
    , id_(0)
    , released_(true)
{
}

BufferGuard::BufferGuard(BufferPool* pool, uint32_t id, BufferBlock* block)
    : pool_(pool)
    , block_(block)
    , id_(id)
    , released_(false)
{
}

BufferGuard::~BufferGuard()
{
    Release();
}

BufferGuard::BufferGuard(BufferGuard&& other) noexcept
    : pool_(other.pool_)
    , block_(other.block_)
    , id_(other.id_)
    , released_(other.released_)
{
    other.pool_ = nullptr;
    other.block_ = nullptr;
    other.released_ = true;
}

BufferGuard& BufferGuard::operator=(BufferGuard&& other) noexcept
{
    if (this != &other)
    {
        Release();

        pool_ = other.pool_;
        block_ = other.block_;
        id_ = other.id_;
        released_ = other.released_;

        other.pool_ = nullptr;
        other.block_ = nullptr;
        other.released_ = true;
    }
    return *this;
}

bool BufferGuard::IsValid() const
{
    return pool_ != nullptr && block_ != nullptr && !released_;
}

uint32_t BufferGuard::Id() const
{
    return id_;
}

uint8_t* BufferGuard::Data() const
{
    return block_ ? block_->data : nullptr;
}

size_t BufferGuard::Size() const
{
    return block_ ? block_->size : 0;
}

void BufferGuard::MarkInFlight()
{
    if (pool_ && !released_)
    {
        pool_->MarkInFlight(id_);
    }
}

void BufferGuard::Release()
{
    if (!released_ && pool_)
    {
        pool_->ReleaseInternal(id_);
    }
    released_ = true;
    pool_ = nullptr;
    block_ = nullptr;
}

} // namespace core
} // namespace camera_subsystem
