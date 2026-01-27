# Camera 推流与 AI 基座子系统 - 命名规范文档

**文档版本:** v1.0  
**目标平台:** Linux / Debian @ RK3576 (预留 Android 迁移)  
**开发语言:** C/C++ 混合 (数据层 C POD, 框架层 C++17)  
**文档作者:** 架构设计团队  
**创建日期:** 2026-01-27  
**最后更新:** 2026-01-27

---

## 1. 文档概述

本文档定义 Camera 推流与 AI 基座子系统的命名规范,包括类名、函数名、变量名、常量名等。统一的命名规范可以提高代码可读性、可维护性和团队协作效率。

### 1.1 文档目的

- 确保团队所有成员使用一致的命名风格
- 提高代码可读性和可维护性
- 减少命名冲突和歧义
- 便于代码审查和重构

### 1.2 适用范围

本规范适用于本项目的所有 C/C++ 代码,包括:
- 头文件 (.h)
- 源文件 (.cpp)
- 内联文件 (.inl)
- 模板文件

---

## 2. 命名规范总则

### 2.1 基本规则

| 类型 | 命名风格 | 示例 |
|------|---------|------|
| 类名 | 大驼峰 (PascalCase) | `class CameraSource`, `class FrameBroker` |
| 结构体 | 大驼峰 (PascalCase) | `struct FrameHandle`, `struct CameraConfig` |
| 联合体 | 大驼峰 (PascalCase) | `union DataBuffer` |
| 枚举类 | 大驼峰 (PascalCase) | `enum class PixelFormat` |
| 枚举值 | 小驼峰 + k 前缀 | `kNV12`, `kRGB888` |
| 函数/方法 | 大驼峰 (PascalCase) | `void StartStreaming()`, `bool Publish()` |
| 成员变量 | 小写 + 下划线后缀 | `int device_fd_`, `bool is_running_` |
| 局部变量 | 小写 + 下划线 | `int frame_count`, `void* buffer_ptr` |
| 全局变量 | g_ + 前缀 + 下划线后缀 | `int g_system_instance_count_` |
| 静态变量 | s_ + 前缀 + 下划线后缀 | `int s_instance_count_` |
| 常量 | k 前缀 + 大驼峰 | `const int kMaxBufferCount = 4` |
| 宏定义 | 全大写 + 下划线 | `#define MAX_SIZE 100` |
| 命名空间 | 全小写 + 下划线 | `namespace camera_subsystem` |
| 文件名 | 全小写 + 下划线 | `camera_source.h`, `frame_broker.cpp` |
| 类型别名 | 大驼峰 (PascalCase) | `using FrameCallback = ...` |
| 模板参数 | 大驼峰 (PascalCase) | `template <typename T>` |

### 2.2 通用约定

- 使用英文单词命名,避免使用拼音或缩写
- 名称长度建议不超过 32 个字符
- 避免使用保留字和关键字
- 避免使用容易混淆的字符 (如 l, 1, I, O, 0)
- 避免使用下划线开头和结尾 (除特殊情况外)

---

## 3. 类名规范

### 3.1 命名规则

**格式:** PascalCase (每个单词首字母大写)

**示例:**
- `CameraSource`
- `FrameBroker`
- `PlatformThread`
- `BufferPool`
- `ImageProcessor`

### 3.2 命名建议

- 类名应为名词,表示一个实体或概念
- 使用完整的单词,避免缩写
- 类名应能清楚地表达类的职责
- 避免使用过于通用的名称 (如 `Manager`, `Handler`)

### 3.3 特殊类名约定

**接口类:**
- 以大写字母 `I` 开头
- 示例: `IFrameSubscriber`, `IVideoEncoder`, `IDeviceController`

**工具类:**
- 以 `Util` 或 `Helper` 结尾
- 示例: `StringUtils`, `MathHelper`, `TimeUtil`

**异常类:**
- 以 `Exception` 结尾
- 示例: `CameraException`, `BufferException`, `TimeoutException`

**工厂类:**
- 以 `Factory` 结尾
- 示例: `CameraFactory`, `BufferFactory`

**单例类:**
- 使用 `GetInstance()` 方法
- 示例: `Logger::GetInstance()`

### 3.4 核心类名清单

```cpp
// 核心类
class CameraSource;
class FrameBroker;
class FrameHandle;
class CameraConfig;

// 平台抽象类
class PlatformThread;
class PlatformEpoll;
class PlatformLogger;
class PlatformMutex;
class PlatformCondition;

// Buffer 管理类
class BufferPool;
class BufferGuard;
class BufferAllocator;

// 辅助类
class StringUtils;
class MathUtils;
class TimeUtils;
class ConfigParser;

// 异常类
class CameraException;
class BufferException;
class TimeoutException;
```

