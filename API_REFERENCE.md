# Camera 推流与 AI 基座子系统 - API 接口设计文档

**文档版本:** v1.0  
**目标平台:** Linux / Debian @ RK3576 (预留 Android 迁移)  
**开发语言:** C/C++ 混合 (数据层 C POD, 框架层 C++17)  
**文档作者:** 架构设计团队  
**创建日期:** 2026-01-27  
**最后更新:** 2026-01-27

---

## 1. 文档概述

本文档定义 Camera 推流与 AI 基座子系统的所有公开接口,包括数据结构、类接口、回调函数和常量定义。所有接口均遵循工业级设计规范,确保线程安全、异常安全和良好的可扩展性。

---

## 2. 数据结构接口

### 2.1 像素格式枚举 (PixelFormat)

```cpp
enum class PixelFormat : uint32_t
{
    kUnknown = 0,
    kNV12,       // Y/CbCr 4:2:0, semi-planar
    kYUYV,       // YUYV 4:2:2 interleaved
    kRGB888,     // 24-bit RGB
    kRGBA8888,   // 32-bit RGBA
    kMJPEG,      // Motion JPEG
    kH264,       // H.264 encoded
    kH265        // H.265 encoded
};
```

### 2.2 内存类型枚举 (MemoryType)

```cpp
enum class MemoryType : uint32_t
{
    kMmap = 0,   // V4L2 MMAP buffer
    kDmaBuf,     // DMA-BUF file descriptor
    kShm,        // Shared memory
    kHeap        // User space heap memory
};
```

### 2.3 帧句柄结构 (FrameHandle)

```cpp
struct FrameHandle
{
    // --- 基础标识 ---
    uint32_t    frame_id_;       // 全局递增帧序号
    uint32_t    camera_id_;      // 相机设备 ID
    uint64_t    timestamp_ns_;   // 纳秒级时间戳 (CLOCK_MONOTONIC)

    // --- 图像属性 ---
    uint32_t    width_;          // 图像宽度 (像素)
    uint32_t    height_;         // 图像高度 (像素)
    PixelFormat format_;         // 像素格式

    // --- 内存布局 (关键: 支持对齐与多平面) ---
    uint32_t    plane_count_;    // 平面数量 (1, 2 or 3)
    uint32_t    line_stride_[3]; // 每个平面的行跨度 (字节)
    uint32_t    plane_offset_[3];// 每个平面相对于 buffer 起始的偏移
    uint32_t    plane_size_[3];  // 每个平面的大小

    // --- 内存句柄 ---
    MemoryType  memory_type_;    // 内存类型
    int         buffer_fd_;      // DMA-BUF 或 Shared Memory FD
    void*       virtual_address_; // 映射后的虚拟地址 (仅 CPU 访问有效)
    size_t      buffer_size_;    // Buffer 总大小

    // --- 扩展字段 ---
    uint32_t    sequence_;       // 帧序列号 (V4L2)
    uint32_t    flags_;          // 标志位 (保留)
    uint8_t     reserved_[56];   // 预留扩展空间 (总计 64 字节)
};
```

### 2.4 Camera 配置结构 (CameraConfig)

```cpp
struct CameraConfig
{
    uint32_t    width_;
    uint32_t    height_;
    PixelFormat format_;
    uint32_t    fps_;
    uint32_t    buffer_count_;
    uint32_t    io_method_;     // IO_METHOD_MMAP, IO_METHOD_DMABUF
    uint8_t     reserved_[64];
};
```

### 2.5 IO 方法枚举 (IoMethod)

```cpp
enum class IoMethod : uint32_t
{
    kMmap = 0,
    kDmaBuf,
    kUserPtr
};
```

### 2.6 错误码枚举 (ErrorCode)

```cpp
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
```

---

## 3. 回调接口

### 3.1 帧回调类型 (FrameCallback)

```cpp
using FrameCallback = std::function<void(const FrameHandle& frame)>;
```

### 3.2 订阅者接口 (IFrameSubscriber)

