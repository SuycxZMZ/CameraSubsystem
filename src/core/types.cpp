/**
 * @file types.cpp
 * @brief Camera子系统核心类型实现
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#include "camera_subsystem/core/types.h"

namespace camera_subsystem {
namespace core {

const char* GetErrorString(ErrorCode code)
{
    switch (code)
    {
        case ErrorCode::kOk:
            return "Success";

        // 参数错误
        case ErrorCode::kErrorInvalidArgument:
            return "Invalid argument";
        case ErrorCode::kErrorNullPointer:
            return "Null pointer";
        case ErrorCode::kErrorOutOfRange:
            return "Out of range";
        case ErrorCode::kErrorInvalidConfig:
            return "Invalid configuration";

        // 设备错误
        case ErrorCode::kErrorDeviceNotFound:
            return "Device not found";
        case ErrorCode::kErrorDeviceBusy:
            return "Device busy";
        case ErrorCode::kErrorDeviceDisconnected:
            return "Device disconnected";
        case ErrorCode::kErrorPermissionDenied:
            return "Permission denied";
        case ErrorCode::kErrorDeviceError:
            return "Device error";

        // IO 操作错误
        case ErrorCode::kErrorIoctlFailed:
            return "Ioctl failed";
        case ErrorCode::kErrorMmapFailed:
            return "Mmap failed";
        case ErrorCode::kErrorDmaBufFailed:
            return "DmaBuf failed";
        case ErrorCode::kErrorReadFailed:
            return "Read failed";
        case ErrorCode::kErrorWriteFailed:
            return "Write failed";
        case ErrorCode::kErrorEpollFailed:
            return "Epoll failed";

        // 资源错误
        case ErrorCode::kErrorMemoryAlloc:
            return "Memory allocation failed";
        case ErrorCode::kErrorOutOfMemory:
            return "Out of memory";
        case ErrorCode::kErrorResourceExhausted:
            return "Resource exhausted";
        case ErrorCode::kErrorBufferFull:
            return "Buffer full";

        // 线程错误
        case ErrorCode::kErrorThreadStartFailed:
            return "Thread start failed";
        case ErrorCode::kErrorThreadJoinFailed:
            return "Thread join failed";
        case ErrorCode::kErrorTimeout:
            return "Timeout";
        case ErrorCode::kErrorDeadlock:
            return "Deadlock detected";

        // 状态错误
        case ErrorCode::kErrorInvalidState:
            return "Invalid state";
        case ErrorCode::kErrorNotInitialized:
            return "Not initialized";
        case ErrorCode::kErrorAlreadyStarted:
            return "Already started";
        case ErrorCode::kErrorAlreadyStopped:
            return "Already stopped";
        case ErrorCode::kErrorNotRunning:
            return "Not running";

        // 未知错误
        case ErrorCode::kErrorUnknown:
        default:
            return "Unknown error";
    }
}

const char* PixelFormatToString(PixelFormat format)
{
    switch (format)
    {
        case PixelFormat::kUnknown:
            return "Unknown";
        case PixelFormat::kNV12:
            return "NV12";
        case PixelFormat::kYUYV:
            return "YUYV";
        case PixelFormat::kRGB888:
            return "RGB888";
        case PixelFormat::kRGBA8888:
            return "RGBA8888";
        case PixelFormat::kMJPEG:
            return "MJPEG";
        case PixelFormat::kH264:
            return "H264";
        case PixelFormat::kH265:
            return "H265";
        case PixelFormat::kFormatCount:
            return "Count";
        default:
            return "Invalid";
    }
}

const char* MemoryTypeToString(MemoryType type)
{
    switch (type)
    {
        case MemoryType::kMmap:
            return "Mmap";
        case MemoryType::kDmaBuf:
            return "DmaBuf";
        case MemoryType::kShm:
            return "SharedMemory";
        case MemoryType::kHeap:
            return "Heap";
        default:
            return "Invalid";
    }
}

} // namespace core
} // namespace camera_subsystem
