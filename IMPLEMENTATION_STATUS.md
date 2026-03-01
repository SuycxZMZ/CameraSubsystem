# CameraSubsystem 实现状态

**更新日期:** 2026-03-01

## 项目概述

CameraSubsystem 项目已完成核心模块的实现，进入优化和完善阶段。项目旨在构建一个高性能、低延迟、可扩展的 Camera 数据流基座，作为 AI 推理、视频编码、预览显示等上层应用的统一数据来源。

## 目录结构

```text
CameraSubsystem/
├── include/camera_subsystem/       # 头文件目录
│   ├── core/                       # 核心数据结构（Frame/Buffer/Config）
│   ├── broker/                     # 分发层接口
│   ├── camera/                     # Camera层接口（含 SessionManager）
│   ├── ipc/                        # 控制面/数据面协议与接口
│   ├── platform/                   # 平台抽象层
│   └── utils/                      # 工具类
├── src/                            # 源文件目录（与 include 对应）
│   ├── core/
│   ├── broker/
│   ├── camera/
│   ├── ipc/
│   ├── platform/
│   └── utils/
├── tests/                          # 测试目录
│   ├── unit/                       # 单元测试
│   │   ├── test_camera_session_manager.cpp
│   │   ├── test_camera_control_ipc.cpp
│   │   └── ...（其余 Core/Broker/Camera 用例）
│   └── stress/                     # 压力测试
│       ├── platform_stress_test.cpp
│       ├── frame_broker_stress_test.cpp
│       └── camera_source_stress_test.cpp
├── docs/                           # 文档目录
│   ├── PROJECT_OVERVIEW.md         # 项目概览
│   └── ARCHITECTURE_REVIEW.md      # 架构评审文档
├── examples/                       # 双进程示例
│   ├── camera_publisher_example.cpp
│   └── camera_subscriber_example.cpp
├── scripts/                        # 脚本目录
│   ├── build.sh                    # 构建脚本
│   ├── format.sh                   # 代码格式化脚本
│   └── count_loc.sh                # 代码量统计脚本
├── cmake/                          # CMake配置目录
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
- ✅ BufferGuard 单元测试（新增）
- ✅ BufferState 状态机测试（新增）
- ✅ FrameHandleEx 单元测试（新增）

**新增组件:**

- ✅ `buffer_pool.h/cpp` - BufferPool 统一生命周期与复用池
- ✅ `buffer_guard.h/cpp` - BufferGuard RAII 所有权管理
- ✅ `buffer_state.h` - Buffer 状态机定义
- ✅ `frame_handle_ex.h/cpp` - FrameHandleEx 扩展结构（绑定 Buffer 生命周期）

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
- ✅ `camera_session_manager.h/cpp` - 会话管理（按订阅启停）

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
- ✅ `examples/CMakeLists.txt` - 双进程示例构建配置
- ✅ 库目标定义
  - camera_subsystem_core (静态库)
  - camera_subsystem_platform (静态库)
  - camera_subsystem_broker (静态库)
  - camera_subsystem_camera (静态库)
  - camera_subsystem_ipc (静态库)
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
- ✅ CameraSessionManager 单元测试
- ✅ 控制面 IPC 单元测试（含沙箱受限跳过策略）

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
- ✅ docs/ARCHITECTURE_REVIEW.md - 架构评审文档
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

1. **完善 CameraSource 数据通路**
   - 增加多平面格式验证用例
   - 推进 DMA-BUF 零拷贝主链路

2. **完善 Broker 背压策略**
   - 策略参数化（阈值 / 优先级 / 延迟窗口）
   - 观测指标联动压测

3. **提升进程模型健壮性**
   - 控制面心跳与断链恢复
   - 订阅抖动场景下的会话防抖策略

### 中期目标（3-4周）

1. **完善测试覆盖**
   - 增加集成测试
   - 添加性能测试
   - 扩展压力测试场景

2. **示例工程强化**
   - 增加子发布端（编解码链路）示例
   - 增加多订阅端并发示例
   - 增加故障注入示例（设备断连/重连）

3. **文档完善**
   - 架构评审文档与实现状态联动
   - 补充 RK3576 部署手册
   - 补充运维排障手册

### 长期目标（1-2月）

1. **性能优化**
   - 零拷贝传输优化
   - 内存占用优化
   - CPU 占用优化

2. **功能扩展**
   - 支持 Android HAL
   - 完善发布端/订阅端解耦通信（生产级协议与安全控制）
   - 支持更多像素格式

3. **监控与诊断**
   - 实现性能指标采集
   - 实现实时监控接口
   - 实现诊断日志

## 架构完善项（面向边缘设备）

- 完成零拷贝主链路（V4L2 DMABUF -> FrameHandle -> Broker -> Consumer）
- 引入背压与丢帧策略（按订阅者优先级/延迟阈值）✅ 基础版本已实现（池耗尽丢帧）
- 统一 Buffer 生命周期与复用池（减少 malloc/free 抖动）✅ 基础版本已实现（拷贝模式）
- 发布端/订阅端解耦模型（唯一核心发布端 + 多子发布端/订阅端）
- 按订阅启停 Camera（无订阅不采集，避免资源空耗）
- 协议头协定与默认设备宏（支持 MIPI/USB 可扩展）
- 增加可观测性指标（FPS、队列深度、丢帧率、延迟分布）
- 采集线程与分发线程亲和性配置（绑定大核/小核）
- 降低日志对热路径的影响（采样或分级降噪）

架构评审详情见：`docs/ARCHITECTURE_REVIEW.md`

## 发布端/订阅端解耦模型（典型用户故事同步）

为与 `README.md` 与 `docs/PROJECT_OVERVIEW.md` 保持一致，当前阶段定义如下：

1. 架构目标是“唯一核心发布端（V4L2 直连）+ 多子发布端/订阅端”，不将“1 发布端 + 1 订阅端”作为架构限制。
2. 核心发布端是唯一底层设备入口；对同一路 Camera（同一 `/dev/videoX`）仅允许核心发布端持有采集句柄。
3. 子发布端作为核心发布端的订阅者，可执行编解码/转封装并向下游再发布。
4. 纯订阅端直接消费核心发布端或子发布端的数据，进行 AI/业务处理。
5. `1 核心发布端 + 1 订阅端` 仅作为开发阶段联调示例；业务场景可多子发布端/多订阅端并发。
6. 某路 Camera 仅在存在订阅时启动；订阅归零时停止并释放该路资源。

协议协定（基础落地）：

1. 公共头文件：`include/camera_subsystem/ipc/camera_channel_contract.h`
2. 默认设备宏：`CAMERA_SUBSYSTEM_DEFAULT_CAMERA`（默认值 `/dev/video0`）
3. 可扩展 Camera 类型：默认、MIPI、USB、平台私有类型

示例程序（双进程）：

1. 核心发布端：`bin/camera_publisher_example`
2. 订阅端：`bin/camera_subscriber_example`

启动步骤：

1. 先启动发布端（终端 1）：

```bash
./bin/camera_publisher_example
```

2. 再启动订阅端（终端 2）：

```bash
./bin/camera_subscriber_example
```

3. 两端默认无限运行，按 `Ctrl+C` 退出（`SIGINT/SIGTERM` 优雅退出）。

参数说明：

1. 发布端：

```bash
./bin/camera_publisher_example [device_path] [control_socket] [data_socket]
```

- `device_path`：默认 `CAMERA_SUBSYSTEM_DEFAULT_CAMERA`（通常 `/dev/video0`）
- `control_socket`：默认 `/tmp/camera_subsystem_control.sock`
- `data_socket`：默认 `/tmp/camera_subsystem_data.sock`

2. 订阅端：

```bash
./bin/camera_subscriber_example [output_dir] [control_socket] [data_socket]
```

- `output_dir`：默认 `./subscriber_frames`
- `control_socket`：默认 `/tmp/camera_subsystem_control.sock`
- `data_socket`：默认 `/tmp/camera_subsystem_data.sock`

运行期输出：

1. 发布端每秒打印：`sec | frames | fps | clients | sent_bytes | send_fail`
2. 订阅端每秒打印：`sec | frames | fps | received_bytes | save_fail | image`
3. 订阅端每秒保存 1 张图片，槽位 `0~9` 循环覆盖。

故障排查：

1. `connect control socket failed` / `connect data socket failed`：
   确认发布端已启动并运行，且控制面与数据面 socket 路径参数一致。
2. Camera 打开失败（如 `/dev/video0`）：
   检查设备节点 `ls /dev/video0` 与用户权限（`video` 组），必要时用 `sudo` 验证。
3. socket bind 失败（Address already in use）：
   清理残留 socket 文件后重试：

```bash
rm -f /tmp/camera_subsystem_control.sock /tmp/camera_subsystem_data.sock
```

4. 订阅端无图或 `save_fail` 增长：
   检查输出目录写权限，并先用 `camera_source_stress_test` 验证采集链路。

## 架构设计细化（Buffer 生命周期与背压策略）

**Buffer 生命周期与复用池** ✅ 已完善

1. `BufferPool` 统一管理预分配 Buffer，避免热路径频繁分配。
2. `FrameHandle` 仅持有引用，使用 RAII 归还 Buffer。
3. `FrameHandleEx` 扩展结构持有 `shared_ptr<BufferGuard>`，绑定 Buffer 生命周期。
4. 生命周期状态：`Free -> InUse -> InFlight -> Free`（新增 `Error` 状态）。
5. 状态机支持回退：`InFlight -> InUse`（取消分发时）。
6. 析构安全：BufferPool 析构时等待所有 BufferGuard 归还（最多 5 秒）。
7. 归还后由 CameraSource 重新 `QBUF`，保证采集连续性。
8. 当前实现为 **拷贝模式**（V4L2 MMAP -> BufferPool -> Broker/Subscriber）。
9. 后续升级为 **DMA-BUF 零拷贝**，避免拷贝成本。

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

- [x] 修复 FrameHandle 悬空指针风险（P0）✅ 2026-02-27
- [x] 修复 BufferPool 析构竞态条件（P1）✅ 2026-02-27
- [x] 完善状态机转换机制（P1）✅ 2026-02-27
- [ ] 添加更多的错误处理和边界检查
- [ ] 实现内存池管理
- [ ] 添加性能分析工具
- [ ] 完善日志系统
- [ ] 添加代码覆盖率检查
- [ ] 增加跨架构编译与运行时自检
- [ ] 完善多平面与 DMA-BUF 实现细节
- [ ] 将 BufferPool 与 DMA-BUF 零拷贝打通
- [ ] 背压策略参数化（延迟阈值/优先级规则）
- [ ] ARCH-018：核心发布端（V4L2 直连）+ 多子发布端/订阅端生产级落地
- [ ] ARCH-019：按订阅启停 Camera 会话管理（抖动防护/恢复策略）
- [ ] ARCH-020：跨平台协议头协定与设备描述扩展（版本协商/能力位）

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

**最后更新:** 2026-03-01
**文档版本:** v0.2