```cpp
class IFrameSubscriber
{
public:
    virtual ~IFrameSubscriber() = default;

    /**
     * @brief 帧数据回调接口
     * @param frame 帧句柄,包含 buffer 引用
     * @note 该回调在 Worker 线程中执行,需保证线程安全且快速返回
     * @warning 不要在回调中执行耗时操作,避免阻塞分发线程
     */
    virtual void OnFrame(const FrameHandle& frame) = 0;

    /**
     * @brief 获取订阅者名称,用于调试和日志
     * @return 订阅者名称字符串
     */
    virtual const char* GetSubscriberName() const = 0;

    /**
     * @brief 获取订阅者优先级,用于任务调度
     * @return 优先级值 (0-255, 数值越大优先级越高)
     * @note 默认实现返回 128 (中等优先级)
     */
    virtual uint8_t GetPriority() const
    {
        return 128;
    }

    /**
     * @brief 订阅者被移除时的回调
     * @note 可用于清理资源或状态
     */
    virtual void OnUnsubscribed()
    {
        // 默认空实现
    }
};
```

---

## 4. CameraSource 类接口

### 4.1 构造函数与析构函数

```cpp
/**
 * @brief 构造函数
 * @param camera_id Camera 设备 ID
 */
explicit CameraSource(uint32_t camera_id);

/**
 * @brief 析构函数
 */
~CameraSource();
```

### 4.2 初始化与配置

```cpp
/**
 * @brief 初始化 Camera 设备
 * @param config Camera 配置参数
 * @return 成功返回 true,失败返回 false
 */
bool Initialize(const CameraConfig& config);
```

### 4.3 流控制

```cpp
/**
 * @brief 开始流采集
 * @return 成功返回 true,失败返回 false
 */
bool StartStreaming();

/**
 * @brief 停止流采集
 */
void StopStreaming();

/**
 * @brief 获取设备状态
 * @return true 表示正在采集,false 表示已停止
 */
bool IsStreaming() const;
```

### 4.4 回调设置

```cpp
/**
 * @brief 设置帧回调
 * @param callback 帧回调函数
 * @note 通常指向 Broker 的 Publish 接口
 */
void SetFrameCallback(FrameCallback callback);
```

### 4.5 信息获取

```cpp
/**
 * @brief 获取 Camera ID
 * @return Camera 设备 ID
 */
uint32_t GetCameraId() const;

/**
 * @brief 获取当前配置
 * @return Camera 配置参数
 */
const CameraConfig& GetConfig() const;
```

---

## 5. FrameBroker 类接口

### 5.1 构造函数与析构函数

```cpp
/**
 * @brief 构造函数
 */
FrameBroker();

/**
 * @brief 析构函数
 */
~FrameBroker();
```

### 5.2 生命周期控制

```cpp
/**
 * @brief 启动 Broker,创建 Worker 线程池
 * @param thread_num Worker 线程数量,默认为 4
 * @return 成功返回 true,失败返回 false
 */
bool Start(size_t thread_num = 4);

/**
 * @brief 停止 Broker,停止接收新任务
 */
void Stop();

/**
 * @brief 等待所有任务处理完成,回收 Worker 线程
 */
void Join();

/**
 * @brief 获取运行状态
 * @return true 表示正在运行
 */
bool IsRunning() const;
```

### 5.3 订阅管理

```cpp
/**
 * @brief 注册订阅者
 * @param camera_id 指定订阅的相机 ID,0xFFFFFFFF 表示订阅所有相机
 * @param subscriber 订阅者智能指针
 * @return 成功返回 true,失败返回 false
 */
bool Subscribe(
    uint32_t camera_id,
    std::shared_ptr<IFrameSubscriber> subscriber
);

/**
 * @brief 注销订阅者
 * @param subscriber 订阅者智能指针
 */
void Unsubscribe(std::shared_ptr<IFrameSubscriber> subscriber);

/**
 * @brief 注销指定 Camera ID 的所有订阅者
 * @param camera_id Camera ID
 */
void UnsubscribeAll(uint32_t camera_id);

/**
 * @brief 注销所有订阅者
 */
void ClearAllSubscribers();

/**
 * @brief 获取订阅者数量
 * @param camera_id Camera ID
 * @return 订阅者数量
 */
size_t GetSubscriberCount(uint32_t camera_id) const;
```

