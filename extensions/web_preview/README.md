# Web Preview Extension

`web_preview` 是 CameraSubsystem 的 Web 调试预览扩展模块，面向 Linux 边缘设备，用于在开发板上提供局域网可访问的 Camera 实时预览页面。

## 目标

- 通过独立 `web_preview_gateway` 作为 CameraSubsystem 订阅端接入核心发布端。
- 在开发板上提供 HTTP 静态页面服务和 WebSocket 数据通道。
- 支持局域网内 PC 或其他节点通过浏览器访问 `http://<board_ip>:8080` 查看实时画面。
- 第一阶段优先支持 USB 摄像头 JPEG 输出的原始透传预览。
- 后续扩展支持 MIPI、NV12、YUYV、UYVY、RGB/RGBA、硬件格式转换、AI 检测结果叠加等能力。

## 目录结构

| 路径 | 用途 |
|------|------|
| `gateway/` | 纯 C++ 实现的 `web_preview_gateway`，独立 CMake 子工程。 |
| `web/` | 前端工程，Vite + React + TypeScript + Tailwind CSS + shadcn/ui。 |
| `docs/` | Web Preview 相关架构文档。 |
| `scripts/` | 构建、部署和板端运行辅助脚本。 |

## 快速开始

### 前置条件

- 本机：Node.js >= 18、pnpm
- 开发板：Luckfox Omni3576 (RK3576)，已运行 CameraSubsystem 核心发布端
- 开发板 IP：`192.168.31.9`（默认，可按实际修改）
- USB 摄像头已连接，设备节点为 `/dev/video45`（按实际修改）

### 方式一：本机开发模式（推荐调试使用）

本机开发模式下，Vite dev server 在本机运行，通过代理转发 WebSocket 到开发板上的 Gateway。前端热更新即时生效，适合开发调试。

**步骤 1：构建并部署 Gateway 到开发板**

```bash
# 交叉编译 Gateway
cd CameraSubsystem/extensions/web_preview
./scripts/build-gateway-rk3576.sh

# 拷贝 Gateway 到开发板
scp gateway/build-rk3576/web_preview_gateway luckfox@192.168.31.9:/home/luckfox/
```

**步骤 2：在开发板上启动核心发布端和 Gateway**

```bash
# SSH 登录开发板
ssh luckfox@192.168.31.9

# 启动 CameraSubsystem 核心发布端（如果尚未启动）
# 注意：camera_publisher_example 使用位置参数，不是 --device
./camera_publisher_example /dev/video45 &

# 启动 Gateway（绑定 0.0.0.0:8080，允许局域网访问）
./web_preview_gateway --device /dev/video45 --port 8080 --static-root /home/luckfox/web_dist
```

> **重要**：必须先启动 camera_publisher_example，再启动 web_preview_gateway。Gateway 启动时会立即连接发布端的控制面和数据面 IPC，如果发布端未就绪，Gateway 会因连接失败而退出。

> 注意：开发模式下 Gateway 不需要前端静态文件，Vite dev server 会代理前端请求。但 Gateway 仍需运行以提供 WebSocket 服务。

**步骤 3：在本机启动前端开发服务器**

```bash
cd CameraSubsystem/extensions/web_preview/web
pnpm install
pnpm dev
```

Vite dev server 启动后，在本机浏览器访问：

```
http://localhost:5173
```

Vite 会自动将 `/ws` 的 WebSocket 请求代理到 `ws://192.168.31.9:8080/ws`，将 `/status` 的 HTTP 请求代理到 `http://192.168.31.9:8080/status`。

如果开发板 IP 不是 `192.168.31.9`，需要修改 `web/vite.config.ts` 中的 proxy 配置：

```typescript
server: {
  proxy: {
    '/ws': {
      target: 'ws://<你的开发板IP>:8080',
      ws: true,
    },
    '/status': {
      target: 'http://<你的开发板IP>:8080',
    },
  },
},
```

### 方式二：生产模式（部署到开发板）

生产模式下，前端构建为静态文件，由开发板上的 Gateway 直接服务，不依赖本机 Node.js。

**步骤 1：构建前端**

```bash
cd CameraSubsystem/extensions/web_preview
./scripts/build-web.sh
```

构建产物在 `web/dist/` 目录。

**步骤 2：构建并部署 Gateway**

```bash
# 交叉编译 Gateway
./scripts/build-gateway-rk3576.sh

# 拷贝 Gateway 和前端到开发板
scp gateway/build-rk3576/web_preview_gateway luckfox@192.168.31.9:/home/luckfox/
ssh luckfox@192.168.31.9 "mkdir -p /home/luckfox/web_dist"
scp -r web/dist/* luckfox@192.168.31.9:/home/luckfox/web_dist/
```

或使用一键部署脚本：

```bash
./scripts/deploy-web.sh
```

**步骤 3：在开发板上启动**

```bash
ssh luckfox@192.168.31.9

# 启动核心发布端
./camera_publisher_example /dev/video45 &

# 启动 Gateway，指定前端静态文件目录
./web_preview_gateway --device /dev/video45 --port 8080 --static-root /home/luckfox/web_dist
```

**步骤 4：在局域网浏览器访问**

```
http://192.168.31.9:8080
```

### 方式三：仅使用 Fallback 页面

如果未部署前端构建产物，Gateway 会自动使用内置的 Fallback HTML 页面，提供基础的 JPEG 帧预览功能（无操作按钮、无自适应布局）。