---

## 4. 函数名规范

### 4.1 命名规则

**格式:** PascalCase (每个单词首字母大写)

**示例:**
- `Initialize`
- `StartStreaming`
- `StopStreaming`
- `GetStatistics`
- `SetFrameCallback`

### 4.2 命名建议

- 函数名应为动词或动词短语,表示执行的动作
- 使用完整的单词,避免缩写
- 函数名应能清楚地表达函数的功能

### 4.3 常用动词前缀

| 动词前缀 | 用途 | 示例 |
|---------|------|------|
| Get | 获取值 | `GetCameraId()`, `GetConfig()`, `GetStatistics()` |
| Set | 设置值 | `SetFrameCallback()`, `SetPriority()`, `SetConfig()` |
| Is | 判断状态 (返回 bool) | `IsStreaming()`, `IsInitialized()`, `IsRunning()` |
| Has | 判断是否存在 (返回 bool) | `HasBuffer()`, `HasSubscriber()` |
| Can | 判断是否可以 (返回 bool) | `CanStart()`, `CanStop()` |
| Create | 创建对象 | `CreateBuffer()`, `CreateThread()` |
| Destroy | 销毁对象 | `DestroyBuffer()`, `DestroyThread()` |
| Open | 打开资源 | `OpenDevice()`, `OpenFile()` |
| Close | 关闭资源 | `CloseDevice()`, `CloseFile()` |
| Start | 启动 | `StartStreaming()`, `StartThread()` |
| Stop | 停止 | `StopStreaming()`, `StopThread()` |
| Init | 初始化 | `Initialize()`, `InitDevice()` |
| Reset | 重置 | `ResetStatistics()`, `ResetBuffer()` |
| Add | 添加 | `AddSubscriber()`, `AddBuffer()` |
| Remove | 移除 | `RemoveSubscriber()`, `RemoveBuffer()` |
| Clear | 清空 | `ClearAllSubscribers()`, `ClearBuffer()` |
| Update | 更新 | `UpdateConfig()`, `UpdateStatistics()` |
| Handle | 处理 | `HandleV4L2Event()`, `HandleError()` |
| Process | 处理 | `ProcessFrame()`, `ProcessData()` |
| Parse | 解析 | `ParseConfig()`, `ParseData()` |
| Serialize | 序列化 | `SerializeFrame()`, `SerializeData()` |
| Deserialize | 反序列化 | `DeserializeFrame()`, `DeserializeData()` |
| Validate | 验证 | `ValidateConfig()`, `ValidateData()` |
| Register | 注册 | `RegisterSubscriber()`, `RegisterCallback()` |
| Unregister | 注销 | `UnregisterSubscriber()`, `UnregisterCallback()` |
| Subscribe | 订阅 | `Subscribe()`, `SubscribeToEvent()` |
| Unsubscribe | 取消订阅 | `Unsubscribe()`, `UnsubscribeFromEvent()` |
| Publish | 发布 | `Publish()`, `PublishEvent()` |
| Dispatch | 分发 | `DispatchFrame()`, `DispatchTask()` |
| Acquire | 获取 (资源) | `AcquireBuffer()`, `AcquireLock()` |
| Release | 释放 (资源) | `ReleaseBuffer()`, `ReleaseLock()` |
| Allocate | 分配 | `AllocateBuffer()`, `AllocateMemory()` |
| Deallocate | 释放分配 | `DeallocateBuffer()`, `DeallocateMemory()` |
| Map | 映射 | `MapBuffer()`, `MapMemory()` |
| Unmap | 取消映射 | `UnmapBuffer()`, `UnmapMemory()` |
| Queue | 入队 | `QueueBuffer()`, `QueueTask()` |
| Dequeue | 出队 | `DequeueBuffer()`, `DequeueTask()` |
| Lock | 加锁 | `Lock()`, `LockMutex()` |
| Unlock | 解锁 | `Unlock()`, `UnlockMutex()` |
| Wait | 等待 | `Wait()`, `WaitForEvent()` |
| Notify | 通知 | `Notify()`, `NotifyAll()` |
| Join | 等待线程结束 | `Join()`, `JoinThread()` |
| Detach | 分离线程 | `Detach()`, `DetachThread()` |

---

## 5. 变量名规范

### 5.1 成员变量命名

