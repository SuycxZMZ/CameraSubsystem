/**
 * @file buffer_pool.h
 * @brief BufferPool 统一管理 Buffer 生命周期与复用
 * @author CameraSubsystem Team
 * @date 2026-02-02
 */

#ifndef CAMERA_SUBSYSTEM_CORE_BUFFER_POOL_H
#define CAMERA_SUBSYSTEM_CORE_BUFFER_POOL_H

#include "camera_subsystem/core/buffer_state.h"

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>
#include <queue>
#include <vector>

namespace camera_subsystem {
namespace core {

/**
 * @brief Buffer 块描述
 */
struct BufferBlock
{
    uint32_t id = 0;
    uint8_t* data = nullptr;
    size_t size = 0;
};

class BufferGuard;

/**
 * @brief BufferPool 统一管理 Buffer 生命周期与复用
 *
 * 注意：BufferPool 必须比获取到的 BufferGuard 生命周期更长。
 */
class BufferPool
{
public:
    struct Stats
    {
        size_t total = 0;
        size_t available = 0;
        size_t in_use = 0;
        size_t in_flight = 0;
        size_t max_in_use = 0;
        size_t max_in_flight = 0;
        uint64_t acquire_count = 0;
        uint64_t release_count = 0;
        uint64_t acquire_fail = 0;
    };

    BufferPool();
    ~BufferPool();

    /**
     * @brief 初始化 BufferPool
     * @param buffer_count Buffer 数量
     * @param buffer_size 每个 Buffer 的大小
     * @return 成功返回 true
     */
    bool Initialize(size_t buffer_count, size_t buffer_size);

    /**
     * @brief 获取一个 Buffer
     * @return shared_ptr 持有 BufferGuard, 释放时自动归还到池中
     */
    std::shared_ptr<BufferGuard> Acquire();

    /**
     * @brief 清空并释放所有 Buffer
     */
    void Clear();

    /**
     * @brief 获取统计信息
     */
    Stats GetStats() const;

    /**
     * @brief 泄漏检测（ARCH-003）
     * @return 泄漏的 Buffer ID 列表
     */
    std::vector<uint32_t> CheckLeaks() const;

    size_t GetBufferCount() const;
    size_t GetBufferSize() const;

private:
    friend class BufferGuard;
    void ReleaseInternal(uint32_t buffer_id);
    void MarkInFlight(uint32_t buffer_id);
    std::vector<uint32_t> CollectLeaksLocked() const;

    struct BufferEntry
    {
        BufferBlock block;
        std::unique_ptr<uint8_t[]> storage;
        BufferState state = BufferState::kFree;
    };

    mutable std::mutex mutex_;
    std::vector<BufferEntry> buffers_;
    std::queue<uint32_t> free_ids_;
    size_t buffer_size_;
    bool initialized_;

    Stats stats_;
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_BUFFER_POOL_H