### 5.4 发布接口

```cpp
/**
 * @brief 发布帧数据
 * @param frame 帧句柄
 * @note 该函数是非阻塞的,帧数据会被放入任务队列
 */
void Publish(const FrameHandle& frame);
```

### 5.5 统计信息

```cpp
/**
 * @brief 统计信息结构体
 */
struct Statistics
{
    uint64_t total_frames_published_;   // 总发布帧数
    uint64_t total_frames_dispatched_;  // 总分发帧数
    uint64_t total_frames_dropped_;     // 总丢弃帧数
    uint64_t current_queue_size_;       // 当前队列大小
    uint64_t peak_queue_size_;          // 峰值队列大小
};

/**
 * @brief 获取统计信息
 * @return 统计信息结构体
 */
Statistics GetStatistics() const;

/**
 * @brief 重置统计信息
 */
void ResetStatistics();
```

---

## 6. PlatformThread 类接口

### 6.1 类型定义

```cpp
using ThreadFunc = std::function<void()>;
```

### 6.2 构造函数与析构函数

```cpp
/**
 * @brief 构造函数
 * @param name 线程名称 (用于调试)
 * @param func 线程执行函数
 */
PlatformThread(const std::string& name, ThreadFunc func);

/**
 * @brief 析构函数
 */
~PlatformThread();
```

### 6.3 线程控制

```cpp
/**
 * @brief 启动线程
 * @return 成功返回 true,失败返回 false
 */
bool Start();

/**
 * @brief 等待线程结束
 */
void Join();

/**
 * @brief 检查线程是否正在运行
 * @return true 表示正在运行
 */
bool IsRunning() const;

/**
 * @brief 获取线程 ID
 * @return 线程 ID
 */
std::thread::id GetThreadId() const;

/**
 * @brief 获取线程名称
 * @return 线程名称
 */
const std::string& GetThreadName() const;
```

### 6.4 线程属性

```cpp
/**
 * @brief 设置线程优先级
 * @param priority 优先级 (-20 到 19, 数值越小优先级越高)
 * @return 成功返回 true,失败返回 false
 */
bool SetPriority(int priority);

/**
 * @brief 设置线程 CPU 亲和性
 * @param cpu_ids CPU 核心 ID 列表
 * @return 成功返回 true,失败返回 false
 */
bool SetCpuAffinity(const std::vector<int>& cpu_ids);
```

---

## 7. PlatformEpoll 类接口

### 7.1 常量定义

```cpp
static const int kMaxEvents = 32;
```

### 7.2 构造函数与析构函数

```cpp
/**
 * @brief 构造函数
 */
PlatformEpoll();

/**
 * @brief 析构函数
 */
~PlatformEpoll();
```

### 7.3 Epoll 操作

```cpp
/**
 * @brief 创建 Epoll 实例
 * @return 成功返回 true,失败返回 false
 */
bool Create();

/**
 * @brief 添加文件描述符到 Epoll
 * @param fd 文件描述符
 * @param events 事件类型 (EPOLLIN, EPOLLOUT, EPOLLPRI 等)
 * @param data 用户数据
 * @return 成功返回 true,失败返回 false
 */
bool Add(int fd, uint32_t events, uint64_t data);

/**
 * @brief 修改文件描述符的事件
 * @param fd 文件描述符
 * @param events 事件类型
 * @return 成功返回 true,失败返回 false
 */
bool Modify(int fd, uint32_t events);

/**
 * @brief 从 Epoll 移除文件描述符
 * @param fd 文件描述符
 * @return 成功返回 true,失败返回 false
 */
bool Remove(int fd);

/**
 * @brief 等待事件
 * @param timeout_ms 超时时间 (毫秒),-1 表示无限等待
 * @param events 输出事件数组
 * @param max_events 最大事件数
 * @return 返回的事件数量,失败返回 -1
 */
int Wait(int timeout_ms, struct epoll_event* events, int max_events);

/**
 * @brief 关闭 Epoll 实例
 */
void Close();
```

