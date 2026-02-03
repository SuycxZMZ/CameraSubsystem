# Camera 推流与 AI 基座子系统架构设计文档

**文档版本:** v0.3 (工业级完善版)  
**目标平台:** Linux / Debian @ RK3576 (预留 Android 迁移)  
**开发语言:** C/C++ 混合 (数据层 C POD, 框架层 C++17)  
**核心方向:** Camera / V4L2 -> Zero-Copy Pub/Sub -> NPU / AI  
**文档作者:** 架构设计团队  
**创建日期:** 2026-01-27  
**最后更新:** 2026-02-03

---

## 1. 设计概述

### 1.1 核心目标

本系统旨在构建一个高性能、低延迟、可扩展的 Camera 数据流基座,作为 AI 推理、视频编码、预览显示等上层应用的统一数据来源。

**工业级性能目标:**

- 基于 DMA-BUF 和共享内存实现零拷贝传输,避免 CPU 负担沉重的内存拷贝操作
- 端到端延迟控制在 10ms 以内 (4K@30fps)
- 支持 4K@60fps 高帧率稳定采集与分发
- 内存占用优化,每个 Camera 实例内存开销 < 100MB

**并发能力:**

- 支持多 Camera 实例并行采集 (不设架构层硬上限,路数由平台能力与资源预算决定,可达 12/16 路或更多)
- 支持多订阅者 (AI、编码、录制) 同时消费同一帧数据 (订阅者数量由配置与资源预算决定,通过 QoS 策略保障关键路径)
- 线程池动态扩展,支持 CPU 核心数自适应

**工业级稳定性:**

- 严格的 RAII 资源管理,杜绝资源泄漏
- 明确的线程生命周期控制,避免死锁和竞态条件
- 完善的错误处理与恢复机制,支持设备热插拔
- 7x24 小时稳定运行,MTBF > 10000 小时

**可维护性与可扩展性:**

- 跨平台抽象,核心逻辑与平台 API (Linux V4L2/Android HAL) 解耦
- 模块化设计,支持插件式扩展
- 完善的日志和监控体系

### 1.2 设计原则

**接口隔离原则:**

- 上层业务仅依赖抽象接口,不感知底层硬件细节
- 平台相关操作完全封装在 PlatformLayer 中

**数据驱动设计:**

- 以 FrameHandle 为核心数据载体
- 通过发布订阅模式解耦生产者和消费者

**线程安全设计:**

- 所有共享资源访问受锁保护
- 关键路径使用无锁结构 (如原子操作) 优化
- 明确的线程所有权和生命周期管理

**资源所有权明确:**

- 通过引用计数或句柄生命周期明确 Buffer 归属权
- 防止内存泄漏或悬空指针
- 使用智能指针管理对象生命周期

**单一职责原则:**

- 每个模块职责单一明确
- 高内聚低耦合

### 1.3 适用范围

**包含:**

- Camera 设备管理 (设备发现、打开、配置、关闭)
- V4L2 流控 (格式协商、Buffer 管理、流控制)
- 帧数据封装 (FrameHandle 构建、元数据管理)
- 进程内高性能分发 (发布订阅、任务队列、线程池)
- 线程池管理 (任务调度、负载均衡)
- 平台抽象层 (Epoll、Thread、Log 跨平台封装)

**不包含:**

- 具体的 AI 算法实现
- 音视频编码协议实现 (如 H.264/H.265 编码器内部逻辑)
- 跨进程通信 (IPC) 机制
- UI 渲染逻辑
- 网络传输协议

---

## 2. 总体架构设计

### 2.1 系统分层视图

系统采用分层架构,自底向上分为平台抽象层、数据源层、分发总线层和应用业务层。

```text
┌───────────────────────────────────────────────────────────────────────┐
│                      Application Layer (AI/App)                       │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────────────┐   │
│  │  AI Inference  │  │ Video Encoder  │  │ Debug/Record/Display   │   │
│  │  (Subscriber)  │  │  (Subscriber)  │  │  (Subscriber)          │   │
│  └───────┬────────┘  └───────┬────────┘  └──────────┬─────────────┘   │
└──────────┼───────────────────┼──────────────────────┼─────────────────┘
           │ (OnFrame)         │ (OnFrame)            │ (OnFrame)
┌──────────┼───────────────────┼──────────────────────┼─────────────────┐
│          v                   v                      v                 │
│                   Frame Broker Layer (Core)                           │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │                    FrameBroker (Manager)                        │  │
│  │  - Subscription Registry (Weak Ptr)                             │  │
│  │  - Task Dispatcher & Priority Queue                             │  │
│  │  - Worker Thread Pool                                           │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────────┘
                                        ↑
                                        │ (Publish)
┌───────────────────────────────────────────────────────────────────────┐
│                      Camera Source Layer                              │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │                    CameraSource                                 │  │
│  │  - V4L2 Device Control (Open/Start/Stop)                        │  │
│  │  - Buffer Pool Management (DMA-BUF/mmap)                        │  │
│  │  - Capture Thread (Epoll Wait)                                  │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────────┘
                                        ↑
                                        │ (Ioctl / Mmap)
┌───────────────────────────────────────────────────────────────────────┐
│                     Platform Abstraction Layer                        │
│  ┌────────────────┐  ┌────────────────┐  ┌───────────────────────┐    │
│  │  PlatformEpoll │  │ PlatformThread │  │  PlatformLogger       │    │
│  └────────────────┘  └────────────────┘  └───────────────────────┘    │
└───────────────────────────────────────────────────────────────────────┘
```

