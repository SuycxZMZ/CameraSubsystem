#include "camera_subsystem/core/dma_buf_sync.h"

#include <linux/dma-buf.h>
#include <sys/ioctl.h>

namespace camera_subsystem
{
namespace core
{

namespace
{

unsigned long long ToSyncDirection(DmaBufSyncDirection direction)
{
    switch (direction)
    {
        case DmaBufSyncDirection::kRead:
            return DMA_BUF_SYNC_READ;
        case DmaBufSyncDirection::kWrite:
            return DMA_BUF_SYNC_WRITE;
        case DmaBufSyncDirection::kReadWrite:
            return DMA_BUF_SYNC_RW;
    }
    return DMA_BUF_SYNC_RW;
}

} // namespace

bool DmaBufSyncHelper::StartCpuAccess(int fd, DmaBufSyncDirection direction)
{
    return Sync(fd, DMA_BUF_SYNC_START, direction);
}

bool DmaBufSyncHelper::EndCpuAccess(int fd, DmaBufSyncDirection direction)
{
    return Sync(fd, DMA_BUF_SYNC_END, direction);
}

bool DmaBufSyncHelper::Sync(int fd, unsigned long long phase, DmaBufSyncDirection direction)
{
    if (fd < 0)
    {
        return false;
    }

    struct dma_buf_sync sync;
    sync.flags = phase | ToSyncDirection(direction);
    return ioctl(fd, DMA_BUF_IOCTL_SYNC, &sync) == 0;
}

} // namespace core
} // namespace camera_subsystem