---

## 8. PlatformLogger 类接口

### 8.1 日志级别枚举

```cpp
enum class LogLevel : int
{
    kTrace = 0,
    kDebug,
    kInfo,
    kWarning,
    kError,
    kCritical
};
```

### 8.2 静态方法

```cpp
/**
 * @brief 初始化日志系统
 * @param log_file 日志文件路径,空字符串表示仅输出到控制台
 * @param level 日志级别
 * @return 成功返回 true,失败返回 false
 */
static bool Initialize(
    const std::string& log_file,
    LogLevel level = LogLevel::kInfo
);

/**
 * @brief 记录日志
 * @param level 日志级别
 * @param module 模块名称
 * @param format 格式化字符串
 * @param ... 可变参数
 */
static void Log(
    LogLevel level,
    const char* module,
    const char* format,
    ...
);

/**
 * @brief 设置日志级别
 * @param level 日志级别
 */
static void SetLogLevel(LogLevel level);

/**
 * @brief 关闭日志系统
 */
static void Shutdown();
```

---

## 9. 日志宏定义

```cpp
#define LOG_TRACE(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kTrace, module, fmt, ##__VA_ARGS__)

#define LOG_DEBUG(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kDebug, module, fmt, ##__VA_ARGS__)

#define LOG_INFO(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kInfo, module, fmt, ##__VA_ARGS__)

#define LOG_WARN(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kWarning, module, fmt, ##__VA_ARGS__)

#define LOG_ERROR(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kError, module, fmt, ##__VA_ARGS__)

#define LOG_CRITICAL(module, fmt, ...) \
    PlatformLogger::Log(LogLevel::kCritical, module, fmt, ##__VA_ARGS__)
```

---

## 10. 常量定义

```cpp
// Buffer 相关常量
const int kMaxBufferCount = 8;
const int kMinBufferCount = 2;

// 线程相关常量
const int kMaxWorkerThreads = 16;
const int kMinWorkerThreads = 1;

// 任务队列相关常量
const int kMaxQueueSize = 1000;
const int kDefaultQueueSize = 100;

// Camera 相关常量
const uint32_t kMaxCameraCount = 0;        // 0 表示不设架构层硬上限,由平台能力与配置决定
const uint32_t kMaxSubscriberCount = 0;    // 0 表示由资源预算与 QoS 策略决定

// 优先级常量
const uint8_t kMinPriority = 0;
const uint8_t kMaxPriority = 255;
const uint8_t kDefaultPriority = 128;
```

说明: Camera 路数与订阅者数量不应作为编译期硬限制,推荐通过平台能力探测与运行时资源预算得出“建议上限”。

### 10.1 平台能力模型与建议上限 API (草案)