### 2.2 核心组件职责

| 组件名称 | 职责描述 | 关键特性 |
|---------|---------|---------|
| CameraSource | 负责 Camera 设备的初始化、参数配置、流控及原始帧数据的捕获 | 多实例支持 (路数上限由平台能力决定,常见 12/16+ 路)、V4L2 完整封装、Buffer 预分配与复用、设备热插拔支持 |
| FrameBroker | 系统中枢,管理订阅关系,将帧数据分发给所有订阅者 | 线程池调度 (动态扩展)、弱引用管理 (防止内存泄漏)、优先级队列 (支持 QoS)、订阅过滤 (按 Camera ID) |
| FrameHandle | 帧数据的元数据描述符,包含内存句柄而非数据指针 | C 风格 POD 结构、支持 Stride 和多平面格式、零拷贝传输、跨语言兼容 |
| PlatformLayer | 封装操作系统相关接口 (Epoll, Thread, Log) | 跨平台适配 (Linux/Android)、统一错误码、资源抽象 |

### 2.3 数据流向图

```text
Camera Hardware -> V4L2 Driver -> CameraSource -> FrameBroker -> Subscribers
     (DQBUF)       (DMA-BUF)      (FrameHandle)  (Dispatch)     (OnFrame)
```

**Buffer 生命周期:**

1. CameraSource 从 V4L2 驱动获取 Buffer
2. 封装为 FrameHandle,包含 Buffer 引用
3. 通过 FrameBroker 分发给所有订阅者
4. 订阅者处理完毕后,自动触发 Buffer 归还
5. CameraSource 将 Buffer 重新入队到 V4L2 驱动

---

## 3. 架构设计深化（Buffer 生命周期与背压策略）

### 3.0 架构评审意见

**评审日期:** 2026-02-02  
**评审人:** 高级架构师团队

#### 3.0.1 评审总述

经过深入的架构评审，系统整体设计合理，但在以下方面存在改进空间：

| 评审项 | 评审意见 | 优先级 | 状态 |
|--------|---------|--------|------|
| Buffer生命周期管理 | BufferPool与FrameHandle绑定关系不清晰，缺乏状态机管理和泄漏检测 | 高 | 已完成 |
| 背压策略配置性 | 丢帧策略硬编码，缺乏动态调整和阈值配置 | 高 | 待处理 |
| 错误恢复机制 | 设备断开后缺乏自动重连和降级策略 | 中 | 待处理 |
| 线程亲和性配置 | 无法配置线程CPU亲和性和大小核分离 | 中 | 待处理 |
| 可观测性 | 缺乏统一的Metrics收集和实时监控接口 | 中 | 待处理 |
| 内存管理策略 | 缺乏内存预算控制和压力检测 | 中 | 待处理 |
| 跨平台抽象 | Android HAL层未实现，平台能力探测不完整 | 低 | 待处理 |

#### 3.0.2 详细评审意见

##### A. Buffer生命周期管理

**[ARCH-001] Buffer所有权不明确**

- **问题**: FrameHandle与Buffer的引用关系模糊，容易导致悬空指针
- **影响**: 可能导致内存泄漏或崩溃
- **落地方案**: 引入BufferGuard类，使用RAII明确所有权
- **代码位置**: `include/camera_subsystem/core/buffer_guard.h`
- **见代码**: `src/camera/camera_source.cpp`, `src/broker/frame_broker.cpp`

**[ARCH-002] 缺少Buffer状态机**

- **问题**: Buffer状态流转不清晰，难以追踪生命周期
- **落地方案**: 实现Buffer状态枚举：Free → InUse → InFlight → Free
- **代码位置**: `include/camera_subsystem/core/buffer_state.h`

**[ARCH-003] Buffer泄漏检测**

- **问题**: 长时间运行可能存在Buffer泄漏
- **落地方案**: 添加Buffer泄漏检测和告警机制
- **代码位置**: `include/camera_subsystem/core/buffer_pool.h`

##### B. 背压策略配置性

**[ARCH-004] 丢帧策略硬编码**

- **问题**: DropNewest/DropOldest等策略固定，无法动态调整
- **建议**: 引入BackpressurePolicy配置类，支持运行时策略切换
- **代码位置**: `include/camera_subsystem/broker/backpressure_policy.h` (待创建)
- **见代码**: `src/broker/frame_broker.cpp` - 需要集成策略配置

**[ARCH-005] 缺少背压阈值配置**

- **问题**: 队列大小和延迟阈值固定
- **建议**: 添加动态阈值调整算法
- **代码位置**: `include/camera_subsystem/broker/broker_config.h` (待创建)

**[ARCH-006] 订阅者优先级静态**