**格式:** 小写 + 下划线后缀

**示例:**
- `camera_id_`
- `device_fd_`
- `is_running_`
- `frame_callback_`
- `buffer_pool_`

**命名建议:**
- 使用名词
- 布尔变量以 `is_`, `has_`, `can_` 等前缀开头
- 指针变量以 `_ptr` 结尾
- 计数器以 `_count` 结尾
- 索引以 `_index` 结尾

### 5.2 局部变量命名

**格式:** 小写 + 下划线

**示例:**
- `frame_count`
- `buffer_ptr`
- `camera_id`
- `device_fd`
- `config`

**命名建议:**
- 使用简短但清晰的名称
- 循环变量使用 `i`, `j`, `k`
- 布尔变量以 `is_`, `has_`, `can_` 等前缀开头
- 指针变量以 `_ptr` 结尾
- 计数器以 `_count` 结尾

### 5.3 全局变量命名

**格式:** g_ + 前缀 + 下划线后缀

**示例:**
- `g_system_instance_count_`
- `g_global_config_`
- `g_logger_instance_`

**命名建议:**
- 尽量避免使用全局变量
- 如必须使用,使用 `g_` 前缀标识
- 使用单例模式替代全局变量

### 5.4 静态变量命名

**格式:** s_ + 前缀 + 下划线后缀

**示例:**
- `s_instance_`
- `s_config_`
- `s_initialized_`

**命名建议:**
- 类内静态成员变量使用 `s_` 前缀
- 文件作用域静态变量使用 `s_` 前缀

### 5.5 具体变量名清单

```cpp
// CameraSource 类成员变量
uint32_t            camera_id_;
int                 device_fd_;
std::string         device_path_;
CameraConfig        config_;
std::atomic<bool>   is_streaming_;
std::atomic<bool>   is_initialized_;
std::vector<BufferInfo> buffer_pool_;
std::unique_ptr<PlatformThread> capture_thread_;
std::atomic<bool>       should_stop_;
FrameCallback           frame_callback_;
int                     epoll_fd_;

// FrameBroker 类成员变量
SubscriptionMap     subscription_map_;
std::shared_mutex    subscription_map_mutex_;
std::priority_queue<DispatchTask, std::vector<DispatchTask>, TaskComparator> task_queue_;
std::mutex           task_queue_mutex_;
std::condition_variable task_queue_cv_;
std::vector<std::unique_ptr<PlatformThread>> worker_threads_;
std::atomic<bool>   is_running_;
std::atomic<bool>   should_stop_;
std::mutex          stats_mutex_;
Statistics          stats_;

// PlatformThread 类成员变量
std::string         thread_name_;
ThreadFunc          thread_func_;
std::thread         native_thread_;
std::atomic<bool>   is_running_;
std::atomic<bool>   should_stop_;

// PlatformEpoll 类成员变量
int                 epoll_fd_;
```

---

## 6. 常量命名规范

### 6.1 命名规则

**格式:** k 前缀 + 大驼峰

**示例:**
- `kMaxBufferCount`
- `kDefaultFps`
- `kDefaultWidth`
- `kDefaultHeight`

### 6.2 命名建议

- 使用有意义的名称
- 避免使用魔法数字
- 将相关常量组织在一起

### 6.3 具体常量清单

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

// 尺寸相关常量
const uint32_t kDefaultWidth = 1920;
const uint32_t kDefaultHeight = 1080;
const uint32_t kDefaultFps = 30;

// Epoll 相关常量
const int kMaxEpollEvents = 32;
const int kEpollTimeout = 1000;

// 超时相关常量
const int kDefaultTimeout = 5000;  // 毫秒
const int kMaxTimeout = 30000;      // 毫秒

// 日志相关常量
const size_t kMaxLogMessageSize = 1024;
const size_t kMaxLogFileSize = 100 * 1024 * 1024;  // 100MB
```

---

## 7. 枚举命名规范

### 7.1 枚举类命名

**格式:** PascalCase (每个单词首字母大写)

**示例:**
- `PixelFormat`
- `MemoryType`
- `IoMethod`
- `LogLevel`
- `ErrorCode`

### 7.2 枚举值命名

**格式:** 小驼峰 + k 前缀

**示例:**
- `kUnknown`
- `kNV12`
- `kRGB888`
- `kMmap`
- `kDmaBuf`

### 7.3 具体枚举示例

```cpp
// 像素格式枚举
enum class PixelFormat : uint32_t
{
    kUnknown = 0,
    kNV12,
    kYUYV,
    kRGB888,
    kRGBA8888,
    kMJPEG,
    kH264,
    kH265,
    kFormatCount
};

