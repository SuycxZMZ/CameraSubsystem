# CameraSubsystem 项目概览

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

```
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

```
Camera Hardware -> V4L2 Driver -> CameraSource -> FrameBroker -> Subscribers
     (DQBUF)       (DMA-BUF)      (FrameHandle)  (Dispatch)     (OnFrame)
```

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
- **完成度**: 约 20%

### 已完成模块
- ✅ 核心数据结构设计
- ✅ 平台抽象层接口定义
- ✅ 分发层接口定义
- ✅ 构建系统配置
- ✅ 单元测试框架

### 进行中模块
- 🚧 PlatformLayer 实现
- 🚧 FrameBroker 实现
- 🚧 CameraSource 实现

### 计划中模块
- ⏳ 工具类实现
- ⏳ 集成测试
- ⏳ 性能测试
- ⏳ 示例代码

## 快速开始

### 环境要求
- Ubuntu 18.04+ / Debian 10+
- GCC 7.0+ (支持 C++17)
- CMake 3.10+
- spdlog（推荐以 third_party 方式集成）
- Google Test（可选；未安装时会自动跳过测试）

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

### 编译步骤

```bash
# 克隆仓库
git clone https://github.com/SuycxZMZ/CameraSubsystem.git
cd CameraSubsystem

# 编译项目
./scripts/build.sh

# 运行测试
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

**最后更新**: 2026-01-28
**文档版本**: v0.1