- **问题**: 优先级在注册时固定，无法动态调整
- **建议**: 实现自适应优先级调整机制
- **代码位置**: `src/broker/frame_broker.cpp` - 需要添加动态优先级调整

##### C. 错误恢复机制

**[ARCH-007] 缺少设备监控和自动重连**

- **问题**: 设备断开后需要手动重启
- **建议**: 引入DeviceMonitor类，实现自动重连和降级策略
- **代码位置**: `include/camera_subsystem/camera/device_monitor.h` (待创建)

**[ARCH-008] 缺少降级策略**

- **问题**: 高负载时无法自动降级
- **建议**: 实现自动降级（降低帧率/分辨率）
- **代码位置**: `include/camera_subsystem/camera/degradation_policy.h` (待创建)

##### D. 线程亲和性配置

**[ARCH-009] 缺少CPU亲和性配置**

- **问题**: 无法配置线程CPU亲和性
- **建议**: 引入ThreadAffinity配置类
- **代码位置**: `include/camera_subsystem/platform/thread_affinity.h` (待创建)

**[ARCH-010] 缺少大小核分离调度**

- **问题**: Worker线程无法区分大小核
- **建议**: 实现CPU拓扑探测和大小核分离调度
- **代码位置**: `include/camera_subsystem/platform/cpu_topology.h` (待创建)

##### E. 可观测性

**[ARCH-011] 缺少Metrics收集接口**

- **问题**: 无法统一收集性能指标
- **建议**: 引入MetricsCollector类，支持Prometheus/Grafana
- **代码位置**: `include/camera_subsystem/metrics/metrics_collector.h` (待创建)

**[ARCH-012] 缺少实时监控接口**

- **问题**: 无法实时监控系统状态
- **建议**: 实现SystemMonitor类，提供REST/gRPC接口
- **代码位置**: `include/camera_subsystem/metrics/system_monitor.h` (待创建)

##### F. 内存管理策略

**[ARCH-013] 缺少内存预算控制**

- **问题**: 内存使用无法限制
- **建议**: 引入MemoryBudget配置类
- **代码位置**: `include/camera_subsystem/core/memory_budget.h` (待创建)

**[ARCH-014] 缺少内存压力检测**

- **问题**: 内存压力无法感知
- **建议**: 实现MemoryMonitor类，支持压力告警
- **代码位置**: `include/camera_subsystem/core/memory_monitor.h` (待创建)

##### G. 跨平台抽象

**[ARCH-015] 缺少平台能力探测**

- **问题**: 无法探测平台能力
- **建议**: 引入PlatformCapabilities接口
- **代码位置**: `include/camera_subsystem/platform/platform_capabilities.h` (待创建)

**[ARCH-016] Android HAL层未实现**

- **问题**: 无法适配Android平台
- **建议**: 预留HAL3接口设计
- **代码位置**: `include/camera_subsystem/platform/android/hal3_wrapper.h` (待创建)

---

### 3.1 Buffer 生命周期与复用池

针对“统一 Buffer 生命周期与复用池”和“背压与丢帧策略”给出明确架构设计，确保在边缘设备上稳定、低延迟运行。

**目标**

1. 全链路零拷贝，避免在热路径中分配与复制。
2. 明确 Buffer 所有权，避免悬空指针与资源泄漏。
3. 支持多平面格式与 DMA-BUF 的可扩展设计。

**核心设计**

1. `BufferPool` 统一管理 Buffer，使用预分配与复用策略。
2. `FrameHandle` 仅持有 Buffer 的引用（或句柄），不拥有内存。
3. Buffer 状态机：`Free -> InUse -> InFlight -> Free`。
4. 当前实现为 **拷贝模式**（V4L2 MMAP -> BufferPool）。
5. 后续升级为 **DMA-BUF 零拷贝**，避免拷贝成本。

**生命周期流程**

1. CameraSource 从 `BufferPool` 申请可用 Buffer。
2. V4L2 `DQBUF` 后拷贝到池中，并封装为 `FrameHandle` 分发。
3. FrameBroker 仅复制 `FrameHandle` 的元数据与引用，不复制数据。
4. 当最后一个订阅者释放 Buffer 引用时，自动归还 `BufferPool`。
5. CameraSource 将驱动 Buffer `QBUF` 回驱动，进入下一轮采集。

**线程安全与性能**

1. `BufferPool` 内部使用 MPMC 队列或互斥锁保护。
2. Buffer 归还采用 RAII（智能指针 + 自定义 deleter）。
3. 统计指标：池深度、可用数量、复用次数、丢帧数。

**扩展规划**

1. 多平面 Buffer：记录 plane offsets/stride/size。
2. DMA-BUF：记录 fd 与 plane fd，支持零拷贝传给 NPU/编码器。
3. 

### 3.2 背压与丢帧策略

**目标**
1. 在高负载下保证关键路径稳定（实时性优先）。
2. 避免队列无限增长导致内存抖动与延迟失控。
3. 为不同订阅者提供差异化 QoS。

**策略分层**

1. **采集层（CameraSource）**
   - 当 `BufferPool` 耗尽时优先丢弃最新帧，避免阻塞采集线程。
