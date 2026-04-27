# Camera Codec Server

`camera_codec_server` 是 CameraSubsystem 的 H.264 编码录制扩展服务。它作为普通订阅端接收原始 Camera 帧，在板端后台完成编码和文件保存；Web Preview 只负责转发录制控制命令和展示状态。

当前实现从最小可运行进程开始，按 [../../docs/CODEC_SERVER_ARCHITECTURE.md](../../docs/CODEC_SERVER_ARCHITECTURE.md) 的顺序逐步接入控制面、订阅端、JPEG 解码、MPP H.264 编码和 Web 录制按钮。

第一阶段目标：

- 先以 USB JPEG/MJPEG 摄像头打通 H.264 文件录制链路。
- 后续扩展 MIPI/RKISP NV12、DataPlaneV2、DMA-BUF 和 MPP import 低拷贝路径。
- 不直接访问 Camera 设备节点，不把编码逻辑塞进 `web_preview_gateway` 或 `camera_publisher`。

构建方式：

```bash
cmake -S . -B build -DCAMERA_SUBSYSTEM_BUILD_CODEC_SERVER=ON
cmake --build build --target camera_codec_server
```

也可以在本目录作为独立 CMake 子工程构建。
