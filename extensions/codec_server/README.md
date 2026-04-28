# Camera Codec Server

`camera_codec_server` 是 CameraSubsystem 的 H.264 编码录制扩展服务。它作为普通订阅端接收原始 Camera 帧，在板端后台完成编码和文件保存；Web Preview 只负责转发录制控制命令和展示状态。

当前实现从最小可运行进程开始，按 [../../docs/CODEC_SERVER_ARCHITECTURE.md](../../docs/CODEC_SERVER_ARCHITECTURE.md) 的顺序逐步接入控制面、订阅端、JPEG 解码、MPP H.264 编码和 Web 录制按钮。

当前已完成：

- `CodecServerConfig` 启动参数解析。
- `RecordingFileWriter` 文件命名、目录创建、写入、flush、close 和统计。
- `CodecControlServer` Unix Domain Socket JSON line 控制面。
- `RecordingSessionManager` 最小 start/status/stop 状态机。
- `CameraStreamSubscriber` v1 copy 数据面订阅模块，支持读取 CameraSubsystem 帧头和 payload 并统计 `input_frames`。

当前 start recording 会打开裸 `.h264` 输出文件并尝试订阅 CameraSubsystem v1 copy 数据面；当前只统计输入帧，尚未接入 JPEG 解码和 MPP H.264 编码。

RK3576 `/dev/video45` smoke 已验证：`camera_codec_server` 通过控制面 start/status/stop 后，`input_frames` 能从 0 增长到 114；输出 `.h264` 文件当前为空符合阶段预期。

第一阶段目标：

- 先以 USB JPEG/MJPEG 摄像头打通 H.264 文件录制链路。
- 后续扩展 MIPI/RKISP NV12、DataPlaneV2、DMA-BUF 和 MPP import 低拷贝路径。
- 不直接访问 Camera 设备节点，不把编码逻辑塞进 `web_preview_gateway` 或 `camera_publisher`。

构建方式：

```bash
cmake -S . -B build -DCAMERA_SUBSYSTEM_BUILD_CODEC_SERVER=ON
cmake --build build --target camera_codec_server
cmake --build build --target recording_file_writer_test recording_session_manager_test
```

也可以在本目录作为独立 CMake 子工程构建。

本机控制面调试：

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

板端 v1 copy 数据面 smoke：

```bash
sh /home/luckfox/codec-v1-smoke-rk3576.sh
```
