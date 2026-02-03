# CameraSubsystem 实现状态

**更新日期:** 2026-02-03

## 项目概述

CameraSubsystem 项目已完成核心模块的实现，进入优化和完善阶段。项目旨在构建一个高性能、低延迟、可扩展的 Camera 数据流基座，作为 AI 推理、视频编码、预览显示等上层应用的统一数据来源。

## 目录结构

```text
CameraSubsystem/
├── include/camera_subsystem/       # 头文件目录
│   ├── core/                       # 核心数据结构
│   │   ├── types.h                 # 类型定义（枚举、错误码等）
│   │   ├── frame_handle.h          # 帧句柄结构
│   │   └── camera_config.h         # Camera配置结构
│   ├── broker/                     # 分发层
│   │   ├── frame_subscriber.h      # 订阅者接口
│   │   └── frame_broker.h          # 分发中心实现
│   ├── platform/                   # 平台抽象层
│   │   ├── platform_logger.h       # 日志系统
│   │   ├── platform_thread.h       # 线程封装
│   │   └── platform_epoll.h        # Epoll封装
│   ├── camera/                     # Camera层
│   │   └── camera_source.h         # Camera数据源实现
│   └── utils/                      # 工具类
│       └── signal_handler.h        # 信号处理工具
├── src/                            # 源文件目录
│   ├── core/
│   │   ├── types.cpp               # 类型实现
│   │   ├── frame_handle.cpp        # 帧句柄实现
│   │   └── camera_config.cpp       # Camera配置实现
│   ├── platform/                   # 平台层实现
│   │   ├── platform_logger.cpp       # 日志系统实现
│   │   ├── platform_thread.cpp       # 线程封装实现
│   │   └── platform_epoll.cpp        # Epoll封装实现
│   ├── broker/                     # 分发层实现
│   │   └── frame_broker.cpp          # 分发中心实现
│   ├── camera/                     # Camera层实现
│   │   └── camera_source.cpp         # Camera数据源实现
│   └── utils/                      # 工具类实现
│       └── signal_handler.cpp        # 信号处理工具实现
├── tests/                          # 测试目录
│   ├── unit/                       # 单元测试
│   │   ├── test_frame_handle.cpp   # 帧句柄测试
│   │   └── test_camera_config.cpp  # Camera配置测试
│   └── stress/                      # 压力测试
│       ├── platform_stress_test.cpp    # PlatformLayer压测
│       ├── frame_broker_stress_test.cpp # FrameBroker压测
│       └── camera_source_stress_test.cpp # CameraSource压测
├── docs/                           # 文档目录
│   └── PROJECT_OVERVIEW.md         # 项目概览
├── examples/                       # 示例代码（待添加）
├── scripts/                        # 脚本目录
│   ├── build.sh                   # 构建脚本
│   └── format.sh                  # 代码格式化脚本
├── cmake/                          # CMake配置目录
├── build/                          # 构建目录
├── CMakeLists.txt                  # 主CMake配置文件
├── README.md                       # 项目主文档
├── structure.md                    # 架构设计文档
├── API_REFERENCE.md                # API接口文档
├── NAMING_CONVENTION.md            # 命名规范文档
└── IMPLEMENTATION_STATUS.md        # 本文件
```

## 已完成模块

### 1. 核心数据结构 (Core) ✅

**状态:** 已完成并测试

**实现内容:**
- ✅ `types.h/cpp` - 类型定义
  - PixelFormat 枚举
  - MemoryType 枚举
  - IoMethod 枚举
  - ErrorCode 枚举
  - LogLevel 枚举
  - 辅助函数（GetErrorString, PixelFormatToString, MemoryTypeToString）

- ✅ `frame_handle.h/cpp` - 帧句柄结构
  - POD 结构设计
  - 多平面格式支持
  - Stride 和 Offset 信息
  - 辅助方法（GetPlaneData, GetPlaneSize, IsValid, Reset）

