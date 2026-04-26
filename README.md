# CameraSubsystem

**目标平台:** Linux / 嵌入式边缘设备（当前已接入 RK3576 / Debian 验证链路，预留 Android 迁移）<br>
**开发语言:** C++17 / C POD 数据结构<br>
**核心方向:** Camera 采集后端 -> Publish/Subscribe -> AI / 编码 / 录制<br>
**最后更新:** 2026-04-26

> **文档硬规范**
>
> - 本项目所有流程图、框图、时序图、状态机图、目录结构图等图示必须使用 Mermaid fenced code block（语言标识为 `mermaid`）。
> - 禁止新增 ASCII art/text 框图；普通日志、命令输出、代码片段按其原始语言使用 fenced code block。
> - 每份项目文档必须在文档元信息和硬规范之后维护 `## 目录`，目录至少覆盖二级标题，并使用相对链接或页内锚点。
> - `README.md` 是团队入口文档，开头必须维护工程结构概览、项目文档索引和常用入口链接。
> - 评审建议、风险、ARCH-* 跟踪项只维护在 [docs/ARCHITECTURE_REVIEW.md](docs/ARCHITECTURE_REVIEW.md)，其他文档只链接引用，避免重复漂移。

---

## 目录

- [1. 项目定位](#1-项目定位)
- [2. 当前状态](#2-当前状态)
- [3. 工程结构](#3-工程结构)
- [4. 项目文档索引](#4-项目文档索引)
- [5. 架构概览](#5-架构概览)
- [6. 快速开始](#6-快速开始)
- [7. 示例运行](#7-示例运行)
- [8. 开发约定](#8-开发约定)
- [9. 当前限制与下一步](#9-当前限制与下一步)

---

## 1. 项目定位

CameraSubsystem 是一个面向边缘视觉应用的通用 Camera 数据流基座。它的职责是把底层 Camera 设备访问、采集会话管理、帧分发、订阅关系治理和平台差异收敛到一个可维护的基础子系统中，向上服务 AI 推理、编码、录制和调试预览等模块。

项目设计不绑定单一 SoC 或单一采集 API。当前已落地的采集后端以 Linux V4L2/MMAP 为主，RK3576 / Debian 是当前已接入的交叉编译与板端验证平台；后续可以继续扩展 Android Camera HAL、厂商媒体栈、USB/UVC、MIPI/CSI 多平面链路或其他平台私有后端。

当前验证平台 — Luckfox Omni3576（RK3576）：

<p align="center">
  <img src="docs/images/Luckfox-Omni3576.jpg" alt="Luckfox Omni3576 开发板" width="480" />
</p>

当前主线目标：

1. 核心发布端独占底层 Camera 设备或采集后端入口，订阅端不直接访问平台设备节点。
2. 使用控制面 IPC 管理订阅/退订，使用数据面链路传输帧数据。
3. 按订阅引用计数启停 Camera，会话无人订阅时释放设备资源。
4. 在 Ubuntu 本机可调试，并保留面向不同边缘设备的交叉编译和板端验证路径。

---

## 2. 当前状态

| 项目 | 状态 | 说明 |
|------|------|------|
| 本机构建与测试 | 已通过 | `./scripts/build.sh` 可完成构建与测试 |
| 交叉编译链路 | 已通过 | 当前已接入 RK3576 / Omni3576 SDK 官方 GCC 10.3 工具链 |
| 发布端/订阅端示例 | 已落地 | `camera_publisher_example` / `camera_subscriber_example` |
| 控制面 IPC | 基础落地 | Subscribe / Unsubscribe / Ping |
| 数据面 IPC | 示例落地 | Unix Socket 复制帧头 + 帧数据 |
| Buffer 生命周期治理 | 基础落地 | `BufferPool` / `BufferGuard` / 状态机 / 泄漏检测 |
| Web Preview 扩展 | 已落地 | Gateway + React 前端，浏览器实时预览 Camera 画面 |
| DMA-BUF 零拷贝主链路 | 未完成 | 当前 V4L2 后端仍是 MMAP -> BufferPool 的拷贝模式 |
| 板端运行验证 | 初步完成 | 已在 RK3576 Debian 12 上完成 publisher/subscriber smoke test |

---

## 3. 工程结构

```mermaid
graph TB
    Root["CameraSubsystem/"]

    Root --> Include["include/camera_subsystem/<br/>对外头文件"]
    Root --> Src["src/<br/>模块实现"]
    Root --> Examples["examples/<br/>发布端/订阅端示例"]
    Root --> Extensions["extensions/<br/>可选扩展模块"]
    Root --> Tests["tests/<br/>单元测试与压力测试"]
    Root --> Scripts["scripts/<br/>构建与工具脚本"]
    Root --> CMake["cmake/<br/>CMake 模块与工具链"]
    Root --> Docs["docs/<br/>项目文档与图片"]
    Root --> ThirdParty["third_party/<br/>第三方依赖"]

    Include --> IncCore["core/<br/>BufferPool, FrameHandle"]
    Include --> IncCamera["camera/<br/>CameraSource, CameraConfig"]
    Include --> IncBroker["broker/<br/>CameraSessionManager"]
    Include --> IncIPC["ipc/<br/>ControlServer, DataServer"]
    Include --> IncPlatform["platform/<br/>Logger, V4L2"]

    Extensions --> WebPreview["web_preview/<br/>Web 调试预览扩展"]
    WebPreview --> Gateway["gateway/<br/>C++ WebSocket 网关"]
    WebPreview --> Web["web/<br/>React 前端工程"]
    WebPreview --> WPScripts["scripts/<br/>构建与部署脚本"]
    WebPreview --> WPDocs["docs/<br/>扩展架构文档"]

    Docs --> DocImages["images/<br/>文档图片资源"]

    style Extensions fill:#e8f5e9,stroke:#4caf50
    style WebPreview fill:#c8e6c9,stroke:#4caf50
    style Gateway fill:#a5d6a7,stroke:#4caf50
    style Web fill:#a5d6a7,stroke:#4caf50
```

| 路径 | 用途 |
|------|------|
| [`include/camera_subsystem/`](include/camera_subsystem/) | 对外头文件，按 core / camera / broker / ipc / platform / utils 分层 |
| [`src/`](src/) | 模块实现，与公共头文件的模块边界保持一致 |
| [`examples/`](examples/) | 核心发布端与订阅端双进程示例 |
| [`extensions/`](extensions/) | 可选扩展模块，当前包含 Web 调试预览扩展 |
| [`extensions/web_preview/gateway/`](extensions/web_preview/gateway/) | C++ WebSocket 网关，CameraSubscriberClient + HTTP/WebSocket 服务 |
| [`extensions/web_preview/web/`](extensions/web_preview/web/) | React + TypeScript + Vite 前端，实时帧渲染与流管理 |
| [`tests/`](tests/) | 单元测试与压力测试 |
| [`scripts/`](scripts/) | 构建、交叉编译、格式化和统计脚本 |
| [`cmake/toolchains/`](cmake/toolchains/) | 交叉编译工具链配置 |
| [`docs/`](docs/) | 项目说明、架构评审、文档索引 |
| [`docs/images/`](docs/images/) | 文档图片资源（开发板照片、运行截图等） |
| [`third_party/`](third_party/) | 第三方依赖源码或 stub，不受本项目文档规范约束 |

---

## 4. 项目文档索引

| 文档 | 角色 | 适合阅读场景 |
|------|------|--------------|
| [docs/README.md](docs/README.md) | 文档总索引 | 查找文档边界、维护规则和推荐阅读路径 |
| [README.md](README.md) | 团队入口 | 快速了解项目状态、工程结构、构建运行入口 |
| [docs/PROJECT_OVERVIEW.md](docs/PROJECT_OVERVIEW.md) | 项目概览 | 了解项目目标、技术栈、功能边界和快速开始 |
| [docs/ARCHITECTURE_REVIEW.md](docs/ARCHITECTURE_REVIEW.md) | 架构评审 | 查看系统/代码架构评审、风险、ARCH-* 跟踪项 |
| [IMPLEMENTATION_STATUS.md](IMPLEMENTATION_STATUS.md) | 实现状态 | 查看模块完成度、测试状态、技术债务和下一步计划 |
| [API_REFERENCE.md](API_REFERENCE.md) | API 参考 | 查询公开数据结构、类接口、IPC 协议和示例用法 |
| [NAMING_CONVENTION.md](NAMING_CONVENTION.md) | 工程规范 | 查询命名、目录、代码格式和跨平台约定 |
| [structure.md](structure.md) | 详细设计/历史设计 | 查看较完整的架构设计长文和历史设计语境 |
| [extensions/web_preview/README.md](extensions/web_preview/README.md) | Web Preview 入口 | 了解 Web 调试预览扩展的目标、使用方法和部署步骤 |
| [extensions/web_preview/docs/WEB_PREVIEW_ARCHITECTURE.md](extensions/web_preview/docs/WEB_PREVIEW_ARCHITECTURE.md) | Web Preview 架构 | 查看 `web_preview_gateway`、前端工程和格式扩展设计 |

文档维护原则：

1. README 只做入口和当前事实摘要，不堆叠长篇详细设计。
2. API 只写接口与用法，不承载架构评审。
3. 实现状态只写完成度和计划，不重复评审建议。
4. 架构建议、风险和优先级统一进入 [docs/ARCHITECTURE_REVIEW.md](docs/ARCHITECTURE_REVIEW.md)。

---

## 5. 架构概览

### 5.1 系统架构

```mermaid
flowchart TB
    subgraph Publisher["核心发布端进程"]
        Control["控制面 IPC<br/>CameraControlServer"]
        Session["CameraSessionManager<br/>按订阅引用启停"]
        Source["CameraSource<br/>采集后端适配<br/>当前 V4L2/MMAP"]
        Data["数据面 IPC<br/>DataSocketServer"]
    end

    subgraph Extensions["extensions 扩展层"]
        subgraph WebPreview["web_preview"]
            GW["web_preview_gateway<br/>CameraSubscriberClient<br/>+ HTTP/WebSocket Server"]
            Frontend["React 前端<br/>帧渲染 + 流管理"]
        end
    end

    subgraph Consumers["其他下游进程"]
        SubPublisher["子发布端（可选）"]
        Subscriber["订阅端 / AI / 编码 / 录制"]
    end

    GW -->|Subscribe / Unsubscribe| Control
    SubPublisher -->|Subscribe / Unsubscribe| Control
    Subscriber -->|Subscribe / Unsubscribe| Control
    Control --> Session
    Session -->|0→1 Start / 1→0 Stop| Source
    Source --> Data
    Data --> GW
    Data --> SubPublisher
    Data --> Subscriber
    GW <-->|WebSocket 二进制帧| Frontend

    style Extensions fill:#e8f5e9,stroke:#4caf50
    style WebPreview fill:#c8e6c9,stroke:#4caf50
```

### 5.2 数据路径

当前数据路径是可运行示例路径，不是最终生产级零拷贝路径：

```mermaid
flowchart LR
    Camera["Camera Hardware"] --> Backend["Camera Backend<br/>当前 V4L2 Driver"]
    Backend -->|MMAP / DQBUF<br/>或其他后端帧句柄| Source["CameraSource"]
    Source -->|copy| Pool["BufferPool"]
    Pool --> Frame["FrameHandle"]
    Frame --> DataIPC["DataSocketServer<br/>Unix Socket"]
    DataIPC --> Subscriber["本地订阅端"]
    DataIPC --> Gateway["web_preview_gateway"]
    Gateway -->|WebFrameHeader<br/>+ JPEG payload| WS["WebSocket"]
    WS --> Browser["浏览器 Canvas"]
```

### 5.3 Web Preview 扩展

`extensions/web_preview/` 提供面向 Linux 边缘设备的 Web 调试预览扩展，当前以 RK3576 / Debian 板端调试为首个验证场景。该扩展通过独立 `web_preview_gateway` 作为 CameraSubsystem 订阅端接入核心发布端，并在开发板上提供 HTTP + WebSocket 服务，使局域网内浏览器可访问实时 Camera 预览页面。

当前第一阶段面向 USB 摄像头 JPEG 输出场景，默认对摄像头输出的 JPEG 帧进行原始透传，不在 Gateway 内执行视频编码。后续将通过可插拔 `FrameTransformPipeline` 扩展 NV12、YUYV、UYVY、RGB/RGBA、多平面 MIPI buffer、硬件格式转换和 AI 检测结果叠加等能力。

Web Preview 运行效果：

<p align="center">
  <img src="docs/images/web_example.jpg" alt="Web Preview 运行示例" width="720" />
</p>

详细设计见：

- [extensions/web_preview/README.md](extensions/web_preview/README.md)
- [extensions/web_preview/docs/WEB_PREVIEW_ARCHITECTURE.md](extensions/web_preview/docs/WEB_PREVIEW_ARCHITECTURE.md)

---

## 6. 快速开始

### 6.1 本机构建与测试

```bash
./scripts/build.sh
```

该脚本会配置 CMake、编译工程并执行 CTest。

### 6.2 交叉编译示例（RK3576）

默认 SDK 路径为项目同级目录 `../Omni3576-sdk`：

```bash
./scripts/build-rk3576.sh
```

如果 SDK 在其他目录：

```bash
OMNI3576_SDK_ROOT=/path/to/Omni3576-sdk ./scripts/build-rk3576.sh
```

当前 RK3576 产物输出到：

```text
bin/rk3576/
```

### 6.3 常用开发命令

| 任务 | 命令 |
|------|------|
| 格式化全部项目代码 | `./scripts/format.sh` |
| 格式化 Git 变更文件 | `./scripts/format.sh changed` |
| 统计代码量 | `./scripts/count_loc.sh` |
| 清理残留 socket | `rm -f /tmp/camera_subsystem_control.sock /tmp/camera_subsystem_data.sock` |

---

## 7. 示例运行

### 7.1 本机运行

终端 1 启动核心发布端：

```bash
./bin/camera_publisher_example
```

终端 2 启动订阅端：

```bash
./bin/camera_subscriber_example
```

### 7.2 板端运行（以 RK3576 为例）

将 `bin/rk3576/` 下产物复制到开发板后运行：

```bash
./camera_publisher_example
./camera_subscriber_example
```

板端运行前需要确认：

1. `/dev/video0` 或目标设备节点存在。
2. 当前用户具备 video 设备访问权限，必要时先用 `sudo` 排查。
3. 运行目录可写，订阅端默认会输出图片到 `subscriber_frames/`。

### 7.3 板端 Web 预览

在开发板上启动 Camera 发布端和 Web Preview Gateway 后，局域网内浏览器可直接访问实时画面：

```bash
# 1. 启动核心发布端（必须先启动）
./camera_publisher_example /dev/video45 &

# 2. 启动 Gateway
./web_preview_gateway --device /dev/video45 --port 8080 --static-root /home/luckfox/web_dist
```

浏览器访问 `http://<开发板IP>:8080` 即可看到实时 Camera 预览。

> **重要**：必须先启动 `camera_publisher_example`，再启动 `web_preview_gateway`。Gateway 启动时会立即连接发布端的 IPC，如果发布端未就绪，Gateway 会因连接失败而退出。

详细部署步骤和开发模式配置见 [extensions/web_preview/README.md](extensions/web_preview/README.md)。

### 7.4 示例参数

发布端：

```bash
./camera_publisher_example [device_path] [control_socket] [data_socket]
```

订阅端：

```bash
./camera_subscriber_example [output_dir] [control_socket] [data_socket] [device_path]
```

---

## 8. 开发约定

1. 文档新增或修改必须遵守本文开头的文档硬规范。
2. 架构风险、评审意见、ARCH-* 项不得分散写入 README 或实现状态文档。
3. 新增公共接口必须同步更新 [API_REFERENCE.md](API_REFERENCE.md)。
4. 新增命名或目录约定必须同步更新 [NAMING_CONVENTION.md](NAMING_CONVENTION.md)。
5. 新增平台依赖必须同步更新 toolchain/build 脚本和文档索引。
6. 代码变更后优先运行 `./scripts/build.sh`；交叉编译相关变更还应运行 `./scripts/build-rk3576.sh`。

---

## 9. 当前限制与下一步

当前限制：

1. 数据面 IPC 是示例复制链路，不适合作为 4K 高帧率生产通路。
2. DMA-BUF 零拷贝主链路尚未打通。
3. 背压策略只有基础队列上限和池耗尽丢帧，尚未参数化。
4. 设备断连恢复、订阅端异常恢复、核心发布端重启恢复仍未形成完整状态机。
5. 多平台后端能力发现、设备热插拔与恢复策略仍需完善。

下一步建议按 [docs/ARCHITECTURE_REVIEW.md](docs/ARCHITECTURE_REVIEW.md) 的 P0/P1 优先级推进。