```cpp
/**
 * @brief 流配置画像,用于能力评估与上限估算
 */
struct StreamProfile
{
    uint32_t    width_;          // 单路分辨率宽度
    uint32_t    height_;         // 单路分辨率高度
    uint32_t    fps_;            // 单路目标帧率
    PixelFormat format_;         // 像素格式
};

/**
 * @brief 运行时资源预算,用于将平台能力映射为“建议上限”
 */
struct ResourceBudget
{
    uint32_t memory_budget_mb_;      // 可用于本子系统的内存预算 (MB)
    uint32_t cpu_budget_percent_;    // 可用于本子系统的 CPU 预算 (0-100)
    uint32_t npu_budget_percent_;    // 可用于本子系统的 NPU 预算 (0-100,可选)
    uint32_t thermal_headroom_percent_; // 热余量预算 (0-100,可选)
    uint32_t safety_margin_percent_; // 安全系数 (建议 10-30)
};

/**
 * @brief 平台能力模型,由平台探测或配置文件提供
 *
 * 约定:
 * - 取值为 0 表示“未知或不设硬上限”,计算建议上限时忽略该维度约束
 * - 所有数值均为“平台能力上限”或“经验标定值”,不等同于最终建议值
 */
struct PlatformCapabilities
{
    // --- 平台标识 ---
    std::string platform_name_;      // 平台名称 (如 "RK3576")
    std::string driver_stack_;       // 驱动栈标识 (如 "V4L2+RKISP")

    // --- 硬能力或能力上限 (平台侧) ---
    uint32_t max_camera_count_hard_;     // 平台硬上限,0 表示未知/不限制
    uint64_t max_pixels_per_sec_isp_;    // ISP 侧像素吞吐能力上限
    uint64_t max_pixels_per_sec_mem_;    // 内存带宽侧可承载的像素吞吐上限

    // --- 经验成本模型 (标定/估算值) ---
    uint32_t per_stream_memory_mb_;      // 单路典型内存开销 (含缓冲池)
    uint32_t per_stream_cpu_percent_;    // 单路典型 CPU 开销 (0-100)
    uint32_t per_stream_npu_percent_;    // 单路典型 NPU 开销 (0-100,可选)

    // --- 调度建议 (用于初始化默认配置) ---
    uint32_t suggested_worker_threads_;  // 建议 Worker 线程数,0 表示未给出
};

/**
 * @brief 建议上限结果,用于指导默认配置与压测目标
 */
struct SuggestedLimits
{
    uint32_t suggested_camera_count_;        // 建议 Camera 路数上限
    uint32_t suggested_subscribers_per_camera_; // 建议每路订阅者上限
    uint32_t suggested_worker_threads_;      // 建议 Worker 线程数
};

/**
 * @brief 平台能力探测接口 (由 PlatformLayer 提供具体实现)
 */
class ICapabilityProbe
{
public:
    virtual ~ICapabilityProbe() = default;

    /**
     * @brief 探测平台能力
     * @param out_caps 输出的平台能力模型
     * @return true 表示探测成功,false 表示失败 (可降级为配置文件或默认值)
     */
    virtual bool Probe(PlatformCapabilities* out_caps) = 0;
};

/**
 * @brief 将平台能力 + 流画像 + 资源预算映射为建议上限
 *
 * 公式细化 (建议口径):
 * 1) 单路像素吞吐:
 *    pixels_per_sec = width * height * fps
 * 2) 像素吞吐侧路数上限:
 *    isp_limit = floor(max_pixels_per_sec_isp / pixels_per_sec)      (当 max_pixels_per_sec_isp > 0)
 *    mem_px_limit = floor(max_pixels_per_sec_mem / pixels_per_sec)   (当 max_pixels_per_sec_mem > 0)
 * 3) 资源预算侧路数上限:
 *    mem_budget_limit = floor(memory_budget_mb / per_stream_memory_mb)         (当两者均 > 0)
 *    cpu_budget_limit = floor(cpu_budget_percent / per_stream_cpu_percent)     (当两者均 > 0)
 *    npu_budget_limit = floor(npu_budget_percent / per_stream_npu_percent)     (当两者均 > 0)
 * 4) 合并约束:
 *    raw_limit = min(所有“已知且 > 0”的候选上限)
 * 5) 安全系数下调:
 *    margin = clamp(safety_margin_percent, 0, 80)
 *    margin_factor = (100 - margin) / 100.0
 *    margin_limit = max(1, floor(raw_limit * margin_factor))
 * 6) 平台硬上限收敛:
 *    final_limit = min(margin_limit, max_camera_count_hard_)   (当 max_camera_count_hard_ > 0)
 *
 * 参考实现伪代码 (强调“忽略未知维度”):
 * SuggestedLimits SuggestLimits(caps, profile, budget):
 *   pixels_per_sec = profile.width_ * profile.height_ * profile.fps_
 *   candidates = []
 *   if pixels_per_sec > 0 and caps.max_pixels_per_sec_isp_ > 0:
 *       candidates.push(floor(caps.max_pixels_per_sec_isp_ / pixels_per_sec))
 *   if pixels_per_sec > 0 and caps.max_pixels_per_sec_mem_ > 0:
 *       candidates.push(floor(caps.max_pixels_per_sec_mem_ / pixels_per_sec))
 *   if budget.memory_budget_mb_ > 0 and caps.per_stream_memory_mb_ > 0:
 *       candidates.push(floor(budget.memory_budget_mb_ / caps.per_stream_memory_mb_))
 *   if budget.cpu_budget_percent_ > 0 and caps.per_stream_cpu_percent_ > 0:
 *       candidates.push(floor(budget.cpu_budget_percent_ / caps.per_stream_cpu_percent_))
 *   if budget.npu_budget_percent_ > 0 and caps.per_stream_npu_percent_ > 0:
 *       candidates.push(floor(budget.npu_budget_percent_ / caps.per_stream_npu_percent_))
 *   raw_limit = candidates.empty() ? 1 : min(candidates)
 *   margin = clamp(budget.safety_margin_percent_, 0, 80)
 *   margin_limit = max(1, floor(raw_limit * (100 - margin) / 100))
 *   final_limit = (caps.max_camera_count_hard_ > 0) ? min(margin_limit, caps.max_camera_count_hard_) : margin_limit
 *   suggested_workers =
 *       (caps.suggested_worker_threads_ > 0)
 *       ? caps.suggested_worker_threads_
 *       : clamp(final_limit * 2, kMinWorkerThreads, kMaxWorkerThreads)
 *   suggested_subscribers_per_camera = max(1, suggested_workers / max(1, final_limit))
 *   return { final_limit, suggested_subscribers_per_camera, suggested_workers }
 *
 * 默认参数建议 (用于统一“初始口径”,后续应以平台标定替换):
 * 1) safety_margin_percent:
 *    建议默认 20 (高负载或强实时场景可取 25-30)
 * 2) per_stream_memory_mb:
 *    建议按公式估算并向上取整:
 *    nv12_frame_bytes ~= width * height * 3 / 2
 *    buffer_pool_bytes ~= nv12_frame_bytes * buffer_count
 *    per_stream_memory_mb ~= ceil(buffer_pool_bytes / 1024 / 1024 * 1.5)
 *    对于 1080p@30fps + NV12 + buffer_count=6,可先用 32MB 作为默认标称值
 * 3) per_stream_cpu_percent:
 *    对“仅采集+分发”路径,建议默认标称值 5 (后续以 profiling 标定)
 * 4) per_stream_npu_percent:
 *    若 NPU 不在关键路径,建议默认 0;若在关键路径,建议由模型侧提供标称值
 */
SuggestedLimits SuggestLimits(
    const PlatformCapabilities& caps,
    const StreamProfile& profile,
    const ResourceBudget& budget);
```

