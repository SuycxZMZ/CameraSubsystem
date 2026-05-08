/**
 * @file dmabuf_smoke_test.cpp
 * @brief Minimal board-side DMA-BUF export and lease smoke test
 *
 * Usage:
 *   ./dmabuf_smoke_test [device_path] [seconds]
 */

#include "camera_subsystem/camera/camera_source.h"
#include "camera_subsystem/core/camera_config.h"
#include "camera_subsystem/core/dma_buf_sync.h"
#include "camera_subsystem/core/frame_descriptor.h"
#include "camera_subsystem/core/frame_lease.h"
#include "camera_subsystem/platform/platform_logger.h"

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <sys/mman.h>
#include <thread>

namespace
{

using camera_subsystem::camera::CameraSource;
using camera_subsystem::core::CameraConfig;
using camera_subsystem::core::DmaBufSyncDirection;
using camera_subsystem::core::DmaBufSyncHelper;
using camera_subsystem::core::FramePacket;
using camera_subsystem::core::IoMethod;
using camera_subsystem::core::PixelFormat;
using camera_subsystem::platform::PlatformLogger;

struct SmokeStats
{
    std::atomic<uint64_t> frames{0};
    std::atomic<uint64_t> mmap_ok{0};
    std::atomic<uint64_t> mmap_fail{0};
    std::atomic<uint64_t> sync_start_fail{0};
    std::atomic<uint64_t> sync_end_fail{0};
    std::atomic<uint64_t> checksum{0};
    std::atomic<size_t> max_active_leases{0};
};

uint64_t SampleChecksum(const uint8_t* data, size_t size)
{
    const size_t sample_size = std::min<size_t>(size, 4096);
    uint64_t checksum = 0;
    for (size_t i = 0; i < sample_size; ++i)
    {
        checksum = (checksum * 131U) + data[i];
    }
    return checksum;
}

bool IsMPlaneProbeOnly()
{
    const char* value = std::getenv("CAMERA_SUBSYSTEM_ENABLE_MPLANE_PROBE");
    if (value == nullptr)
    {
        return false;
    }

    return std::strcmp(value, "1") == 0 ||
           std::strcmp(value, "true") == 0 ||
           std::strcmp(value, "TRUE") == 0 ||
           std::strcmp(value, "on") == 0 ||
           std::strcmp(value, "ON") == 0;
}

uint32_t EnvU32(const char* name, uint32_t fallback)
{
    const char* value = std::getenv(name);
    if (value == nullptr || value[0] == '\0')
    {
        return fallback;
    }
    return static_cast<uint32_t>(std::strtoul(value, nullptr, 10));
}

PixelFormat PixelFormatFromEnv(PixelFormat fallback)
{
    const char* value = std::getenv("CAMERA_SUBSYSTEM_MPLANE_PROBE_FOURCC");
    if (value == nullptr)
    {
        return fallback;
    }
    if (std::strcmp(value, "NV12") == 0)
    {
        return PixelFormat::kNV12;
    }
    if (std::strcmp(value, "YUYV") == 0)
    {
        return PixelFormat::kYUYV;
    }
    if (std::strcmp(value, "MJPG") == 0 || std::strcmp(value, "MJPEG") == 0)
    {
        return PixelFormat::kMJPEG;
    }
    return fallback;
}

void UpdateMax(std::atomic<size_t>& target, size_t value)
{
    size_t current = target.load();
    while (value > current && !target.compare_exchange_weak(current, value))
    {
    }
}

} // namespace