2. **分发层（FrameBroker）**
   - 基于优先级的队列上限。
   - 超过阈值时执行丢帧策略。
3. **订阅者层（Subscriber）**
   - 对低优先级订阅者可以“只保留最新帧”。

**当前落地**

1. 采集层池耗尽丢帧
2. Broker 队列上限丢帧

**丢帧策略（可配置）**

1. `DropNewest`：实时性优先，保持较低延迟。
2. `DropOldest`：吞吐优先，尽量保留最新帧。
3. `DropByPriority`：低优先级先丢，高优先级保留。

**背压触发条件**

1. 队列长度超过 `max_queue_size`。
2. 端到端延迟超过 `max_latency_ms`。
3. 连续处理耗时超出目标帧间隔。

**可观测性**

1. 丢帧数、队列深度、FPS、平均/95P 延迟。
2. 按订阅者统计（便于定位慢消费者）。

---

## 4. 核心数据结构设计

数据结构设计遵循 POD (Plain Old Data) 原则,确保在 C/C++ 边界及共享内存中安全传递。

### 4.1 帧句柄 (FrameHandle)

FrameHandle 是帧数据的唯一凭证,包含所有渲染和处理所需的描述信息。

```cpp
// 像素格式枚举
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

// 内存类型枚举
enum class MemoryType : uint32_t
{
    kMmap = 0,   // V4L2 MMAP buffer
    kDmaBuf,     // DMA-BUF file descriptor
    kShm,        // Shared memory
    kHeap        // User space heap memory
};

// 帧句柄结构 (C-Style POD)
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

---

## 4. 开发与调试指南（Ubuntu + V4L2）

本项目当前可在 Ubuntu 上直接使用 V4L2 采集进行调试（RK3576 交叉编译暂未启用）。

### 4.1 构建

```bash
./scripts/build.sh
```

### 4.2 摄像头权限

```bash
sudo usermod -aG video $USER

# 重新登录后生效

```

或临时使用 sudo 运行压测程序。

### 4.3 压测程序（非 GTest）

所有压测可执行文件输出到项目根目录 `bin/` 下：

```bash

# PlatformLayer 压测

./bin/platform_stress_test 5

# FrameBroker 压测

./bin/frame_broker_stress_test 5

# CameraSource 压测（默认 20 秒）

sudo ./bin/camera_source_stress_test 20 /dev/video0
```

### 4.4 CameraSource 压测说明

- 每秒输出一行表格日志（sec / frames / fps / dispatched / dropped / queue / image）
- 每秒保存一张图片到 `stress_frames/`
- 最多保留 10 张，编号 `frame_0` ~ `frame_9`，循环覆盖
- 保存格式：
  - MJPEG → `.jpg`
  - RGB/RGBA → `.ppm`
  - NV12/YUYV/H264/H265 → `.pgm`（灰度或原始 Y 平面）

### 4.5 代码格式化

```bash

# 全量格式化（排除 third_party）

./scripts/format.sh

# 仅格式化 Git 变更文件

