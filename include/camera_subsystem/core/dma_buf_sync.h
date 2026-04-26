/**
 * @file dma_buf_sync.h
 * @brief DMA-BUF CPU access synchronization helper
 */

#ifndef CAMERA_SUBSYSTEM_CORE_DMA_BUF_SYNC_H
#define CAMERA_SUBSYSTEM_CORE_DMA_BUF_SYNC_H

namespace camera_subsystem
{
namespace core
{

enum class DmaBufSyncDirection
{
    kRead,
    kWrite,
    kReadWrite
};

class DmaBufSyncHelper
{
public:
    static bool StartCpuAccess(int fd, DmaBufSyncDirection direction);
    static bool EndCpuAccess(int fd, DmaBufSyncDirection direction);

private:
    static bool Sync(int fd, unsigned long long phase, DmaBufSyncDirection direction);
};

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_DMA_BUF_SYNC_H
