/**
 * @file buffer_guard.h
 * @brief BufferGuard 统一管理 Buffer 生命周期
 * @author CameraSubsystem Team
 * @date 2026-02-03
 */

#ifndef CAMERA_SUBSYSTEM_CORE_BUFFER_GUARD_H
#define CAMERA_SUBSYSTEM_CORE_BUFFER_GUARD_H

#include "camera_subsystem/core/buffer_pool.h"
#include "camera_subsystem/core/buffer_state.h"

#include <cstddef>
#include <cstdint>

namespace camera_subsystem {
namespace core {

/**
 * @brief BufferGuard（ARCH-001）
 *
 * 以 RAII 方式明确 Buffer 所有权，销毁时自动归还 BufferPool。
 */
class BufferGuard
{
public:
    BufferGuard();
    ~BufferGuard();

    BufferGuard(const BufferGuard&) = delete;
    BufferGuard& operator=(const BufferGuard&) = delete;

    BufferGuard(BufferGuard&& other) noexcept;
    BufferGuard& operator=(BufferGuard&& other) noexcept;

    bool IsValid() const;
    uint32_t Id() const;
    uint8_t* Data() const;
    size_t Size() const;

    /**
     * @brief 标记 Buffer 为 InFlight（ARCH-002）
     */
    void MarkInFlight();

private:
    friend class BufferPool;

    BufferGuard(BufferPool* pool, uint32_t id, BufferBlock* block);
    void Release();

    BufferPool* pool_;
    BufferBlock* block_;
    uint32_t id_;
    bool released_;
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_BUFFER_GUARD_H