---

## 11. 辅助函数

### 11.1 错误处理

```cpp
/**
 * @brief 获取错误码对应的描述信息
 * @param code 错误码
 * @return 错误描述字符串
 */
const char* GetErrorString(ErrorCode code);

/**
 * @brief 获取当前系统错误码
 * @return 系统错误码
 */
int GetSystemErrorCode();

/**
 * @brief 获取系统错误描述
 * @param error_code 系统错误码
 * @return 错误描述字符串
 */
const char* GetSystemErrorString(int error_code);
```

### 11.2 格式转换

```cpp
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
```

---

## 12. 版本信息

```cpp
/**
 * @brief 版本信息结构体
 */
struct VersionInfo
{
    uint32_t major_;  // 主版本号
    uint32_t minor_;  // 次版本号
    uint32_t patch_;  // 补丁版本号
    const char* build_date_;  // 构建日期
    const char* git_commit_;  // Git 提交哈希
};

/**
 * @brief 获取版本信息
 * @return 版本信息结构体
 */
const VersionInfo& GetVersionInfo();
```

---

## 13. 使用示例

### 13.1 CameraSource 使用示例

```cpp
// 创建 CameraSource 实例
auto camera = std::make_unique<CameraSource>(0);

// 设置配置
CameraConfig config;
config.width_ = 1920;
config.height_ = 1080;
config.format_ = PixelFormat::kNV12;
config.fps_ = 30;
config.buffer_count_ = 4;
config.io_method_ = static_cast<uint32_t>(IoMethod::kDmaBuf);

// 初始化
if (!camera->Initialize(config))
{
    LOG_ERROR("main", "Failed to initialize camera");
    return;
}

// 设置帧回调
camera->SetFrameCallback(
    [](const FrameHandle& frame)
    {
        // 处理帧数据
        LOG_INFO("main", "Received frame: %u", frame.frame_id_);
    }
);

// 开始采集
if (!camera->StartStreaming())
{
    LOG_ERROR("main", "Failed to start streaming");
    return;
}

// ... 运行 ...

// 停止采集
camera->StopStreaming();
```

