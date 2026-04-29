# 板端 Web 预览与录制调试指南

**最后更新:** 2026-04-29<br>
**适用范围:** RK3576 / Luckfox Omni3576 板端 Web Preview、Camera 发布端、H.264 录制服务联调<br>
**标准部署根目录:** `/home/luckfox/CameraSubsystem`

> **文档硬规范**
>
> - 本项目的系统架构图、模块框图、部署拓扑图、数据路径框图和工程结构框图必须使用 `architecture-diagram` skill 生成独立 HTML / inline SVG 图表产物；每个 HTML 图必须同步导出同名 `.svg`，Markdown 中默认直接显示 SVG，并附完整 HTML 图表链接。
> - 时序图、状态机图、纯目录结构图等仍使用 Mermaid fenced code block（语言标识为 `mermaid`）。
> - 禁止新增 ASCII art/text 框图；普通日志、命令输出、代码片段按其原始语言使用 fenced code block。
> - 本文是板端调试操作文档，记录当前可运行方式和目录规范，不替代架构设计文档。

---

## 目录

- [1. 目标](#1-目标)
- [2. 板端目录规范](#2-板端目录规范)
- [3. 本机构建](#3-本机构建)
- [4. 统一部署](#4-统一部署)
- [5. 板端启动顺序](#5-板端启动顺序)
- [6. 浏览器调试](#6-浏览器调试)
- [7. 录制验证](#7-录制验证)
- [8. 故障排查](#8-故障排查)
- [9. 维护要求](#9-维护要求)

---

## 1. 目标

当前板端 Web 调试同时覆盖两条功能链路：

1. **实时预览链路**：`camera_publisher_example` 发布原始 Camera 帧，`web_preview_gateway` 订阅原始帧并通过 WebSocket 推送给浏览器，前端 Canvas 显示实时画面。
2. **后台录制链路**：前端 Record 按钮发送 `set_record_enabled`，`web_preview_gateway` 转发给 `camera_codec_server`，`camera_codec_server` 订阅原始帧并编码为 H.264 文件。

实时画面始终显示原始预览流，不切换到编码后的 H.264 流。录制只在板端后台保存文件，并通过 `record_status` 回传状态。

## 2. 板端目录规范

向开发板传输文件时，统一放在 `/home/luckfox/CameraSubsystem` 下，不再散放到 `/home/luckfox` 根目录。

| 板端路径 | 用途 |
|----------|------|
| `/home/luckfox/CameraSubsystem/bin/` | 板端可执行文件，例如 `camera_publisher_example`、`web_preview_gateway`、`camera_codec_server`。 |
| `/home/luckfox/CameraSubsystem/web_preview/dist/` | Web Preview 前端静态资源，由 `web_preview_gateway` 提供 HTTP 服务。 |
| `/home/luckfox/CameraSubsystem/recordings/` | Web 录制输出目录，保存 `.h264` 文件。 |
| `/home/luckfox/CameraSubsystem/logs/` | 调试日志，例如 publisher、gateway、codec server 日志。 |
| `/home/luckfox/CameraSubsystem/run/` | 调试脚本 PID 文件。 |
| `/home/luckfox/CameraSubsystem/tmp/` | 调试脚本临时文件。 |
| `/home/luckfox/CameraSubsystem/scripts/` | 板端 smoke / stability 脚本。 |

首次部署前可手动创建目录：

```bash
ssh luckfox@192.168.31.9 \
  "mkdir -p /home/luckfox/CameraSubsystem/bin \
            /home/luckfox/CameraSubsystem/web_preview/dist \
            /home/luckfox/CameraSubsystem/recordings \
            /home/luckfox/CameraSubsystem/logs \
            /home/luckfox/CameraSubsystem/run \
            /home/luckfox/CameraSubsystem/tmp \
            /home/luckfox/CameraSubsystem/scripts"
```

## 3. 本机构建

从 `CameraSubsystem` 工程根目录执行：

```bash
./scripts/build-rk3576.sh
```

构建 Web Preview Gateway：

```bash
cd extensions/web_preview
./scripts/build-gateway-rk3576.sh
```

构建前端静态资源：

```bash
cd extensions/web_preview
./scripts/build-web.sh
```

## 4. 统一部署

推荐使用统一部署脚本：

```bash
./scripts/deploy-rk3576-web-debug.sh
```

默认部署到：

```text
luckfox@192.168.31.9:/home/luckfox/CameraSubsystem
```

可通过环境变量覆盖：

```bash
BOARD_HOST=192.168.31.9 \
BOARD_USER=luckfox \
REMOTE_ROOT=/home/luckfox/CameraSubsystem \
./scripts/deploy-rk3576-web-debug.sh
```

如果只更新前端静态资源，可以使用：

```bash
cd extensions/web_preview
./scripts/deploy-web.sh 192.168.31.9 luckfox /home/luckfox/CameraSubsystem/web_preview/dist
```

## 5. 板端启动顺序

登录开发板：

```bash
ssh luckfox@192.168.31.9
cd /home/luckfox/CameraSubsystem
```

启动前清理旧 socket：

```bash
rm -f /tmp/camera_subsystem_control.sock \
      /tmp/camera_subsystem_data.sock \
      /tmp/camera_subsystem_codec.sock
```

按顺序启动三个进程：

```bash
./bin/camera_publisher_example /dev/video45 \
  /tmp/camera_subsystem_control.sock \
  /tmp/camera_subsystem_data.sock \
  --io-method mmap \
  > logs/publisher.log 2>&1 &
```

```bash
./bin/camera_codec_server \
  --control-socket /tmp/camera_subsystem_control.sock \
  --data-socket /tmp/camera_subsystem_data.sock \
  --codec-socket /tmp/camera_subsystem_codec.sock \
  --device /dev/video45 \
  --output-dir /home/luckfox/CameraSubsystem/recordings \
  > logs/codec_server.log 2>&1 &
```

```bash
./bin/web_preview_gateway \
  --control-socket /tmp/camera_subsystem_control.sock \
  --data-socket /tmp/camera_subsystem_data.sock \
  --codec-socket /tmp/camera_subsystem_codec.sock \
  --device /dev/video45 \
  --port 8080 \
  --static-root /home/luckfox/CameraSubsystem/web_preview/dist \
  --output-dir /home/luckfox/CameraSubsystem/recordings \
  > logs/web_preview_gateway.log 2>&1 &
```

启动顺序必须是：

1. `camera_publisher_example`
2. `camera_codec_server`
3. `web_preview_gateway`

`web_preview_gateway` 启动时会连接 CameraSubsystem 控制面和数据面；如果 publisher 未就绪，Gateway 会连接失败并退出。Record 按钮依赖 `camera_codec_server`；如果 codec server 未启动，前端会收到 `codec_server_not_available`。

## 6. 浏览器调试

局域网 PC 浏览器访问：

```text
http://192.168.31.9:8080
```

当前前端功能：

| 功能 | 状态 | 说明 |
|------|------|------|
| 实时预览 | 已实现 | WebSocket 接收 JPEG payload，Canvas 显示画面。 |
| 开始 / 停止预览 | 已实现 | Play / Stop 按钮发送 subscribe / unsubscribe。 |
| 开始 / 停止录制 | 已实现 | Record 按钮发送 `set_record_enabled`，Gateway 转发给 `camera_codec_server`。 |
| 录制状态 | 已实现基础状态 | 前端处理 `record_status`，记录 recording、file、encoded_frames、decoded_frames 和错误。 |
| 快照 | 已实现 | 从 Canvas 保存 PNG。 |
| AI Detect | 暂未实现 | 按钮保留，后续接 AI 订阅端。 |

录制文件默认写入：

```text
/home/luckfox/CameraSubsystem/recordings
```

## 7. 录制验证

短 smoke：

```bash
ssh luckfox@192.168.31.9
sh /home/luckfox/CameraSubsystem/scripts/codec-v1-smoke-rk3576.sh
```

成功标志：

```text
CODEC_SMOKE_RESULT=PASS
```

脚本会检查：

1. `input_frames > 0`
2. `decoded_frames > 0`
3. `encoded_frames > 0`
4. `decode_failures == 0`
5. `.h264` 输出文件非空

长稳验证：

```bash
ssh luckfox@192.168.31.9
DURATION=300 sh /home/luckfox/CameraSubsystem/scripts/codec-stability-test-rk3576.sh
```

成功标志：

```text
STABILITY_TEST=PASS
```

## 8. 故障排查

| 现象 | 常见原因 | 处理 |
|------|----------|------|
| 浏览器无法打开页面 | Gateway 未启动或端口未开放 | 检查 `logs/web_preview_gateway.log`，确认监听 `0.0.0.0:8080`。 |
| 页面已打开但无画面 | Publisher 未启动或摄像头节点错误 | 检查 `logs/publisher.log`，确认 `/dev/video45` negotiated format 为 MJPG。 |
| 点击 Record 返回 `codec_server_not_available` | `camera_codec_server` 未启动或 socket 路径不一致 | 检查 `/tmp/camera_subsystem_codec.sock` 和 `logs/codec_server.log`。 |
| 录制文件为空 | 编码服务未收到帧或 encoder 未成功输出 packet | 先运行 `codec-v1-smoke-rk3576.sh`，查看 decoded/encoded counters。 |
| 前端还是旧页面 | `web_preview/dist` 未更新或浏览器缓存 | 重新运行 `build-web.sh` 和 `deploy-web.sh`，浏览器强制刷新。 |
| 文件分散在 `/home/luckfox` | 使用了旧文档或旧脚本 | 统一改用 `/home/luckfox/CameraSubsystem` 和本文命令。 |

## 9. 维护要求

1. 新增板端调试脚本时，默认 `REMOTE_ROOT` 必须是 `/home/luckfox/CameraSubsystem`。
2. 新增部署命令时，必须按 `bin/`、`web_preview/dist/`、`recordings/`、`logs/`、`run/`、`tmp/`、`scripts/` 分类。
3. 不要把新二进制、日志、录制文件或临时脚本直接放到 `/home/luckfox` 根目录。
4. Web Preview README 可以保留快速入口，但完整板端调试流程以本文为准。
