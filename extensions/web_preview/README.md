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
| `web/` | 后续放置前端工程，计划采用 Vite + React + TypeScript + Tailwind CSS + shadcn/ui。 |
| `docs/` | Web Preview 相关架构文档。 |
| `scripts/` | 构建、部署和板端运行辅助脚本。 |

## 构建

本机构建：

```bash
./scripts/build-gateway.sh
```

RK3576 交叉编译：

```bash
./scripts/build-gateway-rk3576.sh
```

## 运行示例

先启动 CameraSubsystem 核心发布端，再启动 Gateway：

```bash
./gateway/build/web_preview_gateway --device /dev/video45 --port 8080
```

访问：

```text
http://<board_ip>:8080
```

第一版 Gateway 内置最小 HTTP + WebSocket 服务，不依赖 Boost.Beast 或 libwebsockets。当前只把 JPEG / MJPEG 作为必须支持的预览格式。

## 文档

- [WEB_PREVIEW_ARCHITECTURE.md](docs/WEB_PREVIEW_ARCHITECTURE.md)
