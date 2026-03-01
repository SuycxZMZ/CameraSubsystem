# CameraSubsystem 项目概览

**最后更新:** 2026-02-03

## 项目简介

CameraSubsystem 是一个高性能、低延迟、可扩展的 Camera 数据流基座，专为 RK3576 平台设计（预留 Android 迁移）。它作为 AI 推理、视频编码、预览显示等上层应用的统一数据来源，提供零拷贝的数据传输能力。

## 核心特性

### 性能特性

- **零拷贝传输**: 基于 DMA-BUF 和共享内存，避免 CPU 密集的内存拷贝操作
- **低延迟**: 端到端延迟控制在 10ms 以内 (4K@30fps)
- **高吞吐**: 支持 4K@60fps 高帧率稳定采集与分发
- **低内存占用**: 单路 4K@30fps 内存占用 < 100MB

### 并发能力

- **多 Camera 支持**: 支持多路 Camera 并行采集（路数由平台能力决定）
- **多订阅者**: 支持多个订阅者同时消费同一帧数据
- **线程池调度**: 动态扩展的线程池，支持 CPU 核心数自适应

### 稳定性

- **RAII 资源管理**: 严格的资源管理，杜绝资源泄漏
- **线程安全**: 完善的同步机制，避免死锁和竞态条件
- **错误恢复**: 完善的错误处理与恢复机制，支持设备热插拔
- **7x24 运行**: MTBF > 10000 小时

### 可维护性

- **跨平台抽象**: 核心逻辑与平台 API 解耦
- **模块化设计**: 支持插件式扩展
- **完善日志**: 分级日志系统，便于问题排查
- **规范编码**: 遵循工业级编码规范

## 系统架构

### 分层架构

```text
┌───────────────────────────────────────────────────────────────────────┐
│                      Application Layer (AI/App)                       │
│  ┌────────────────┐  ┌────────────────┐  ┌────────────────────────┐  │
│  │  AI Inference  │  │ Video Encoder  │  │ Debug/Record/Display   │  │
│  │  (Subscriber)  │  │  (Subscriber)  │  │  (Subscriber)          │  │
│  └───────┬────────┘  └───────┬────────┘  └──────────┬─────────────┘  │
└──────────┼──────────────────┼─────────────────────┼─────────────────┘
           │ (OnFrame)         │ (OnFrame)            │ (OnFrame)
┌──────────┼──────────────────┼─────────────────────┼─────────────────┐
│          v                  v                     v                 │
│                   Frame Broker Layer (Core)                             │
│  ┌─────────────────────────────────────────────────────────────────┐  │
│  │                    FrameBroker (Manager)                       │  │
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
│  │                    CameraSource                                  │  │
│  │  - V4L2 Device Control (Open/Start/Stop)                        │  │
│  │  - Buffer Pool Management (DMA-BUF/mmap)                        │  │
│  │  - Capture Thread (Epoll Wait)                                  │  │
│  └─────────────────────────────────────────────────────────────────┘  │
└───────────────────────────────────────────────────────────────────────┘
                                        ↑
                                        │ (Ioctl / Mmap)
┌───────────────────────────────────────────────────────────────────────┐
│                     Platform Abstraction Layer                        │
│  ┌────────────────┐  ┌────────────────┐  ┌───────────────────────┐  │
│  │  PlatformEpoll │  │ PlatformThread │  │  PlatformLogger       │  │
│  └────────────────┘  └────────────────┘  └───────────────────────┘  │
└───────────────────────────────────────────────────────────────────────┘
```

### 数据流向

```text
Camera Hardware -> V4L2 Driver -> CameraSource -> FrameBroker -> Subscribers
     (DQBUF)       (DMA-BUF)      (FrameHandle)  (Dispatch)     (OnFrame)
```

### 发布端/订阅端解耦架构调整

当前项目增加发布端/订阅端解耦演进方向，满足“唯一发布端 + 多订阅端”的业务场景：

1. 唯一发布端实例 `camera_publisher` 统一管理摄像头与分发。
2. 一个或多个订阅端实例 `camera_subscriber` 独立接入。
3. 某路 Camera 仅在存在订阅时启动采集，无订阅时停止并释放资源。