./scripts/format.sh changed
```

### 4.6 边缘设备与交叉编译（RK3576 / Debian）

当前阶段优先在 Ubuntu 上使用 V4L2 调试。面向 RK3576 的交叉编译计划如下：

- 规划 CMake Toolchain 文件（例如 `cmake/toolchains/rk3576.cmake`）
- 使用 aarch64 交叉编译器与 sysroot（由 SDK 提供）
- 设备侧依赖：V4L2、pthread、libdrm（后续补充）
- CI 增加交叉编译与最小运行时包产物

### 4.7 架构完善与待优化项

- 零拷贝主链路完善（DMABUF / 多平面）
- 订阅者背压与丢帧策略（按优先级/延迟阈值）
- 统一 Buffer 生命周期与复用池
- 采集/分发线程亲和性与调度策略
- 指标与观测性（FPS、延迟、队列深度）

**设计要点:**

- POD 结构,可在 C/C++ 边界安全传递
- 支持多平面格式 (如 NV12)
- 包含 Stride 信息,处理对齐和 Padding
- 预留扩展空间,便于向后兼容

### 3.2 订阅者接口 (IFrameSubscriber)

上层应用 (AI、编码等) 需实现此接口以接收帧数据。

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

### 3.3 Camera 配置结构 (CameraConfig)

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

---

## 4. 详细模块设计

### 4.1 Camera Source Layer

负责与 V4L2 驱动交互,管理硬件 Buffer。

**类名:** CameraSource

**关键流程:**

1. **初始化流程:**
   - 打开 /dev/videoX 设备
   - 查询并设置采集格式 (VIDIOC_S_FMT)
   - 申请 Buffer (VIDIOC_REQBUFS)
   - 映射 Buffer 到用户空间或导入 DMA-BUF
   - 通过 VIDIOC_QBUF 将所有 Buffer 入队
   - 创建 Epoll 实例并注册设备 fd

2. **采集流程:**
   - EpollWait 监听 POLLPRI 事件
   - 触发后调用 VIDIOC_DQBUF 获取填充好的 Buffer
   - 将 V4L2 v4l2_buffer 信息转换为 FrameHandle
   - 调用 frame_callback_ (即 FrameBroker::Publish)
   - Buffer 不立即归还,等待引用计数归零

3. **停止流程:**
   - 停止 Epoll 监听
   - 停止采集 (VIDIOC_STREAMOFF)
   - 等待所有 Buffer 归还
   - 释放 Buffer 资源
   - 关闭设备 fd

4. **错误处理:**
   - 设备断开: 检测到错误后自动停止采集
   - DQBUF 超时: 记录日志,继续尝试
   - Buffer 耗尽: 记录警告,等待 Buffer 归还

### 4.2 Frame Broker Layer

系统的核心调度中心,负责解耦生产者和消费者。

**类名:** FrameBroker

**分发策略:**

1. CameraSource 捕获帧后调用 Publish
2. Publish 将 FrameHandle 包装为 DispatchTask 推入 task_queue_
3. WorkerThread 从队列取出任务
4. 根据 frame.camera_id_ 查找 subscription_map_
5. 遍历订阅者列表,尝试 lock() 弱引用
   - 成功: 调用 OnFrame
   - 失败: 从列表中移除
6. 所有订阅者处理完毕后,Buffer 引用计数归零,自动归还

**优先级调度:**

- 订阅者可指定优先级 (0-255)
- 任务队列按优先级排序
- 高优先级任务优先处理
- 相同优先级按 FIFO 顺序处理

### 4.3 Platform Abstraction Layer

封装 OS 相关操作,提供统一接口。

**类名示例:** PlatformThread, PlatformEpoll, PlatformLogger, PlatformMutex

---

## 5. 线程模型与并发控制

### 5.1 线程角色划分

**Capture Thread (CameraSource 内部):**

- **职责:** 阻塞等待 V4L2 设备事件,执行 DQBUF 获取帧数据,封装 FrameHandle,投递任务到 FrameBroker
- **优先级:** 高优先级 (SCHED_FIFO 或 nice -20)
- **数量:** 每个 CameraSource 一个线程
- **CPU 亲和性:** 绑定到独立 CPU 核心 (可选)

**Worker Threads (FrameBroker 持有):**

- **职责:** 从任务队列获取帧,查找订阅者列表,执行订阅者 OnFrame 回调
- **优先级:** 中等优先级 (默认)
- **数量:** 可配置,建议等于 CPU 核心数或核心数-1
- **模型:** 典型的 Producer-Consumer 模型
- **负载均衡:** 任务队列自动分配

### 5.2 同步与锁策略

**任务队列:**

- 实现方式: std::mutex + std::condition_variable
- 无锁优化: 考虑使用 boost::lockfree::queue 或基于 std::atomic 的环形缓冲区
- 容量限制: 防止内存溢出,建议最大容量 1000 个任务
- 满队列策略: 丢弃最旧的任务或阻塞等待

**订阅表:**
- 读操作 (分发时) 远多于写操作 (注册/注销)
- 使用 std::shared_mutex
- 分发时获取读锁 (shared_lock)
- 更新订阅表时获取写锁 (unique_lock)
- 定期清理失效订阅者 (弱引用 lock 失败)

**Buffer 管理:**
- 使用 std::shared_ptr 管理引用计数
- 原子操作保证线程安全
- 引用计数归零时触发归还

**统计信息:**
- 使用 std::mutex 保护
- 原子变量用于高频更新 (如帧计数)

### 5.3 死锁预防

**设计原则:**
- 所有锁的获取顺序固定
- 避免嵌套锁
- 使用 RAII 自动释放锁
- 设置锁超时机制 (可选)

**锁顺序:**
1. subscription_map_mutex_ (读写锁)
2. task_queue_mutex_
3. stats_mutex_

**避免在持有锁时:**
- 调用用户回调 (OnFrame)
- 执行耗时操作
- 等待其他资源

### 5.4 多 Camera 扩展策略 (平台能力驱动)

- 架构层不设置固定路数上限,实际可用路数由平台能力探测结果与运行时资源预算共同决定
- 将“最大路数”定义为能力模型的输出,而不是常量;能力模型至少考虑 ISP/V4L2 带宽、内存带宽、CPU/NPU 负载与热设计约束
- 在高负载下采用明确的退化策略以保持系统可用性,例如降低分辨率、降低帧率、限制非关键订阅者或按优先级丢帧
- 文档中的并发路数与订阅者数量使用“示例/压测目标”表述,以平台实测数据为准

### 5.5 能力探测与建议上限落地流程 (工程可执行)

推荐在系统启动阶段引入“能力探测 -> 建议上限 -> 默认配置”的闭环:

1. 通过 PlatformLayer 的能力探测接口获取 `PlatformCapabilities`
2. 根据业务目标构造 `StreamProfile` (目标分辨率/帧率/格式)
3. 根据部署形态设定 `ResourceBudget` (内存/CPU/NPU/热余量/安全系数)
4. 调用 `SuggestLimits(...)` 计算建议路数、建议订阅者上限与建议线程数
5. 将建议值作为默认配置与压测目标,并保留手动覆盖能力

```cpp
// 1) 平台能力探测 (失败时可降级为配置文件或默认值)
PlatformCapabilities caps{};
if (!capability_probe->Probe(&caps))
{
    caps = LoadCapabilitiesFromConfigOrDefault();
}