// 内存类型枚举
enum class MemoryType : uint32_t
{
    kMmap = 0,
    kDmaBuf,
    kShm,
    kHeap
};

// IO 方法枚举
enum class IoMethod : uint32_t
{
    kMmap = 0,
    kDmaBuf,
    kUserPtr
};

// 日志级别枚举
enum class LogLevel : int
{
    kTrace = 0,
    kDebug,
    kInfo,
    kWarning,
    kError,
    kCritical
};

// 错误码枚举
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

## 8. 命名空间规范

### 8.1 命名规则

**格式:** 全小写 + 下划线

**示例:**
- `camera_subsystem`
- `platform`
- `utils`
- `core`

### 8.2 命名建议

- 使用模块名作为命名空间
- 避免使用过长的命名空间
- 嵌套命名空间不超过 3 层

### 8.3 具体命名空间清单

```cpp
// 主命名空间
namespace camera_subsystem
{
    // 核心模块
    namespace core
    {
        class FrameHandle;
        class CameraConfig;
        enum class ErrorCode;
    }

    // Camera 模块
    namespace camera
    {
        class CameraSource;
    }

    // Broker 模块
    namespace broker
    {
        class FrameBroker;
        class IFrameSubscriber;
    }

    // Platform 模块
    namespace platform
    {
        class PlatformThread;
        class PlatformEpoll;
        class PlatformLogger;
    }

    // Utils 模块
    namespace utils
    {
        class StringUtils;
        class TimeUtils;
    }
}
```

---

## 9. 文件命名规范

### 9.1 命名规则

**格式:** 全小写 + 下划线

**示例:**
- `camera_source.h`
- `frame_broker.cpp`
- `platform_thread.h`
- `frame_handle.h`

### 9.2 目录结构

```
camera_subsystem/
├── include/
│   └── camera_subsystem/
│       ├── core/
│       │   ├── frame_handle.h
│       │   ├── camera_config.h
│       │   └── error_code.h
│       ├── camera/
│       │   └── camera_source.h
│       ├── broker/
│       │   ├── frame_broker.h
│       │   └── frame_subscriber.h
│       └── platform/
│           ├── platform_thread.h
│           ├── platform_epoll.h
│           └── platform_logger.h
├── src/
│   ├── camera/
│   │   └── camera_source.cpp
│   ├── broker/
│   │   ├── frame_broker.cpp
│   │   └── frame_subscriber.cpp
│   └── platform/
│       ├── platform_thread.cpp
│       ├── platform_epoll.cpp
│       └── platform_logger.cpp
└── tests/
    ├── unit/
    └── integration/
```

---

## 10. 类型别名规范

### 10.1 命名规则

**格式:** PascalCase (每个单词首字母大写)

**示例:**
- `FrameCallback`
- `ThreadFunc`
- `SubscriberList`
- `SubscriptionMap`

### 10.2 具体类型别名清单

```cpp
// CameraSource 类
using FrameCallback = std::function<void(const FrameHandle&)>;

// PlatformThread 类
using ThreadFunc = std::function<void()>;

// FrameBroker 类
using SubscriberList = std::vector<std::weak_ptr<IFrameSubscriber>>;
using SubscriptionMap = std::unordered_map<uint32_t, SubscriberList>;
```

---

## 11. 模板参数规范

### 11.1 命名规则

**格式:** PascalCase (每个单词首字母大写)

**示例:**
- `T`
- `Key`
- `Value`
- `Allocator`

### 11.2 具体模板参数示例

```cpp
template <typename T>
class BufferPool
{
    // ...
};

template <typename Key, typename Value>
class Cache
{
    // ...
};

template <typename T, typename Allocator = std::allocator<T>>
class CustomVector
{
    // ...
};
```

---

## 12. 命名示例

### 12.1 完整类示例