- ✅ `camera_config.h/cpp` - Camera配置结构
  - 配置参数定义
  - 验证方法（IsValid）
  - 默认配置（GetDefault）
  - 重置方法（Reset）

**测试覆盖:**
- ✅ FrameHandle 单元测试（12个测试用例）
- ✅ CameraConfig 单元测试（11个测试用例）
- ✅ 类型转换测试
- ✅ BufferPool 单元测试

**新增组件:**
- ✅ `buffer_pool.h/cpp` - BufferPool 统一生命周期与复用池

### 2. 平台抽象层 (Platform) ✅

**状态:** 已完成

**实现内容:**
- ✅ `platform_logger.h/cpp` - 日志系统封装（spdlog）
- ✅ `platform_thread.h/cpp` - 线程封装
- ✅ `platform_epoll.h/cpp` - Epoll封装

### 3. 分发层 (Broker) ✅

**状态:** 已完成

**实现内容:**
- ✅ `frame_subscriber.h` - 订阅者接口定义
- ✅ `frame_broker.h/cpp` - 分发中心实现

### 4. Camera层 (Camera) ✅

**状态:** 已完成基础实现

**已实现:**
- ✅ `camera_source.h/cpp` - Camera数据源实现（V4L2 + MMAP）
- ✅ 设备打开/格式配置/帧采集/回调分发
- ✅ BufferPool 复用池接入（拷贝模式）
- ✅ 基础背压：池耗尽时丢帧

**待实现:**
- ⏳ V4L2 多平面与 DMA-BUF 支持
- ⏳ 高级 Buffer 管理机制

### 5. 工具类 (Utils) 🚧

**状态:** 部分完成

**已实现:**
- ✅ signal_handler 信号处理工具

**待实现:**
- ⏳ 字符串工具类
- ⏳ 时间工具类
- ⏳ 数学工具类

## 构建系统

**状态:** 基本完成

**实现内容:**
- ✅ `CMakeLists.txt` - 主CMake配置文件
- ✅ `tests/CMakeLists.txt` - 测试配置文件
- ✅ 库目标定义
  - camera_subsystem_core (静态库)
  - camera_subsystem_platform (静态库)
  - camera_subsystem_broker (静态库)
  - camera_subsystem_camera (静态库)
- ✅ 依赖配置
  - pthread
  - spdlog
  - Google Test

## 测试状态

**状态:** 部分完成

**已实现测试:**
- ✅ FrameHandle 单元测试（12个测试用例，全部通过）
- ✅ CameraConfig 单元测试（11个测试用例，全部通过）
- ✅ PlatformLayer 压测程序（platform_stress_test）
- ✅ FrameBroker 压测程序（frame_broker_stress_test）
- ✅ CameraSource 压测程序（camera_source_stress_test）

**待添加测试:**
- ⏳ PlatformLayer 单元测试
- ⏳ FrameBroker 单元测试
- ⏳ CameraSource 单元测试
- ⏳ 集成测试
- ⏳ 性能测试

## 文档状态

**已完成文档:**
- ✅ README.md - 项目主文档
- ✅ structure.md - 架构设计文档
- ✅ API_REFERENCE.md - API接口文档
- ✅ NAMING_CONVENTION.md - 命名规范文档
- ✅ IMPLEMENTATION_STATUS.md - 本文件

**待添加文档:**
- ⏳ 开发者指南
- ⏳ 用户手册
- ⏳ 部署指南
- ⏳ 故障排查指南

## 下一步工作计划

### 短期目标（1-2周）

1. **完善 PlatformLayer 测试**
   - 添加 PlatformLayer 单元测试
   - 完善压测程序的覆盖率

2. **完善 FrameBroker 测试**
   - 添加 FrameBroker 单元测试
   - 完善压测程序的覆盖率

3. **完善 CameraSource 测试**
   - 添加 CameraSource 单元测试
   - 完善压测程序的覆盖率

### 中期目标（3-4周）

1. **完善测试覆盖**
   - 增加集成测试
   - 添加性能测试
   - 扩展压力测试场景

