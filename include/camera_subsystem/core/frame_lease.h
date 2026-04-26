/**
 * @file frame_lease.h
 * @brief Frame lease abstractions for heap and DMA-BUF backed frames
 */

#ifndef CAMERA_SUBSYSTEM_CORE_FRAME_LEASE_H
#define CAMERA_SUBSYSTEM_CORE_FRAME_LEASE_H

#include "camera_subsystem/core/buffer_guard.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace camera_subsystem {
namespace core {

class FrameLease
{
public:
    virtual ~FrameLease() = default;

    FrameLease(const FrameLease&) = delete;
    FrameLease& operator=(const FrameLease&) = delete;

    virtual void Release() = 0;
    virtual bool IsReleased() const = 0;
    virtual uint32_t BufferId() const = 0;

protected:
    FrameLease() = default;
};

class HeapFrameLease final : public FrameLease
{
public:
    explicit HeapFrameLease(std::shared_ptr<BufferGuard> guard);
    ~HeapFrameLease() override;

    void Release() override;
    bool IsReleased() const override;
    uint32_t BufferId() const override;
    std::shared_ptr<BufferGuard> Guard() const;

private:
    std::shared_ptr<BufferGuard> guard_;
    std::atomic<bool> released_{false};
};

class DmaBufFrameLease final : public FrameLease
{
public:
    using ReleaseCallback = std::function<void(uint32_t buffer_id)>;

    DmaBufFrameLease(uint32_t buffer_id, ReleaseCallback release_callback);
    ~DmaBufFrameLease() override;

    void Release() override;
    bool IsReleased() const override;
    uint32_t BufferId() const override;

private:
    uint32_t buffer_id_ = 0;
    ReleaseCallback release_callback_;
    std::atomic<bool> released_{false};
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_FRAME_LEASE_H