典型用户故事（开发阶段示例）：

1. 示例使用 1 个发布端 + 1 个订阅端做联调。
2. 订阅端收到一帧后 `sleep 5ms` 模拟上层处理。
3. 订阅端每 1 秒保存 1 张图片。
4. 最多保留 1 张，固定文件名覆盖。

实际场景中，可以有多个订阅端并发订阅同一路或多路 Camera。

协议协定（规划）：

1. 公共头文件：`include/camera_subsystem/ipc/camera_channel_contract.h`
2. 默认设备宏：`CAMERA_SUBSYSTEM_DEFAULT_CAMERA`（默认 `/dev/video0`）
3. 支持可扩展 Camera 类型：默认、MIPI、USB、平台私有类型。

### 核心组件

#### 1. CameraSource

- **职责**: 负责 Camera 设备的初始化、参数配置、流控及原始帧数据的捕获
- **特性**: 多实例支持、V4L2 完整封装、Buffer 预分配与复用、设备热插拔支持

#### 2. FrameBroker

- **职责**: 系统中枢，管理订阅关系，将帧数据分发给所有订阅者
- **特性**: 线程池调度、弱引用管理、优先级队列、订阅过滤

#### 3. FrameHandle

- **职责**: 帧数据的元数据描述符，包含内存句柄而非数据指针
- **特性**: C 风格 POD 结构、支持 Stride 和多平面格式、零拷贝传输

#### 4. PlatformLayer

- **职责**: 封装操作系统相关接口（Epoll, Thread, Log）
- **特性**: 跨平台适配、统一错误码、资源抽象

## 技术栈

### 开发语言

- **C++17**: 框架层，提供面向对象和现代 C++ 特性
- **C (POD)**: 数据层，确保跨语言兼容性和零拷贝传输

### 核心依赖

- **spdlog**: 高性能 C++ 日志库
- **pthread**: POSIX 线程库
- **V4L2**: Linux 视频设备驱动接口

### 测试框架

- **Google Test**: C++ 单元测试框架

### 构建工具

- **CMake**: 跨平台构建系统

## 性能指标

### 吞吐量

- 支持 4K@60fps 稳定采集
- 多路并发采集能力（示例: 12/16 路 1080p@30fps）
- 多订阅者同时消费能力

### 延迟

- 端到端延迟 < 10ms (4K@30fps)
- 采集延迟 < 2ms
- 分发延迟 < 3ms

### 资源占用

- 单路 4K@30fps 内存占用 < 100MB
- CPU 占用率 < 30% (单核)
- 无内存泄漏

### 稳定性

- 7x24 小时稳定运行
- MTBF > 10000 小时
- 支持设备热插拔

## 适用场景

### 包含的功能

- Camera 设备管理（设备发现、打开、配置、关闭）
- V4L2 流控（格式协商、Buffer 管理、流控制）
- 帧数据封装（FrameHandle 构建、元数据管理）
- 进程内高性能分发（发布订阅、任务队列、线程池）
- 线程池管理（任务调度、负载均衡）
- 平台抽象层（Epoll、Thread、Log 跨平台封装）

### 不包含的功能

- 具体的 AI 算法实现
- 音视频编码协议实现（如 H.264/H.265 编码器内部逻辑）
- 跨进程通信（IPC）机制
- UI 渲染逻辑
- 网络传输协议

## 编码规范

### 命名规范

- **类名**: 大驼峰 (PascalCase) - 例如: `CameraSource`, `FrameBroker`
- **函数名**: 大驼峰 (PascalCase) - 例如: `StartStreaming()`, `Publish()`
- **成员变量**: 小写 + 下划线后缀 - 例如: `device_fd_`, `is_running_`
- **局部变量**: 小写 + 下划线 - 例如: `frame_count`, `buffer_ptr`
- **常量**: k 前缀 + 大驼峰 - 例如: `kMaxBufferCount = 4`

### 代码格式

- **缩进**: 4 个空格
- **大括号**: 独立占一行
- **行宽**: 不超过 100 字符
- **空行**: 函数之间空 2 行，逻辑块之间空 1 行

### 注释规范