2. **示例代码**
   - 创建简单的采集示例
   - 创建订阅者示例
   - 创建完整的应用示例

3. **文档完善**
   - 编写开发者指南
   - 编写用户手册
   - 编写部署指南

### 长期目标（1-2月）

1. **性能优化**
   - 零拷贝传输优化
   - 内存占用优化
   - CPU 占用优化

2. **功能扩展**
   - 支持 Android HAL
   - 支持跨进程通信
   - 支持更多像素格式

3. **监控与诊断**
   - 实现性能指标采集
   - 实现实时监控接口
   - 实现诊断日志

## 架构完善项（面向边缘设备）

- 完成零拷贝主链路（V4L2 DMABUF -> FrameHandle -> Broker -> Consumer）
- 引入背压与丢帧策略（按订阅者优先级/延迟阈值）✅ 基础版本已实现（池耗尽丢帧）
- 统一 Buffer 生命周期与复用池（减少 malloc/free 抖动）✅ 基础版本已实现（拷贝模式）
- 增加可观测性指标（FPS、队列深度、丢帧率、延迟分布）
- 采集线程与分发线程亲和性配置（绑定大核/小核）
- 降低日志对热路径的影响（采样或分级降噪）

## 架构设计细化（Buffer 生命周期与背压策略）

**Buffer 生命周期与复用池**
1. `BufferPool` 统一管理预分配 Buffer，避免热路径频繁分配。
2. `FrameHandle` 仅持有引用，使用 RAII 归还 Buffer。
3. 生命周期状态：`Free -> InUse -> InFlight -> Free`。
4. 归还后由 CameraSource 重新 `QBUF`，保证采集连续性。
5. 当前实现为 **拷贝模式**（V4L2 MMAP -> BufferPool -> Broker/Subscriber）。
6. 后续升级为 **DMA-BUF 零拷贝**，避免拷贝成本。

**背压与丢帧策略**
1. 采集层：池耗尽时优先丢弃最新帧，避免阻塞采集线程。
2. 分发层：队列超阈值或延迟过大触发丢帧策略。
3. 订阅者层：低优先级订阅者可“仅保留最新帧”。
4. 支持策略：`DropNewest`、`DropOldest`、`DropByPriority`。
5. 当前落地：采集层池耗尽丢帧 + Broker 队列上限丢帧。
6. 指标：丢帧数、队列深度、FPS、延迟分布。

## 边缘设备适配与交叉编译计划（RK3576 / Debian）

- 规划 CMake Toolchain 文件（例如 `cmake/toolchains/rk3576.cmake`）
- 引入 sysroot 目录与交叉编译器前缀（aarch64-linux-gnu-）
- CI 中增加交叉编译构建与最小运行时包产物
- 记录设备侧依赖清单（V4L2、pthread、libdrm 等）

## 技术债务

- [ ] 添加更多的错误处理和边界检查
- [ ] 实现内存池管理
- [ ] 添加性能分析工具
- [ ] 完善日志系统
- [ ] 添加代码覆盖率检查
- [ ] 增加跨架构编译与运行时自检
- [ ] 完善多平面与 DMA-BUF 实现细节
- [ ] 将 BufferPool 与 DMA-BUF 零拷贝打通
- [ ] 背压策略参数化（延迟阈值/优先级规则）

## 贡献指南

欢迎贡献代码！请遵循以下步骤：

1. Fork 本仓库
2. 创建特性分支 (`git checkout -b feature/AmazingFeature`)
3. 提交更改 (`git commit -m 'Add some AmazingFeature'`)
4. 推送到分支 (`git push origin feature/AmazingFeature`)
5. 开启 Pull Request

## 许可证

本项目采用 MIT 许可证。详见 LICENSE 文件。

## 联系方式

- 项目维护者: CameraSubsystem Team
- 问题反馈: [GitHub Issues](https://github.com/SuycxZMZ/CameraSubsystem/issues)

---

**最后更新:** 2026-02-03
**文档版本:** v0.2
