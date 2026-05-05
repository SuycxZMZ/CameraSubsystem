# 板端 Web 预览与录制调试指南

**最后更新:** 2026-05-05<br>
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
- [10. 下一步计划](#10-下一步计划)

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
| 录制状态 | 已实现基础状态 | 前端处理 `record_status`，记录 recording、file、encoded_frames、decoded_frames 和错误；录制切换期间按钮进入 pending，避免重复点击。 |
| 快照 | 已实现 | 从 Canvas 保存 PNG。 |
| AI Detect | 暂未实现 | 按钮保留，后续接 AI 订阅端。 |

录制文件默认写入：

```text
/home/luckfox/CameraSubsystem/recordings
```

预览链路画质验证点：

1. Web 预览状态中 `format` 应为 `JPEG`，`resolution` 应与 publisher negotiated format 一致。
2. 开启 Record 后，实时画面仍来自 `web_preview_gateway` 订阅的原始 JPEG 预览流，不来自 H.264 录制输出。
3. 2026-04-29 板端短验证结果：`/status` 返回 `format=JPEG`、`width=1920`、`height=1080`、`input_frames>0`、`published_frames>0`，确认预览链路未进入编码降质路径。
4. 2026-05-05 板端正式目录验证结果：Web Record start/stop 后 `LIVE_WS_COUNTS before=15 during=25 after=25`，停止录制后 `/status` 仍返回 `streaming`，`0.0.0.0:8080` 保持监听。

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

Web 录制开关 smoke：

```bash
ssh luckfox@192.168.31.9
sh /home/luckfox/CameraSubsystem/scripts/web-record-freeze-smoke-rk3576.sh
```

成功标志：

```text
WS_COUNTS before=<正数> during=<正数> after=<正数>
```

`after` 必须大于 0，表示停止录制后 WebSocket 预览仍然持续出帧；脚本默认使用 `/home/luckfox/CameraSubsystem` 规范目录，不再依赖 `/home/luckfox` 根目录临时文件。

## 8. 故障排查

| 现象 | 常见原因 | 处理 |
|------|----------|------|
| 浏览器无法打开页面 | Gateway 未启动或端口未开放 | 检查 `logs/web_preview_gateway.log`，确认监听 `0.0.0.0:8080`。 |
| 停止录制后刷新报 `ERR_CONNECTION_REFUSED` | 正式目录仍在运行旧版 `web_preview_gateway` / `camera_publisher_example`，或 8080 进程已退出 | 确认 `/home/luckfox/CameraSubsystem/bin/` 中二进制已重新部署；检查 `pgrep -af web_preview_gateway`、`ss -lntp \| grep 8080` 和 `logs/web_preview_gateway.log`。 |
| 页面已打开但无画面 | Publisher 未启动或摄像头节点错误 | 检查 `logs/publisher.log`，确认 `/dev/video45` negotiated format 为 MJPG。 |
| 预览画面明显模糊 | 前端 Canvas backing store 与 CSS 显示尺寸不一致，或浏览器在较小卡片中下采样 1080p 原始画面 | 先确认 Gateway `/status` 中 `format=JPEG`、`width/height` 为摄像头原始分辨率；预览链路不走 H.264 编码。前端应使用 DPR-aware Canvas 渲染，并保持 contain 等比显示。 |
| 点击 Record 返回 `codec_server_not_available` | `camera_codec_server` 未启动或 socket 路径不一致 | 检查 `/tmp/camera_subsystem_codec.sock` 和 `logs/codec_server.log`。 |
| 录制文件为空 | 编码服务未收到帧或 encoder 未成功输出 packet | 先运行 `codec-v1-smoke-rk3576.sh`，查看 decoded/encoded counters。 |
| 前端还是旧页面 | `web_preview/dist` 未更新或浏览器缓存 | 重新运行 `build-web.sh` 和 `deploy-web.sh`，浏览器强制刷新。 |
| 文件分散在 `/home/luckfox` | 使用了旧文档或旧脚本 | 统一改用 `/home/luckfox/CameraSubsystem` 和本文命令。 |

预览画质排查顺序：

1. 检查 publisher 日志，确认摄像头 negotiated format 是 MJPG，分辨率符合预期，例如 1920x1080。
2. 打开 Gateway 状态接口或查看 Web 页面状态，确认 `format=JPEG`、`resolution` 与 publisher 一致。
3. 确认 Record 按钮只影响后台 H.264 保存，不改变实时预览数据源。
4. 如果源分辨率正确但画面仍发糊，优先检查前端 Canvas 渲染：Canvas backing store 应按 CSS 显示尺寸乘以 `devicePixelRatio` 设置，并使用等比 contain 绘制。
5. 如果浏览器窗口或卡片宽度明显小于原始 1080p 画面，页面会进行下采样显示；这是显示尺寸限制，不代表原始预览流被压缩。

## 9. 维护要求

1. 新增板端调试脚本时，默认 `REMOTE_ROOT` 必须是 `/home/luckfox/CameraSubsystem`。
2. 新增部署命令时，必须按 `bin/`、`web_preview/dist/`、`recordings/`、`logs/`、`run/`、`tmp/`、`scripts/` 分类。
3. 不要把新二进制、日志、录制文件或临时脚本直接放到 `/home/luckfox` 根目录。
4. Web Preview README 可以保留快速入口，但完整板端调试流程以本文为准。

## 10. 下一步计划

1. 将 `web-record-freeze-smoke-rk3576.sh` 纳入统一部署脚本，避免手工部署时遗漏最新 smoke 工具。
2. 增加 Web 录制长稳脚本，覆盖 5 到 30 分钟连续录制、重复 start/stop、浏览器断开重连和录制服务重启。
3. 补充 H.264 文件播放兼容验证，至少覆盖 `ffprobe` / `ffplay` 或板端可用播放器的基本可解码检查。
4. 梳理 `camera_codec_server` 与 `web_preview_gateway` 的生产化启动方式，后续可沉淀为 systemd service 或统一 run script。