```cpp
class FrameBroker
{
public:
    explicit FrameBroker(const std::string& name);

    bool Start(size_t thread_num = 4)
    {
        is_running_ = true;

        for (size_t i = 0; i < thread_num; ++i)
        {
            auto thread = std::make_unique<PlatformThread>(
                "worker_" + std::to_string(i),
                [this]() { WorkerThreadLoop(); }
            );

            if (!thread->Start())
            {
                LOG_ERROR("frame_broker", "Failed to start worker thread %zu", i);
                return false;
            }

            worker_threads_.push_back(std::move(thread));
        }

        return true;
    }

    void Stop()
    {
        is_running_ = false;
        should_stop_ = true;

        // 唤醒所有 Worker 线程
        task_queue_cv_.notify_all();
    }

private:
    void WorkerThreadLoop()
    {
        while (is_running_)
        {
            DispatchTask task;

            {
                std::unique_lock<std::mutex> lock(task_queue_mutex_);
                task_queue_cv_.wait(lock, [this]()
                {
                    return !task_queue_.empty() || should_stop_;
                });

                if (should_stop_ && task_queue_.empty())
                {
                    break;
                }

                task = task_queue_.top();
                task_queue_.pop();
            }

            DispatchFrame(task.frame_);
        }
    }

    std::string                     broker_name_;
    std::atomic<bool>               is_running_;
    std::atomic<bool>               should_stop_;
    std::vector<std::unique_ptr<PlatformThread>> worker_threads_;
    std::priority_queue<DispatchTask,
                      std::vector<DispatchTask>,
                      TaskComparator> task_queue_;
    std::mutex                      task_queue_mutex_;
    std::condition_variable         task_queue_cv_;
};
```

### 12.2 全局变量示例

```cpp
int g_active_broker_count_ = 0;
const int kMaxWorkerThreads = 16;
```

---

## 13. 命名检查清单

在编写代码时,请使用以下清单检查命名是否符合规范:

- [ ] 类名使用 PascalCase
- [ ] 函数名使用 PascalCase
- [ ] 成员变量使用小写 + 下划线后缀
- [ ] 局部变量使用小写 + 下划线
- [ ] 全局变量使用 g_ 前缀
- [ ] 静态变量使用 s_ 前缀
- [ ] 常量使用 k 前缀
- [ ] 宏定义使用全大写
- [ ] 枚举类使用 PascalCase
- [ ] 枚举值使用 k 前缀 + 小驼峰
- [ ] 命名空间使用全小写 + 下划线
- [ ] 文件名使用全小写 + 下划线
- [ ] 类型别名使用 PascalCase
- [ ] 模板参数使用 PascalCase
- [ ] 布尔变量使用 is_/has_/can_ 前缀
- [ ] 指针变量使用 _ptr 后缀
- [ ] 计数器使用 _count 后缀
- [ ] 索引使用 _index 后缀
- [ ] 名称长度不超过 32 个字符
- [ ] 使用英文单词命名
- [ ] 避免使用缩写

---

## 14. 命名规范速查表

### 14.1 快速参考

| 类型 | 命名风格 | 示例 |
|------|---------|------|
| 类名 | PascalCase | `CameraSource`, `FrameBroker` |
| 结构体 | PascalCase | `FrameHandle`, `CameraConfig` |
| 函数/方法 | PascalCase | `StartStreaming()`, `Publish()` |
| 成员变量 | 小写 + _ | `device_fd_`, `is_running_` |
| 局部变量 | 小写 + _ | `frame_count`, `buffer_ptr` |
| 全局变量 | g_ + _ | `g_system_instance_count_` |
| 常量 | k + PascalCase | `kMaxBufferCount = 4` |
| 枚举类 | PascalCase | `PixelFormat { kNV12, kRGB }` |
| 枚举值 | k + camelCase | `kNV12`, `kRGB888` |
| 命名空间 | lowercase + _ | `camera_subsystem` |
| 文件名 | lowercase + _ | `camera_source.h` |

### 14.2 常用动词前缀

Get, Set, Is, Has, Can, Create, Destroy, Open, Close, Start, Stop,
Init, Reset, Add, Remove, Clear, Update, Handle, Process, Parse,
Serialize, Deserialize, Validate, Register, Unregister, Subscribe,
Unsubscribe, Publish, Dispatch, Acquire, Release, Allocate, Deallocate,
Map, Unmap, Queue, Dequeue, Lock, Unlock, Wait, Notify, Join, Detach

### 14.3 常用后缀

- `_ptr` (指针)
- `_count` (计数器)
- `_index` (索引)
- `_size` (大小)
- `_length` (长度)
- `_time` (时间)
- `_fd` (文件描述符)
- `_id` (标识符)
- `_name` (名称)
- `_path` (路径)
- `_config` (配置)
- `_mutex` (互斥锁)
- `_thread` (线程)
- `_callback` (回调)
- `_handler` (处理器)
- `_manager` (管理器)
- `_pool` (池)
- `_guard` (守卫)
- `_info` (信息)
- `_data` (数据)
- `_buffer` (缓冲区)

---

**文档结束**