```bash
# 启动 Gateway（不指定 static-root 或指向空目录）
./web_preview_gateway --device /dev/video45 --port 8080
```

> **重要**：必须先启动 camera_publisher_example，再启动 web_preview_gateway。
```

浏览器访问 `http://192.168.31.9:8080` 即可看到 Fallback 页面。

## Gateway 命令行参数

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `--bind <host>` | `0.0.0.0` | 绑定地址 |
| `--port <port>` | `8080` | HTTP/WebSocket 端口 |
| `--control-socket <path>` | `/tmp/camera_subsystem_control.sock` | 控制面 IPC socket 路径 |
| `--data-socket <path>` | `/tmp/camera_subsystem_data.sock` | 数据面 IPC socket 路径 |
| `--device <path>` | `/dev/video0` | 摄像头设备节点 |
| `--static-root <path>` | `../web/dist` | 前端静态文件目录 |
| `--client-id <id>` | `web_preview_gateway` | 控制面 IPC 客户端 ID |
| `--camera-id <id>` | `0` | Camera ID |
| `--max-fps <fps>` | `15` | 预览最大帧率 |
| `--help` | - | 显示帮助 |

## 前端功能说明

### 实时画面预览

- 通过 WebSocket 接收 `[WebFrameHeader][JPEG Payload]` 二进制帧
- 使用 `createImageBitmap` 解码 JPEG 并渲染到 Canvas
- 实时显示 FPS、分辨率、像素格式

### 流控制操作

| 按钮 | 功能 | 状态 |
|------|------|------|
| Play (▶) | 订阅流，开始预览 | 已实现 |
| Stop (■) | 退订流，停止预览 | 已实现 |
| Record (●) | 录制 | 暂未实现（UI 预留） |
| Detect (🔍) | AI 检测 | 暂未实现（UI 预留） |
| Camera (📷) | 保存当前画面快照为 PNG | 已实现 |

### 连接状态

页面右上角实时显示与 Gateway 的 WebSocket 连接状态：

- 🟢 已连接
- 🟡 连接中... / 重连中...
- 🔴 已断开

断连后自动重连（指数退避：1s → 2s → 4s → ... → 最大 30s）。

### 自适应布局

| 流数量 | 布局 |
|--------|------|
| 1 | 1x1 大画面 |
| 2 | 2x1 |
| 3-4 | 2x2 |
| 5-6 | 3x2 |
| 7-9 | 3x3 |
| >9 | 4列 + 滚动 |

移动端浏览器自动切换为单列布局。

## 构建

### Gateway 构建

本机构建：

```bash
./scripts/build-gateway.sh
```

RK3576 交叉编译：

```bash
./scripts/build-gateway-rk3576.sh
```

### 前端构建

```bash
./scripts/build-web.sh
```

## 协议说明

### WebSocket Binary Frame（Gateway → 浏览器）

帧结构：`[WebFrameHeader (92 bytes)][Payload]`

WebFrameHeader 字段布局（小端序，紧凑对齐）：

| 偏移 | 大小 | 字段 | 说明 |
|------|------|------|------|
| 0 | 4 | magic | 0x57504652 ("WPFR") |
| 4 | 4 | version | 1 |
| 8 | 4 | header_size | 92 |
| 12 | 4 | stream_id | 流索引 |
| 16 | 8 | frame_id | 帧序号 |
| 24 | 8 | timestamp_ns | 采集时间戳(纳秒) |
| 32 | 4 | width | 图像宽 |
| 36 | 4 | height | 图像高 |
| 40 | 4 | pixel_format | WebPixelFormat 枚举 |
| 44 | 4 | stride_y | Y平面stride |
| 48 | 4 | stride_uv | UV平面stride |
| 52 | 4 | payload_size | payload字节数 |
| 56 | 4 | transform_flags | 转换标志 |
| 60 | 32 | reserved | 保留 |

### WebSocket Text Frame（控制命令）

浏览器 → Gateway：

```json
{"type": "subscribe_stream", "stream_id": "usb_camera_0"}
{"type": "unsubscribe_stream", "stream_id": "usb_camera_0"}
```

Gateway → 浏览器：

```json
{"type": "command_result", "status": "success"}
{"type": "status", "stream_id": "usb_camera_0", "status": "streaming", "width": 640, "height": 480, "format": "JPEG", ...}
```

## 故障排查

| 问题 | 可能原因 | 解决方法 |
|------|----------|----------|
| 页面显示"已断开" | Gateway 未启动 | 在开发板上启动 `web_preview_gateway` |
| 页面显示"暂无活跃流" | 核心发布端未启动 | 在开发板上启动 `camera_publisher_example` |
| 画面不更新 | 摄像头设备节点错误 | 检查 `--device` 参数是否正确 |
| Fallback 页面而非 React UI | 前端 dist 未部署 | 运行 `build-web.sh` 并部署到开发板 |
| 本机开发无法连接 WebSocket | Vite proxy 配置错误 | 检查 `vite.config.ts` 中的开发板 IP |
| 显示"不支持的格式" | 摄像头输出非 JPEG 格式 | 当前仅支持 JPEG/MJPEG 透传 |

## 文档

- [WEB_PREVIEW_ARCHITECTURE.md](docs/WEB_PREVIEW_ARCHITECTURE.md)