// 2) 流画像 (以“单路目标规格”为基准)
StreamProfile profile{
    .width_ = 1920,
    .height_ = 1080,
    .fps_ = 30,
    .format_ = PixelFormat::kNV12,
};

// 3) 资源预算 (建议由产品形态或部署配置给出)
ResourceBudget budget{
    .memory_budget_mb_ = 1024,
    .cpu_budget_percent_ = 60,
    .npu_budget_percent_ = 70,
    .thermal_headroom_percent_ = 20,
    .safety_margin_percent_ = 20,
};

// 4) 建议上限计算
SuggestedLimits limits = SuggestLimits(caps, profile, budget);

// 5) 用建议值驱动默认配置 (仍允许上层覆盖)
broker->Start(limits.suggested_worker_threads_);
camera_manager->SetSuggestedCameraCount(limits.suggested_camera_count_);
```

---

## 6. 生命周期与资源管理

### 6.1 启动流程

```cpp
1. 初始化日志系统
   PlatformLogger::Initialize("camera.log", LogLevel::kInfo);

2. 创建 FrameBroker 实例
   auto broker = std::make_unique<FrameBroker>();

3. 启动 Broker Worker 线程池
   broker->Start(4);  // 4 个 Worker 线程

4. 创建 CameraSource 实例
   auto camera = std::make_unique<CameraSource>(0);  // Camera ID 0

5. 设置 Camera 配置
   CameraConfig config;
   config.width_ = 1920;
   config.height_ = 1080;
   config.format_ = PixelFormat::kNV12;
   config.fps_ = 30;
   config.buffer_count_ = 4;
   config.io_method_ = IO_METHOD_DMABUF;

6. 初始化 Camera
   camera->Initialize(config);

7. 设置帧回调为 Broker::Publish
   camera->SetFrameCallback(
       [broker_ptr = broker.get()](const FrameHandle& frame)
       {
           broker_ptr->Publish(frame);
       }
   );

8. 上层创建 Subscriber (如 AI 模块)
   class AISubscriber : public IFrameSubscriber
   {
   public:
       void OnFrame(const FrameHandle& frame) override
       {
           // AI 推理逻辑
       }

       const char* GetSubscriberName() const override
       {
           return "AISubscriber";
       }
   };
   auto ai_subscriber = std::make_shared<AISubscriber>();

9. 注册订阅者
   broker->Subscribe(0, ai_subscriber);  // 订阅 Camera 0

10. 开始采集
    camera->StartStreaming();
```

### 6.2 关闭流程

为避免死锁和 UAF (Use-After-Free),必须严格遵守以下顺序:

1. **停止生产**

   ```cpp
   camera->StopStreaming();
   // 此时不再有新帧产生
   ```

2. **停止分发**

   ```cpp
   broker->Stop();
   // 设置 is_running_ = false
   // 唤醒所有 Worker 线程
   ```

3. **等待任务处理完成**

   ```cpp
   broker->Join();
   // 等待所有 Worker 线程安全退出
   // 清空任务队列 (或处理完剩余任务)
   ```

4. **注销订阅** (可选,依赖弱引用可自动处理)

   ```cpp
   broker->Unsubscribe(ai_subscriber);
   ```

5. **销毁对象**

   ```cpp
   ai_subscriber.reset();
   camera.reset();
   broker.reset();
   ```

6. **关闭日志系统**

   ```cpp
   PlatformLogger::Shutdown();
   ```

### 6.3 资源释放策略

**Buffer 归还机制:**

- 采用引用计数 std::shared_ptr 管理 Buffer 生命周期
- FrameHandle 内部包含一个 shared_ptr<BufferGuard>
- BufferGuard 析构函数自动触发 VIDIOC_QBUF
- 当所有持有该帧的 Subscriber 处理完毕 (shared_ptr 析构)
- Buffer 自动归还给驱动

**RAII 资源管理:**

- 所有资源封装在 RAII 类中
- 构造函数获取资源,析构函数释放资源
- 使用智能指针管理对象生命周期
- 避免手动 new/delete

**异常安全:**

- 使用 try-catch 捕获异常
- 异常发生时自动清理资源
- 不抛出异常的接口标记为 noexcept
- 关键路径避免使用异常

### 6.4 错误恢复

**设备断开恢复:**

- 检测到设备错误后自动停止采集
- 关闭设备 fd
- 等待设备重新连接 (轮询或 inotify 监听)
- 重新初始化设备
- 恢复采集

**采集错误恢复:**

- DQBUF 失败: 记录日志,继续尝试
- Buffer 耗尽: 等待 Buffer 归还
- Epoll 错误: 重新创建 Epoll 实例

---

## 7. 错误处理与日志

### 7.1 错误码规范

定义统一的错误码枚举,避免直接使用数字或 errno。

```cpp
enum class ErrorCode : int
{
    kOk = 0,

