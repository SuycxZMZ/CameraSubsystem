/**
 * @file types.h
 * @brief Camera子系统核心类型定义
 * @author CameraSubsystem Team
 * @date 2026-01-28
 */

#ifndef CAMERA_SUBSYSTEM_CORE_TYPES_H
#define CAMERA_SUBSYSTEM_CORE_TYPES_H

#include <cstdint>

namespace camera_subsystem {
namespace core {

/**
 * @brief 像素格式枚举
 */
enum class PixelFormat : uint32_t
{
    kUnknown = 0,
    kNV12,       // Y/CbCr 4:2:0, semi-planar
    kYUYV,       // YUYV 4:2:2 interleaved
    kRGB888,     // 24-bit RGB
    kRGBA8888,   // 32-bit RGBA
    kMJPEG,      // Motion JPEG
    kH264,       // H.264 encoded
    kH265,       // H.265 encoded
    kFormatCount
};

/**
 * @brief 内存类型枚举
 */
enum class MemoryType : uint32_t
{
    kMmap = 0,   // V4L2 MMAP buffer
    kDmaBuf,     // DMA-BUF file descriptor
    kShm,        // Shared memory
    kHeap        // User space heap memory
};

/**
 * @brief IO方法枚举
 */
enum class IoMethod : uint32_t
{
    kMmap = 0,
    kDmaBuf,
    kUserPtr
};

/**
 * @brief 错误码枚举
 */
enum class ErrorCode : int
{
    kOk = 0,

    // 参数错误 (100-199)
    kErrorInvalidArgument = 100,
    kErrorNullPointer,
    kErrorOutOfRange,
    kErrorInvalidConfig,

    // 设备错误 (200-299)
    kErrorDeviceNotFound = 200,
    kErrorDeviceBusy,
    kErrorDeviceDisconnected,
    kErrorPermissionDenied,
    kErrorDeviceError,

    // IO 操作错误 (300-399)
    kErrorIoctlFailed = 300,
    kErrorMmapFailed,
    kErrorDmaBufFailed,
    kErrorReadFailed,
    kErrorWriteFailed,
    kErrorEpollFailed,

    // 资源错误 (400-499)
    kErrorMemoryAlloc = 400,
    kErrorOutOfMemory,
    kErrorResourceExhausted,
    kErrorBufferFull,

    // 线程错误 (500-599)
    kErrorThreadStartFailed = 500,
    kErrorThreadJoinFailed,
    kErrorTimeout,
    kErrorDeadlock,

    // 状态错误 (600-699)
    kErrorInvalidState = 600,
    kErrorNotInitialized,
    kErrorAlreadyStarted,
    kErrorAlreadyStopped,
    kErrorNotRunning,

    // 未知错误
    kErrorUnknown = 999
};

/**
 * @brief 日志级别枚举
 */
enum class LogLevel : int
{
    kTrace = 0,
    kDebug,
    kInfo,
    kWarning,
    kError,
    kCritical
};

/**
 * @brief 获取错误码对应的描述信息
 * @param code 错误码
 * @return 错误描述字符串
 */
const char* GetErrorString(ErrorCode code);

/**
 * @brief 像素格式转字符串
 * @param format 像素格式
 * @return 格式字符串
 */
const char* PixelFormatToString(PixelFormat format);

/**
 * @brief 内存类型转字符串
 * @param type 内存类型
 * @return 类型字符串
 */
const char* MemoryTypeToString(MemoryType type);

} // namespace core
} // namespace camera_subsystem

#endif // CAMERA_SUBSYSTEM_CORE_TYPES_H
