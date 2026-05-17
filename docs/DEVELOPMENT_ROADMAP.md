# CameraSubsystem 下一阶段开发路线图

**最后更新:** 2026-05-17<br>
**适用范围:** 当前 CameraSubsystem 主线开发，面向 RK3576 / Debian 板端验证与后续 USB + MIPI 多路摄像头演进<br>
**关联文档:** [AGENTS.md](../AGENTS.md)、[ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)、[MULTI_CAMERA_ARCHITECTURE.md](MULTI_CAMERA_ARCHITECTURE.md)、[DMA_BUF_ZERO_COPY_ARCHITECTURE.md](DMA_BUF_ZERO_COPY_ARCHITECTURE.md)、[CODEC_SERVER_ARCHITECTURE.md](CODEC_SERVER_ARCHITECTURE.md)、[DATAPLANEV2_MPP_LOW_COPY_RECORDING_DESIGN.md](DATAPLANEV2_MPP_LOW_COPY_RECORDING_DESIGN.md)

> **文档硬规范**
>
> - 本项目的系统架构图、模块框图、部署拓扑图、数据路径框图和工程结构框图必须使用 `architecture-diagram` skill 生成独立 HTML / inline SVG 图表产物；每个 HTML 图必须同步导出同名 `.svg`，Markdown 中默认直接显示 SVG，并附完整 HTML 图表链接。
> - 时序图、状态机图、纯目录结构图等仍使用 Mermaid fenced code block（语言标识为 `mermaid`）。
> - 禁止新增 ASCII art/text 框图；普通日志、命令输出、代码片段按其原始语言使用 fenced code block。
> - 每份项目文档必须在文档元信息和硬规范之后维护 `## 目录`，目录至少覆盖二级标题，并使用相对链接或页内锚点。
> - `README.md` 是团队入口文档，开头必须维护工程结构概览、项目文档索引和常用入口链接。
> - 评审建议、风险、ARCH-* 跟踪项只维护在 [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)，其他文档只链接引用，避免重复漂移。

---

## 目录

