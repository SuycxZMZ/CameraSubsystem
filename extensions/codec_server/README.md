# Camera Codec Server

**最后更新:** 2026-05-14

> **文档硬规范**
>
> - 本项目的系统架构图、模块框图、部署拓扑图、数据路径框图和工程结构框图必须使用 `architecture-diagram` skill 生成独立 HTML / inline SVG 图表产物；每个 HTML 图必须同步导出同名 `.svg`，Markdown 中默认直接显示 SVG，并附完整 HTML 图表链接。
> - 时序图、状态机图、纯目录结构图等仍使用 Mermaid fenced code block（语言标识为 `mermaid`）。
> - 禁止新增 ASCII art/text 框图；普通日志、命令输出、代码片段按其原始语言使用 fenced code block。
> - 每份项目文档必须在文档元信息和硬规范之后维护 `## 目录`，目录至少覆盖二级标题，并使用相对链接或页内锚点。
> - Codec Server 当前只做录制 smoke、错误收敛和后续低拷贝输入适配；RTSP、H.265、MKV、分段录制、断电恢复暂缓。

## 目录

- [当前定位](#当前定位)
- [构建方式](#构建方式)
- [本机控制面调试](#本机控制面调试)
- [板端 v1 copy 数据面 smoke](#板端-v1-copy-数据面-smoke)

## 当前定位

`camera_codec_server` 是 CameraSubsystem 的 H.264 编码录制扩展服务。它作为普通订阅端接收原始 Camera 帧，在板端后台完成编码和文件保存；Web Preview 只负责转发录制控制命令和展示状态。

当前 USB copy path 已完成端到端录制闭环，包含多 RecordingSession、控制面、v1 copy 数据面订阅、MPP JPEG decode、MPP H.264 encode、raw H.264 与最小 MP4 文件写入。后续 DataPlaneV2 -> MPP fd path 等真实 MIPI sensor 到位后按 [../../docs/DATAPLANEV2_MPP_LOW_COPY_RECORDING_DESIGN.md](../../docs/DATAPLANEV2_MPP_LOW_COPY_RECORDING_DESIGN.md) 推进。

当前已完成：

- `CodecServerConfig` 启动参数解析。
- `RecordingFileWriter` 文件命名、目录创建、写入、flush、close、统计、可配置扩展名和冲突避让。
- `CodecControlServer` Unix Domain Socket JSON line 控制面。
- `RecordingSessionManager` 最小 start/status/stop 状态机。
- `CameraStreamSubscriber` v1 copy 数据面订阅模块，支持读取 CameraSubsystem 帧头和 payload 并统计 `input_frames`。
- `JpegDecodeStage` 已在 RK3576 交叉构建中接入 MPP MJPEG/JPEG 解码，输出 NV12 `DecodedImageFrame`；主机无 MPP 时仍保留 `jpeg_decoder_not_available` fallback。
- `H264MppEncoder` 已在 RK3576 交叉构建中接入 MPP H.264 编码，支持将 NV12 `DecodedImageFrame` 编码为裸 H.264 packet。
- `H264AnnexBParser` 已支持 Annex-B H.264 NAL 解析、SPS/PPS 提取和 slice 分类，为 MP4 muxer 接入做准备。
- `Mp4FileWriter` 已支持最小 MP4 写入，基于 SPS/PPS 生成 `avcC`，将 Annex-B NAL 转为 length-prefixed sample，正常 close 后输出可被 `ffprobe` 识别的 `.mp4`。
- `mpp_jpeg_decode_probe` 已在 RK3576 上验证单帧 JPEG 可通过 MPP 解码为 NV12。

当前 start recording 会打开裸 `.h264` 输出文件、订阅 CameraSubsystem v1 copy 数据面，并将 USB MJPEG/JPEG payload 送入 MPP JPEG decode，再将 NV12 帧送入 MPP H.264 encoder 写入文件。

`container=mp4` 已接入 `RecordingSessionManager` 控制分支，可通过 `codec-mp4-smoke-rk3576.sh` 在 RK3576 上直接验证 live `.mp4` 录制；Web/Gateway 已具备 MP4 参数入口，当前仍以 smoke 和稳定性为维护重点。

多 session 控制面已新增 `codec-multi-session-control-smoke-rk3576.sh`。该脚本使用 `--disable-camera-subscriber` 启动 codec server，只验证 `RecordingSessionManager` 的多路 session 状态隔离，不重复打开 USB 数据流。

RK3576 `/dev/video45` smoke 已验证：`camera_codec_server` 通过控制面 start/status/stop 后，`input_frames=94`、`decoded_frames=94`、`encoded_frames=94`、`decode_failures=0`、`write_failures=0`；输出 `.h264` 文件约 1.5MB。

板端调试文件统一部署到 `/home/luckfox/CameraSubsystem`，录制文件默认写入 `/home/luckfox/CameraSubsystem/recordings`。Web 预览和录制联调方式见 [../../docs/BOARD_WEB_DEBUG_GUIDE.md](../../docs/BOARD_WEB_DEBUG_GUIDE.md)。

当前阶段边界：

- USB JPEG/MJPEG 摄像头继续走 v1 copy path。
- 后续扩展 MIPI/RKISP NV12、DataPlaneV2、DMA-BUF 和 MPP import 低拷贝路径。
- 不直接访问 Camera 设备节点，不把编码逻辑塞进 `web_preview_gateway` 或 `camera_publisher`。
- 暂缓 RTSP、H.265、MKV、分段录制、断电恢复。

## 构建方式

```bash
cmake -S . -B build -DCAMERA_SUBSYSTEM_BUILD_CODEC_SERVER=ON
cmake --build build --target camera_codec_server
cmake --build build --target recording_file_writer_test recording_session_manager_test jpeg_decode_stage_test h264_mpp_encoder_test
```

也可以在本目录作为独立 CMake 子工程构建。

## 本机控制面调试

```bash
./bin/camera_codec_server \
  --codec-socket /tmp/camera_subsystem_codec.sock \
  --output-dir /tmp/camera_codec_recordings

printf '%s\n' \
  '{"type":"start_recording","request_id":"t1","stream_id":"usb_camera_0"}' \
  '{"type":"status","request_id":"t2","stream_id":"usb_camera_0"}' \
  '{"type":"stop_recording","request_id":"t3","stream_id":"usb_camera_0"}' \
  | nc -U /tmp/camera_subsystem_codec.sock
```

## 板端 v1 copy 数据面 smoke

```bash
sh /home/luckfox/CameraSubsystem/scripts/codec-v1-smoke-rk3576.sh
```