- 使用 Doxygen 风格注释
- 文件头注释：包含文件名、简介、作者、日期
- 类注释：描述类的职责和用途
- 函数注释：包含参数说明、返回值、注意事项

## 项目状态

### 当前版本

- **版本号**: v0.1
- **状态**: 开发中
- **完成度**: 约 70%

### 已完成模块

- ✅ 核心数据结构设计
- ✅ 平台抽象层实现
- ✅ 分发层实现（FrameBroker）
- ✅ 信号处理工具（utils/signal_handler）
- ✅ CameraSource（V4L2 + MMAP 采集）
- ✅ BufferPool（统一生命周期与复用池，拷贝模式）
- ✅ Buffer 生命周期治理（BufferGuard / BufferState / 泄漏检测）
- ✅ 构建系统配置
- ✅ 单元测试框架
- ✅ 压测程序（PlatformLayer / FrameBroker / CameraSource）

### 进行中模块

- 🚧 CameraSource 高级能力（多平面 / DMA-BUF）
- 🚧 背压策略完善（延迟阈值 / DropPolicy 参数化）

### 计划中模块

- ⏳ 工具类实现
- ⏳ 集成测试
- ⏳ 性能测试
- ⏳ 示例代码

### 架构评审落实

| 编号 | 主题 | 状态 | 备注 |
|------|------|------|------|
| ARCH-001 | Buffer 所有权不明确 | ✅ 已完成 | 引入 BufferGuard（RAII） |
| ARCH-002 | 缺少 Buffer 状态机 | ✅ 已完成 | Free → InUse → InFlight |
| ARCH-003 | Buffer 泄漏检测 | ✅ 已完成 | CheckLeaks + 清理时告警 |
| ARCH-004 | 丢帧策略硬编码 | 🚧 进行中 | Backpressure 策略参数化 |
| ARCH-005 | 背压阈值配置 | ⏳ 计划中 | 阈值动态调整 |
| ARCH-006 | 订阅者优先级静态 | ⏳ 计划中 | 自适应优先级 |
| ARCH-007 | 设备自动重连 | ⏳ 计划中 | 设备监控与恢复 |
| ARCH-008 | 降级策略 | ⏳ 计划中 | 降帧/降分辨率 |
| ARCH-018 | 发布端/订阅端解耦模式 | 🚧 进行中 | 单发布端 + 多订阅端隔离 |
| ARCH-019 | 按订阅启停 Camera | 🚧 进行中 | 无订阅不采集，避免资源空耗 |
| ARCH-020 | 跨平台协议头协定 | 🚧 进行中 | 默认设备宏 + MIPI/USB 可扩展 |

### 架构完善与待优化项

- 零拷贝主链路完善（DMABUF / 多平面）
- 订阅者背压与丢帧策略（按优先级/延迟阈值）
- Buffer 生命周期与复用池（ARCH-001/2/3 已完成，零拷贝待推进）
- 采集/分发线程亲和性与调度策略
- 指标与观测性（FPS、延迟、队列深度）

### 详细设计摘要（Buffer 生命周期与背压）

**Buffer 生命周期与复用池**
1. `BufferPool` 统一管理预分配 Buffer，避免热路径分配与拷贝。
2. `FrameHandle` 持有 Buffer 引用而非所有权，依靠 RAII 归还。
3. 生命周期状态：`Free -> InUse -> InFlight -> Free`。
4. 归还后由 CameraSource 重新 `QBUF`，保持采集连续性。
5. 当前实现为 **拷贝模式**（V4L2 MMAP -> BufferPool）。
6. 后续升级为 **DMA-BUF 零拷贝**。

**背压与丢帧策略**
1. 采集层：池耗尽时优先丢弃最新帧，避免阻塞采集线程。
2. 分发层：队列超阈值或延迟过大触发丢帧策略。
3. 订阅者层：低优先级订阅者可“仅保留最新帧”。
4. 支持策略：`DropNewest`、`DropOldest`、`DropByPriority`。
5. 当前落地：采集层池耗尽丢帧 + Broker 队列上限丢帧。
6. 观测指标：丢帧数、队列深度、FPS、延迟分布。

## 快速开始

### 环境要求