    // 参数错误
    kErrorInvalidArgument = 100,
    kErrorNullPointer,
    kErrorOutOfRange,

    // 设备错误
    kErrorDeviceNotFound = 200,
    kErrorDeviceBusy,
    kErrorDeviceDisconnected,
    kErrorPermissionDenied,

    // IO 操作错误
    kErrorIoctlFailed = 300,
    kErrorMmapFailed,
    kErrorDmaBufFailed,
    kErrorReadFailed,
    kErrorWriteFailed,

    // 资源错误
    kErrorMemoryAlloc = 400,
    kErrorOutOfMemory,
    kErrorResourceExhausted,

    // 线程错误
    kErrorThreadStartFailed = 500,
    kErrorThreadJoinFailed,
    kErrorTimeout,

    // 状态错误
    kErrorInvalidState = 600,
    kErrorNotInitialized,
    kErrorAlreadyStarted,
    kErrorAlreadyStopped,

    // 未知错误
    kErrorUnknown = 999
};

/**
 * @brief 获取错误码对应的描述信息
 * @param code 错误码
 * @return 错误描述字符串
 */
const char* GetErrorString(ErrorCode code);
```

### 7.2 日志规范

使用 spdlog 封装,支持按模块分级。

**Logger 命名:**

- camera_source: CameraSource 模块
- frame_broker: FrameBroker 模块
- platform: PlatformLayer 模块
- main: 主程序

**格式:**

```text
[Timestamp] [Thread] [Level] [Module] Message
```

**示例:**

```text
[2026-01-27 12:34:56.789] [12345] [INFO]  [camera_source] Camera 0 initialized
[2026-01-27 12:34:56.790] [12346] [ERROR] [frame_broker] Failed to dispatch frame: invalid subscriber
```

**日志级别:**

- TRACE: 最详细的调试信息
- DEBUG: 调试信息
- INFO: 一般信息
- WARNING: 警告信息
- ERROR: 错误信息
- CRITICAL: 严重错误,可能导致系统崩溃

**日志策略:**

- **热路径 (OnFrame 回调内):**
  - 默认不打印 INFO 日志
  - 仅打印 ERROR 日志
  - 使用异步日志避免阻塞
- **控制路径:**
  - 打印 INFO/WARN 日志
  - 关键操作记录 INFO
  - 异常情况记录 WARN/ERROR
- **启动/关闭:**
  - 详细记录 INFO 日志
  - 方便排查问题

**日志文件:**

- 按日期滚动 (每天一个文件)
- 单个文件大小限制 (如 100MB)
- 保留最近 N 天的日志 (如 7 天)
- 支持日志压缩 (可选)

---

## 8. 编码规范与命名约定

根据工业级标准,本项目严格遵循以下 C++ 编码规范:
**参考:** Google C++ Style Guide

### 8.1 格式与排版

**缩进:**

- 统一使用 4 个空格
- 禁止使用 Tab 字符
- 编辑器配置: expandtab, tabstop=4, shiftwidth=4

**大括号:**

- 左大括号 { 和右大括号 } 独立占一行

```cpp
if (condition)
{
    DoSomething();
}
else
{
    DoOtherThing();
}

class MyClass
{
public:
    void MyMethod();
};
```

**行宽:**

- 建议不超过 100 字符
- 必要时可适当放宽,但不超过 120 字符
- 长表达式适当换行

**空行:**

- 函数之间空 2 行
- 逻辑块之间空 1 行
- 类定义结束后空 1 行

**空格:**

- 操作符前后加空格 (a = b + c)
- 逗号后加空格 (function(a, b, c))
- 括号内不加空格 (if (condition))
- 指针/引用: 类型与 */& 之间加空格 (int* ptr, int& ref)

### 8.2 命名规则

| 类型 | 命名风格 | 示例 |
|------|---------|------|
| 类名 | 大驼峰 (PascalCase) | class CameraSource, class FrameBroker |
| 结构体 | 大驼峰 (PascalCase) | struct FrameHandle, struct CameraConfig |
| 函数/方法 | 大驼峰 (PascalCase) | void StartStreaming(), bool Publish() |
| 成员变量 | 小写 + 下划线后缀 | int device_fd_, bool is_running_ |
| 局部变量 | 小写 + 下划线 | int frame_count, void* buffer_ptr |
| 全局变量 | g_ + 前缀 + 下划线后缀 | int g_system_instance_count_ |
| 常量/宏 | 全大写 + 下划线 | const int kMaxBufferCount = 4 |
| 枚举类 | 大驼峰 (PascalCase) | enum class PixelFormat { kNV12, kRGB } |
| 枚举值 | 小驼峰 + k 前缀 | kNV12, kRGB888 |
| 命名空间 | 全小写 + 下划线 | namespace camera_subsystem |
| 文件名 | 全小写 + 下划线 | camera_source.h, frame_broker.cpp |

### 8.3 代码示例

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

// 全局变量示例
int g_active_broker_count_ = 0;
const int kMaxWorkerThreads = 16;
```

### 8.4 注释规范

**文件头注释:**