int main(int argc, char* argv[])
{
    const std::string device_path = argc > 1 ? argv[1] : "/dev/video0";
    const uint32_t duration_sec = argc > 2 ? static_cast<uint32_t>(std::strtoul(argv[2], nullptr, 10)) : 5U;

    if (!PlatformLogger::Initialize(std::string(), camera_subsystem::core::LogLevel::kInfo))
    {
        return 1;
    }

    const bool mplane_probe_only = IsMPlaneProbeOnly();
    CameraConfig config(mplane_probe_only
                            ? EnvU32("CAMERA_SUBSYSTEM_MPLANE_PROBE_WIDTH", 800)
                            : 1920,
                        mplane_probe_only
                            ? EnvU32("CAMERA_SUBSYSTEM_MPLANE_PROBE_HEIGHT", 600)
                            : 1080,
                        mplane_probe_only ? PixelFormatFromEnv(PixelFormat::kNV12)
                                           : PixelFormat::kMJPEG,
                        30,
                        4,
                        static_cast<uint32_t>(IoMethod::kDmaBuf));

    CameraSource source;
    source.SetDevicePath(device_path);

    SmokeStats stats;
    source.SetFramePacketCallback(
        [&](const FramePacket& packet)
        {
            stats.frames.fetch_add(1);
            UpdateMax(stats.max_active_leases, source.GetDmaBufActiveLeaseCount());

            const auto& descriptor = packet.descriptor;
            if (descriptor.fd_count == 0 || descriptor.fds[0] < 0 ||
                descriptor.total_bytes_used == 0)
            {
                stats.mmap_fail.fetch_add(1);
                if (packet.lease)
                {
                    packet.lease->Release();
                }
                return;
            }

            const int fd = descriptor.fds[0];
            const size_t length = static_cast<size_t>(descriptor.total_bytes_used);

            if (!DmaBufSyncHelper::StartCpuAccess(fd, DmaBufSyncDirection::kRead))
            {
                stats.sync_start_fail.fetch_add(1);
            }

            void* mapped = mmap(nullptr, length, PROT_READ, MAP_SHARED, fd, 0);
            if (mapped == MAP_FAILED)
            {
                stats.mmap_fail.fetch_add(1);
            }
            else
            {
                stats.mmap_ok.fetch_add(1);
                stats.checksum.fetch_xor(SampleChecksum(static_cast<const uint8_t*>(mapped), length));
                munmap(mapped, length);
            }

            if (!DmaBufSyncHelper::EndCpuAccess(fd, DmaBufSyncDirection::kRead))
            {
                stats.sync_end_fail.fetch_add(1);
            }

            if (packet.lease)
            {
                packet.lease->Release();
            }
        });

    PlatformLogger::Log(camera_subsystem::core::LogLevel::kInfo, "dmabuf_smoke",
                        "start device=%s duration=%u", device_path.c_str(), duration_sec);

    if (!source.Initialize(config))
    {
        PlatformLogger::Log(camera_subsystem::core::LogLevel::kError, "dmabuf_smoke",
                            "CameraSource initialize failed");
        PlatformLogger::Shutdown();
        return 1;
    }

    if (mplane_probe_only)
    {
        std::printf("mplane_probe_only_result=PASS\n");
        std::printf("device=%s\n", device_path.c_str());
        PlatformLogger::Shutdown();
        return 0;
    }

    if (!source.Start())
    {
        PlatformLogger::Log(camera_subsystem::core::LogLevel::kError, "dmabuf_smoke",
                            "CameraSource start failed");
        PlatformLogger::Shutdown();
        return 1;
    }

    std::this_thread::sleep_for(std::chrono::seconds(duration_sec));
    source.Stop();

    const bool ok = source.IsDmaBufPathEnabled() &&
                    source.GetDmaBufFrameCount() > 0 &&
                    source.GetDmaBufExportFailureCount() == 0 &&
                    source.GetDmaBufLeaseExhaustedCount() == 0 &&
                    stats.mmap_ok.load() > 0 &&
                    stats.mmap_fail.load() == 0 &&
                    source.GetDmaBufActiveLeaseCount() == 0;

    std::printf("dmabuf_smoke_result=%s\n", ok ? "PASS" : "FAIL");
    std::printf("device=%s\n", device_path.c_str());
    std::printf("dmabuf_enabled=%u\n", source.IsDmaBufPathEnabled() ? 1U : 0U);
    std::printf("frames=%" PRIu64 "\n", source.GetFrameCount());
    std::printf("dmabuf_frames=%" PRIu64 "\n", source.GetDmaBufFrameCount());
    std::printf("export_fail=%" PRIu64 "\n", source.GetDmaBufExportFailureCount());
    std::printf("lease_exhausted=%" PRIu64 "\n", source.GetDmaBufLeaseExhaustedCount());
    std::printf("active_leases=%zu\n", source.GetDmaBufActiveLeaseCount());
    std::printf("max_active_leases=%zu\n", stats.max_active_leases.load());
    std::printf("lease_max=%zu\n", source.GetDmaBufLeaseInFlightMax());
    std::printf("min_queued=%zu\n", source.GetDmaBufMinQueuedCaptureBuffers());
    std::printf("cpu_mmap_ok=%" PRIu64 "\n", stats.mmap_ok.load());
    std::printf("cpu_mmap_fail=%" PRIu64 "\n", stats.mmap_fail.load());
    std::printf("sync_start_fail=%" PRIu64 "\n", stats.sync_start_fail.load());
    std::printf("sync_end_fail=%" PRIu64 "\n", stats.sync_end_fail.load());
    std::printf("checksum_xor=%" PRIu64 "\n", stats.checksum.load());

    PlatformLogger::Shutdown();
    return ok ? 0 : 1;
}