- [1. 当前基线](#1-当前基线)
- [2. 架构师判断](#2-架构师判断)
- [3. 优先级路线](#3-优先级路线)
  - [3.1 P0：硬件到位后立即推进](#31-p0硬件到位后立即推进)
  - [3.2 P1：不依赖新增摄像头的主线增强](#32-p1不依赖新增摄像头的主线增强)
  - [3.3 P2：暂缓或轻量维护](#33-p2暂缓或轻量维护)
- [4. 编码准入门槛](#4-编码准入门槛)
- [5. 阶段验收口径](#5-阶段验收口径)

---

## 1. 当前基线

截至 2026-05-14，当前主线已经完成以下基线能力：

| 能力 | 当前状态 | 后续边界 |
|------|----------|----------|
| 多路身份模型 | `CameraStreamIdentity`、`stream_id`、控制面/数据面/release/log/status 已贯通 | 新增跨模块对象必须继续携带稳定 `stream_id` |
| DataPlaneV2 | `SCM_RIGHTS` fd 传递、独立 ReleaseFrame、超时/断连回收、fd 泄漏长稳已完成 | 真实 MIPI live fd path 仍待 sensor 到位验证 |
| Web Preview | W1-W2 已完成，`stream_index -> stream_id` 映射稳定归并到 stream card | W3/W4 暂缓，只保留 smoke/bugfix |
| Codec Server | 多 `RecordingSession`、raw H.264、最小 MP4、Web 录制闭环已完成 | DataPlaneV2 -> MPP fd path 待真实 NV12 DMA-BUF |
| CameraSource 恢复 | 自动恢复默认关闭；RK3576 USB 物理拔插/重插恢复已验证 | 设备节点重枚举仍待 device discovery 策略 |
| CameraSource 降级 | `mmap/v1` enable=1 降级/恢复/Stop 清理已验证 | DMA-BUF active lease 重配置仍需专项验证 |
| Metrics | `StreamMetrics`、`IMetricsProvider`、`MetricsAggregator` 已完成；USB lifecycle / topology 已接入结构化判定 | 不再单独维护 topology metrics 阶段文档，后续直接在主文档演进 |
| MPLANE | readiness probe 和初始化骨架已完成 | 无真实 MIPI sensor，不能标记 live 完成 |

当前硬件事实：板端只有 USB 摄像头 `/dev/video45`，没有真实 MIPI sensor。RKISP/RKVpss 节点的 `REQBUFS + QUERYBUF + EXPBUF` readiness 已验证，但这不能替代 STREAMON 后真实出帧验证。

## 2. 架构师判断

当前系统已经不缺“再加一个外围功能”，缺的是把主链路变成可持续验证的工程基线。下一阶段必须坚持三个边界：

1. **主线优先于扩展功能**：Web 和 Codec 当前只服务调试、录制 smoke 和低拷贝输入适配，不再扩展复杂 UI、RTSP、H.265、MKV、分段录制等产品功能。
2. **可验证优先于纸面完整**：没有真实 MIPI live frame 前，不写无法板端闭环的 fd path 生产实现；可以补设计、脚本入口和 readiness 检查。
3. **多路能力必须以 identity 和 metrics 为约束**：任何新路径都要按 `stream_id` 归因，并能通过结构化 metrics 判断 PASS/FAIL。

## 3. 优先级路线

### 3.1 P0：当前收口阶段

| 顺序 | 任务 | 产出 | 验收口径 |
|------|------|------|----------|
| 1 | USB/RK3576 主链路封板 | 现有 board smoke 回归、已知边界文档化 | 不新增新的 smoke 维度；现有入口可稳定回归 |
| 2 | 设备发现能力定版 | M0/M1/M2a/M2b/M3 状态同步到主文档 | 一次性 hook 边界明确，方向 B 暂不实现 |
| 3 | 文档与计划收口 | README、实现状态、路线图统一 | 明确 USB 基线完成，MIPI/低拷贝录制后移；阶段性设计文档并回主线 |

### 3.2 P1：硬件到位后再开启

| 顺序 | 任务 | 产出 | 验收口径 |
|------|------|------|----------|
| 1 | 真实 MIPI/RKISP live STREAMON | MPLANE live smoke、per-plane layout 记录、DataPlaneV2 subscriber 实帧验证 | 持续 DQBUF/QBUF、`bytesused > 0`、timestamp/sequence 单调、release 后持续采集 |
| 2 | DataPlaneV2 -> MPP 低拷贝录制实现 | codec server fd path、MPP import、编码完成后 ReleaseFrame | `NV12 + kDmaBuf + plane_count==1` 可录制，失败可回退 copy path，`release_pending` 最终归零 |
| 3 | DMA-BUF 降级重配置专项验证 | active lease 场景下的降级/恢复专项 | 未 release fd 时跳过或延后重配置，Stop 后 fd drift=0 |

### 3.3 P2：暂缓或轻量维护

| 领域 | 当前策略 |
|------|----------|
| Web Preview | 只修影响 smoke、状态归并、错误收敛的问题 |
| Codec Server | 只维护 raw_h264/mp4 当前链路和后续 fd path 入口 |
| 新容器/推流 | RTSP、H.265、MKV、分段录制、断电恢复暂缓 |
| 大平台抽象 | Android HAL、配置中心、复杂能力协商暂缓 |

## 4. 编码准入门槛

进入下一项代码开发前必须满足：

1. 需求一句话能说清，且明确不做范围。
2. 相关设计文档没有未闭合的 TODO/FIXME。
3. 验证方式可执行，包含命令、指标和预期结果。
4. 多路对象具备 `stream_id` 归因。
5. 涉及 DataPlaneV2/DMA-BUF 时，必须说明 lease 生命周期和 ReleaseFrame 时序。
6. 涉及板端行为时，必须说明是否能在现有 `/dev/video45` 上验证；不能验证的部分必须标注硬件阻塞。
7. 新增脚本前必须先判断能否并入 `rk3576-build-deploy-debug.sh`、`rk3576-board-debug-stack.sh` 或现有 smoke 入口，禁止继续扩散平行启动脚本。

## 5. 阶段验收口径

下一阶段可以认为“完成”的标准：

1. RK3576 quick/full smoke 使用 metrics 快照完成自动 PASS/FAIL。
2. CameraSource 降级在 `mmap/v1` 与 DataPlaneV2 active lease 边界都有明确结论。
3. 有真实 MIPI sensor 后，MPLANE live STREAMON 和 DataPlaneV2 实帧传递进入 smoke。
4. Codec fd path 只在真实 NV12 DMA-BUF 实帧验证后合入，不以“能编译”作为完成标准。
5. README、IMPLEMENTATION_STATUS、ARCHITECTURE_REVIEW 与本路线图保持同一事实口径。