```cpp
/**
 * @file camera_source.h
 * @brief Camera 数据源模块,负责 V4L2 设备管理和帧数据采集
 * @author CameraSubsystem Team
 * @date 2026-01-27
 */
```

**类注释:**

```cpp
/**
 * @class CameraSource
 * @brief Camera 数据源类,封装 V4L2 设备操作
 *
 * 该类负责 Camera 设备的初始化、配置、流控制和帧数据采集。
 * 支持多实例并行运行,每个实例对应一个 Camera 设备。
 */
```

**函数注释:**

```cpp
/**
 * @brief 初始化 Camera 设备
 * @param config Camera 配置参数
 * @return 成功返回 ErrorCode::kOk,失败返回对应错误码
 *
 * 该函数完成以下操作:
 * 1. 打开 V4L2 设备
 * 2. 查询并设置采集格式
 * 3. 申请并映射 Buffer
 * 4. 创建 Epoll 实例
 *
 * @note 调用此函数前,设备必须未被初始化
 * @warning 不要在多线程中同时调用此函数
 */
ErrorCode Initialize(const CameraConfig& config);
```

---

## 9. 性能指标与测试要求

### 9.1 性能指标

**吞吐量:**

- 支持 4K@60fps 稳定采集
- 多路并发采集能力随平台扩展 (示例: 12/16 路 1080p@30fps,以平台实测与资源预算为准)
- 多订阅者同时消费能力由资源预算与 QoS 策略决定 (关键路径优先,非关键路径可退化)

**延迟:**

- 端到端延迟 < 10ms (4K@30fps)
- 采集延迟 < 2ms
- 分发延迟 < 3ms

**资源占用:**

- 单路 4K@30fps 内存占用 < 100MB
- CPU 占用率 < 30% (单核)
- 无内存泄漏 (长时间运行测试)

**稳定性:**

- 7x24 小时稳定运行
- MTBF > 10000 小时
- 支持设备热插拔

### 9.2 测试要求

**单元测试:**

- 每个模块必须有单元测试
- 测试覆盖率 > 80%
- 使用 Google Test 框架

**集成测试:**

- 端到端流程测试
- 多 Camera 并发测试
- 多订阅者测试
- 异常场景测试

**性能测试:**

- 压力测试 (高帧率、长时间运行)
- 内存泄漏检测 (Valgrind/ASan)
- CPU 性能分析 (perf)
- 延迟测试

**兼容性测试:**

- 不同 Camera 设备测试
- 不同分辨率和格式测试
- Linux 不同版本测试

---

## 10. 后续工作计划

### 10.1 详细设计文档

产出以下详细设计文档:

- CameraSource API 定义文档
- FrameBroker API 定义文档
- PlatformLayer API 定义文档
- 数据结构详细设计文档
- 错误处理机制设计文档

### 10.2 Buffer 管理机制

设计具体的 BufferPool 类:

- 处理 DMA-BUF 的导入/导出
- 实现引用计数逻辑
- 支持 Buffer 复用
- 监控 Buffer 使用情况

### 10.3 性能测试

设计压力测试用例:

- 4K@60fps 长时间运行测试 (24 小时)
- 多路并发测试 (按平台能力阶梯压测,如 12/16+ 路 1080p@30fps)
- 多订阅者测试 (按资源预算压测,验证 QoS 与退化策略)
- CPU 占用率和延迟测试

### 10.4 Android HAL 适配

实现 PlatformLayer 的 Android 版本:

- 对接 Camera HAL3
- 适配 Android Binder IPC
- 支持 Android Log 系统
- 兼容 Android NDK

### 10.5 监控与诊断

实现监控和诊断功能:

- 性能指标采集
- 实时监控接口
- 诊断日志
- 崩溃堆栈分析

### 10.6 文档完善

完善以下文档:

- 用户手册
- 开发者指南
- API 参考文档
- 部署指南
- 故障排查指南

---

## 11. 附录

### 11.1 术语表

- **DMA-BUF:** Direct Memory Access Buffer, 零拷贝内存共享机制
- **V4L2:** Video for Linux 2, Linux 视频设备驱动接口
- **POD:** Plain Old Data, 简单数据结构
- **RAII:** Resource Acquisition Is Initialization, 资源获取即初始化
- **MTBF:** Mean Time Between Failures, 平均故障间隔时间

### 11.2 参考资料

- V4L2 API Documentation: https://www.kernel.org/doc/html/latest/userspace-api/media/v4l/v4l2.html
- DMA-BUF Documentation: https://www.kernel.org/doc/html/latest/driver-api/dma-buf.html
- Google C++ Style Guide: https://google.github.io/styleguide/cppguide.html
- RK3576 SoC Datasheet

### 11.3 版本历史

**v0.3 (2026-01-27):**

- 完善架构设计,补充工业级规范
- 添加性能指标和测试要求
- 完善错误处理和日志规范
- 添加生命周期管理详细说明

**v0.2 (2026-01-26):**

- 初步架构设计
- 核心组件定义
- 基本数据结构设计

**v0.1 (2026-01-25):**

- 文档创建
- 需求分析

---

**文档结束**