### 13.2 FrameBroker 使用示例

```cpp
// 创建 FrameBroker 实例
auto broker = std::make_unique<FrameBroker>();

// 启动 Broker
if (!broker->Start(4))
{
    LOG_ERROR("main", "Failed to start broker");
    return;
}

// 创建订阅者
class MySubscriber : public IFrameSubscriber
{
public:
    void OnFrame(const FrameHandle& frame) override
    {
        // 处理帧数据
        LOG_INFO("MySubscriber", "Received frame: %u", frame.frame_id_);
    }

    const char* GetSubscriberName() const override
    {
        return "MySubscriber";
    }
};

auto subscriber = std::make_shared<MySubscriber>();

// 注册订阅者
broker->Subscribe(0, subscriber);

// ... 运行 ...

// 注销订阅者
broker->Unsubscribe(subscriber);

// 停止 Broker
broker->Stop();
broker->Join();
```

### 13.3 完整使用示例

```cpp
int main()
{
    // 初始化日志系统
    PlatformLogger::Initialize("camera.log", LogLevel::kInfo);

    // 创建 Broker
    auto broker = std::make_unique<FrameBroker>();
    if (!broker->Start(4))
    {
        LOG_ERROR("main", "Failed to start broker");
        return -1;
    }

    // 创建 Camera
    auto camera = std::make_unique<CameraSource>(0);

    // 设置配置
    CameraConfig config;
    config.width_ = 1920;
    config.height_ = 1080;
    config.format_ = PixelFormat::kNV12;
    config.fps_ = 30;
    config.buffer_count_ = 4;
    config.io_method_ = static_cast<uint32_t>(IoMethod::kDmaBuf);

    // 初始化 Camera
    if (!camera->Initialize(config))
    {
        LOG_ERROR("main", "Failed to initialize camera");
        return -1;
    }

    // 设置帧回调
    camera->SetFrameCallback(
        [broker_ptr = broker.get()](const FrameHandle& frame)
        {
            broker_ptr->Publish(frame);
        }
    );

    // 创建订阅者
    auto subscriber = std::make_shared<MySubscriber>();
    broker->Subscribe(0, subscriber);

    // 开始采集
    if (!camera->StartStreaming())
    {
        LOG_ERROR("main", "Failed to start streaming");
        return -1;
    }

    // 运行一段时间
    std::this_thread::sleep_for(std::chrono::seconds(10));

    // 清理资源
    camera->StopStreaming();
    broker->Unsubscribe(subscriber);
    broker->Stop();
    broker->Join();

    PlatformLogger::Shutdown();

    return 0;
}
```

---

## 14. 接口版本管理

### 14.1 版本号规则

版本号采用语义化版本控制 (Semantic Versioning):
- 主版本号 (Major): 不兼容的 API 修改
- 次版本号 (Minor): 向下兼容的功能性新增
- 补丁版本号 (Patch): 向下兼容的问题修正

### 14.2 向后兼容性

- 新增接口不影响现有接口
- 废弃接口保留至少一个主版本周期
- 重大变更需要提供迁移指南

---

## 15. 线程安全保证

### 15.1 线程安全接口

以下接口是线程安全的,可以在多线程环境中安全调用:
- FrameBroker::Publish()
- FrameBroker::Subscribe()
- FrameBroker::Unsubscribe()
- FrameBroker::GetStatistics()
- PlatformLogger::Log()

### 15.2 非线程安全接口

以下接口不是线程安全的,需要在单线程环境中调用:
- CameraSource::Initialize()
- CameraSource::StartStreaming()
- CameraSource::StopStreaming()
- FrameBroker::Start()
- FrameBroker::Stop()

---

**文档结束**