- Ubuntu 18.04+ / Debian 10+
- GCC 7.0+ (支持 C++17)
- CMake 3.10+
- spdlog（推荐以 third_party 方式集成）
- Google Test（推荐以 third_party/googletest 方式集成）

### spdlog 安装与集成（推荐）

本项目的 CMake 已支持以下优先级：
1. `find_package(spdlog CONFIG)`（系统已安装时）
2. `third_party/spdlog`（推荐）
3. `third_party/spdlog_stub`（仅兜底）

推荐将 spdlog 作为子模块放入 `third_party/spdlog`：

```bash

# 在仓库根目录执行

git submodule add https://github.com/gabime/spdlog.git third_party/spdlog
git submodule update --init --recursive
```

可选方案（系统安装，适合 CI / 统一环境）：

```bash
sudo apt-get update
sudo apt-get install -y libspdlog-dev
```

完成上述任一方式后，直接构建即可：

```bash
rm -rf build
./scripts/build.sh
```

### Google Test 安装与配置（推荐）

本项目的 CMake 已支持以下优先级：
1. `find_package(GTest CONFIG)`（系统已安装时）
2. `third_party/googletest`（推荐）
3. 未找到时跳过测试目标（不影响库构建）

推荐将 googletest 作为子模块放入 `third_party/googletest`：

```bash

# 在仓库根目录执行

git submodule add https://github.com/google/googletest.git third_party/googletest
git submodule update --init --recursive
```

可选方案（系统安装，适合 CI / 统一环境）：

```bash
sudo apt-get update
sudo apt-get install -y libgtest-dev
```

### 边缘设备与交叉编译（RK3576 / Debian）

当前阶段优先在 Ubuntu 上使用 V4L2 调试。面向 RK3576 的交叉编译规划：

- 规划 CMake Toolchain 文件（例如 `cmake/toolchains/rk3576.cmake`）
- 使用 aarch64 交叉编译器与 sysroot（由 SDK 提供）
- 设备侧依赖：V4L2、pthread、libdrm（后续补充）
- CI 增加交叉编译与最小运行时包产物

完成配置后，建议全量重新构建并执行测试：

```bash
rm -rf build
./scripts/build.sh
cd build
ctest --output-on-failure
```

### 编译步骤

```bash

# 克隆仓库

git clone https://github.com/SuycxZMZ/CameraSubsystem.git
cd CameraSubsystem

# 编译项目

./scripts/build.sh

# 运行测试（需已配置 Google Test）

cd build
ctest --output-on-failure
```

### 使用示例

```cpp
#include "camera_subsystem/core/camera_config.h"
#include "camera_subsystem/broker/frame_subscriber.h"

using namespace camera_subsystem;

// 创建订阅者
class MySubscriber : public broker::IFrameSubscriber
{
public:
    void OnFrame(const core::FrameHandle& frame) override
    {
        // 处理帧数据
        printf("Received frame: %u\n", frame.frame_id_);
    }

    const char* GetSubscriberName() const override
    {
        return "MySubscriber";
    }
};

int main()
{
    // 初始化日志系统
    platform::PlatformLogger::Initialize("camera.log", core::LogLevel::kInfo);

    // 创建配置
    core::CameraConfig config = core::CameraConfig::GetDefault();

    // TODO: 创建 CameraSource 和 FrameBroker
    // TODO: 注册订阅者
    // TODO: 开始采集

    return 0;
}
```

## 贡献指南

我们欢迎所有形式的贡献！请遵循以下步骤：

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

### 代码规范

- 遵循项目的编码规范（详见 NAMING_CONVENTION.md）
- 添加适当的单元测试
- 更新相关文档
- 确保所有测试通过

## 许可证

本项目采用 MIT 许可证。详见 LICENSE 文件。

## 联系方式

- **项目维护者**: CameraSubsystem Team
- **问题反馈**: [GitHub Issues](https://github.com/yourusername/CameraSubsystem/issues)
- **文档**: [项目文档](https://github.com/yourusername/CameraSubsystem/tree/main/docs)

## 致谢

感谢所有为本项目做出贡献的开发者！

---

**最后更新**: 2026-02-03
**文档版本**: v0.1
