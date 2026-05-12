# CameraSubsystem 下一阶段开发路线图

**最后更新:** 2026-05-10  
**适用范围:** 当前 `personal_kimi_vibe_develop` 分支，面向 CameraSubsystem 核心链路后续演进  
**关联文档:** [AGENTS.md](../AGENTS.md)、[ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)、[DMA_BUF_ZERO_COPY_ARCHITECTURE.md](DMA_BUF_ZERO_COPY_ARCHITECTURE.md)、[MULTI_CAMERA_ARCHITECTURE.md](MULTI_CAMERA_ARCHITECTURE.md)、[CODEC_SERVER_ARCHITECTURE.md](CODEC_SERVER_ARCHITECTURE.md)

---

## 目录

- [1. 当前基线](#1-当前基线)
- [2. 候选目标与可行性分析](#2-候选目标与可行性分析)
  - [2.1 目标 A：CameraSessionManager 回调持锁重构](#21-目标-acamerasessionmanager-回调持锁重构)
  - [2.2 目标 B：multi-camera-topology 强校验](#22-目标-bmulti-camera-topology-强校验)
  - [2.3 目标 C：DataPlaneV2 → MPP 低拷贝录制设计](#23-目标-cdataplanev2--mpp-低拷贝录制设计)
  - [2.4 目标 D：FrameBroker 背压参数化](#24-目标-dframebroker-背压参数化)
  - [2.5 目标 E：统一 Metrics 接口](#25-目标-e统一-metrics-接口)
  - [2.6 目标 F：真实 MIPI/RKISP live STREAMON](#26-目标-f真实-miipirkisp-live-streamon)
- [3. 下一步推荐](#3-下一步推荐)
- [4. 写代码前置检查清单](#4-写代码前置检查清单)

---

## 1. 当前基线

截至 2026-05-10，`personal_kimi_vibe_develop` 分支已完成以下里程碑：

| 里程碑 | 状态 | 关键产物 |
|--------|------|----------|
| M1 身份模型 | ✅ | `CameraStreamIdentity`、`stream_id` 贯通控制面/数据面/release/日志 |
| M2 publisher 多 runtime | ✅ | `stream_id -> CameraStreamRuntime` 映射，两路互不干扰 |
| M3 DataPlaneV2/release 多字段 key | ✅ | pending lease / release tracker 使用 `(stream_id, frame_id, buffer_id)` |
| M4 codec 多 session | ✅ | `stream_id -> RecordingSession`，单路 stop 不影响其他路 |
| M5 Web 多 stream 映射 | ✅ | Gateway status 携带 `stream_index`，前端归并到 `stream_id` |
| M6 topology smoke | ✅ | `rk3576-multi-camera-topology-smoke.sh` 覆盖 USB live + MIPI readiness |
| DataPlaneV2 Phase 2 | ✅ | `SCM_RIGHTS` fd 传递、独立 ReleaseFrame 通道、lifecycle smoke PASS |
| MPLANE 骨架 | ✅ | `InitMPlaneBuffers`、CaptureLoop MPLANE 适配、HandleDequeuedBuffer 扩展、RequeueBuffer MPLANE、release callback MPLANE |
| publisher 死锁修复 | ✅ | stop callback / cleanup 中移除 `runtime->mutex` 对 `source.Stop()` 的包裹 |
| FillFrameLayout 修复 | ✅ | `HandleDequeuedBufferDmaBuf` 中 MPLANE/single-planar 路径正确填充 plane size |

**当前唯一硬件约束：** 板端只有 USB 摄像头 `/dev/video45`，无真实 MIPI sensor。RKISP/RKVpss 节点已完成 MPLANE readiness probe（REQBUFS+QUERYBUF+EXPBUF）和 MPP DMA-BUF import probe，但 STREAMON 后真实出帧无法验证。

---

## 2. 候选目标与可行性分析

### 2.1 目标 A：CameraSessionManager 回调持锁重构

**需求来源:** ARCHITECTURE_REVIEW.md P0 风险 — "回调持锁执行"

**当前问题:**
- `CameraSessionManager::Subscribe()` / `Unsubscribe()` 在持有内部 `mutex_` 时直接调用外部 start/stop callback
- callback 中可能执行 V4L2 初始化、buffer 分配、线程 join 等耗时操作，阻塞其他订阅管理
- 已暴露的实例：publisher 示例中 `stop_callback_` 调用 `runtime->source.Stop()` 导致 capture thread join，与 capture thread 的 DataPlaneV2 callback 形成死锁（已修复示例层，但 `CameraSessionManager` 本身仍持锁执行）

**架构设计要点:**
1. 锁内只做状态决策和状态更新（session map、member map、引用计数）
2. 锁外执行 callback（start/stop），允许阻塞和异常
3. callback 完成后，如果需要回滚（start 失败），再获取锁回写状态
4. 保持现有 `CameraEndpoint` / `CameraStreamIdentity` 契约不变

**风险点:**
- 回调移出锁后，并发 Subscribe/Unsubscribe 的时序变复杂。例如：A 订阅触发 start，B 同时订阅同一 endpoint，需要确保不会重复 start
- 需要引入显式的 "starting" / "stopping" 中间态，防止重入
- 单元测试需要补充并发订阅/退订场景

**依赖条件:** 无硬件依赖，纯代码重构

**是否可立即开始:** ✅ **已完成**（2026-05-10）

---

### 2.2 目标 B：multi-camera-topology 强校验

**需求来源:** AGENTS.md 主线优先级 #2；MULTI_CAMERA_ARCHITECTURE.md M6 验收标准

**当前问题:**
- `rk3576-multi-camera-topology-smoke.sh` 当前只覆盖 USB live + MIPI readiness probe
- 缺少多路并发时的 identity 一致性校验（例如两路 stream 的 `stream_id` 是否冲突、device path 是否重复）
- 缺少 DataPlaneV2 lease 跨 stream 隔离验证（确认一路的 release 不会误触另一路的 buffer）
- 缺少 publisher 多 runtime 启停独立性的压力验证（启动 stream A 时 stream B 的帧率/lease 是否抖动）

**架构设计要点:**
1. 扩展 topology smoke 的校验矩阵：单 USB / 单 MIPI readiness / USB + MIPI readiness / 多 USB（如果硬件支持）
2. 增加 identity 冲突检测：同一 topology 中不允许重复的 `stream_id` 或 `device_path`
3. 增加 lease 隔离验证：subscriber 只收到自己订阅的 stream 的帧，release 只影响对应 stream 的 buffer
4. 增加并发启停验证：交替 start/stop 不同 stream，确认互不影响

**风险点:**
- 板端只有单路 USB，多 USB 场景需要 mock 或虚拟设备
- MIPI live 无法验证，当前只能做到 readiness 级别
- smoke 脚本变复杂后，运行时间增加，需要控制 TIER 分级

**依赖条件:** 无新硬件依赖，但多路并发验证受限于单 USB

**是否可立即开始:** ✅ **已完成（仅文档）**（2026-05-10）

---

### 2.3 目标 C：DataPlaneV2 → MPP 低拷贝录制设计

**需求来源:** AGENTS.md 主线优先级 #3；CODEC_SERVER_ARCHITECTURE.md 第 20 章 TODO；DMA_BUF_ZERO_COPY_ARCHITECTURE.md ARCH-010D

**当前问题:**
- `camera_codec_server` 当前走 v1 copy 数据面（USB MJPEG -> MPP JPEG decode -> MPP H.264 encode）
- DataPlaneV2 的 fd 传递和 ReleaseFrame 通道已就绪，但未接入 codec server
- MPP DMA-BUF import 已在 RKISP/RKVpss 节点验证，但缺少与 CameraSubsystem DataPlaneV2 的集成设计

**架构设计要点（必须先写文档）:**
1. **copy path vs fd path 选择条件**: USB MJPEG 必须走 copy（fd 指向的是压缩数据，MPP encoder 需要 NV12）；MIPI/RKISP NV12 优先走 fd path
2. **MPP import 输入契约**: MPP buffer 类型 `MPP_BUFFER_TYPE_EXT_DMA`、format、stride、width、height、pts、EOS 的映射规则
3. **release 时序**: codec server 作为 DataPlaneV2 subscriber，必须在 MPP 完成编码写入后才能发送 ReleaseFrame；需要明确 MPP encoder 的异步回调与 ReleaseFrame 的同步点
4. **多路隔离**: 每路 `RecordingSession` 独立作为 DataPlaneV2 consumer，consumer_id 与 `stream_id` 绑定
5. **fallback**: fd path 初始化失败时（例如 MPP import 返回错误），自动回退到 copy path，不中断录制

**风险点:**
- MPP encoder 的异步架构（`MppTask` / `MppFrame` / `MppPacket`）与 DataPlaneV2 的同步 release 语义存在时序摩擦
- 多路并发时，MPP 硬件资源有限，需要明确最大并发路数和排队策略
- 真实 MIPI 帧尚未可用，fd path 的设计只能基于 readiness probe 和文档推演，需要标注"待 MIPI sensor 验证"

**依赖条件:** 无硬件依赖可写设计文档；但 fd path 的板端验证需要真实 MIPI sensor

**是否可立即开始:** ✅ **是（仅限文档设计阶段）**

---

### 2.4 目标 D：FrameBroker 背压参数化

**需求来源:** ARCHITECTURE_REVIEW.md P1 风险 — "背压策略不足"；ARCH-004

**当前问题:**
- `FrameBroker` 队列满时直接跳过新任务，没有 `DropOldest`、按订阅者限流、按延迟阈值丢弃、慢消费者隔离等策略
- 策略硬编码在代码中，无法按 stream 或按场景配置

**架构设计要点:**
1. 定义 `BackpressureConfig` 结构：队列深度、延迟阈值、DropPolicy（DropNewest / DropOldest / DropLowPriority）
2. `FrameBroker` 支持按 subscriber 配置独立策略
3. 慢消费者检测：统计每个 subscriber 的队列等待时间和丢帧率，超过阈值时隔离或降级
4. 策略可配置来源：代码默认 -> 配置文件 -> 运行时 API 调整

**风险点:**
- 参数化后，策略组合爆炸，需要定义清晰的默认行为和无效配置拒绝规则
- 慢消费者隔离可能引入新的并发问题（隔离时如何优雅释放积压 buffer）
- 与 DataPlaneV2 lease 上限的交互：背压丢弃的帧需要确保 lease 正确释放

**依赖条件:** 无硬件依赖

**是否可立即开始:** ✅ **已完成**（2026-05-10）

---

### 2.5 目标 E：统一 Metrics 接口

**需求来源:** ARCHITECTURE_REVIEW.md P1 风险 — "指标体系不足"；ARCH-009

**当前问题:**
- 统计分散在 publisher 示例、CameraSource、FrameBroker 中，没有统一接口
- 指标命名不统一，没有采样周期、导出方式和故障快照规范
- 多路场景下缺少按 `stream_id` 标签的指标聚合

**架构设计要点:**
1. 在 `core` 层定义最小 Metrics 接口（或纯 C++ 结构），不引入外部依赖
2. 指标类别：采集 FPS、发布 FPS、队列深度、丢帧数、发送失败数、端到端延迟、lease 统计、fd drift
3. 每个指标必须携带 `stream_id` 标签
4. 导出方式：先支持内存快照和日志输出，后续可扩展为文件/网络导出

**风险点:**
- Metrics 接口设计过早绑定具体实现，可能限制后续扩展
- 采样本身带来性能开销，需要支持编译时/运行时开关
- 与现有 PlatformLogger 的日志统计重复，需要明确边界（metrics 是结构化聚合，日志是文本追踪）

**依赖条件:** 无硬件依赖

**是否可立即开始:** ✅ **是**

---

### 2.6 目标 F：真实 MIPI/RKISP live STREAMON

**需求来源:** AGENTS.md 主线优先级 #1；DMA_BUF_ZERO_COPY_ARCHITECTURE.md ARCH-010C

**当前问题:**
- MPLANE 代码骨架已写，但缺少真实 MIPI sensor 出帧验证
- 无法验证：STREAMON 后持续 DQBUF/QBUF 帧率稳定性、per-plane `bytesused` 真实性、timestamp/sequence 正确性、per-plane fd 经 DataPlaneV2 到 MPP/RGA import 的端到端路径

**架构设计要点:**
1. 确保 `CameraSource::InitMPlaneBuffers()` 的 live 初始化路径（S_FMT → REQBUFS → QUERYBUF → EXPBUF → QBUF → STREAMON）在真实 RKISP 节点上可执行
2. MPLANE DQBUF 后正确解析 `v4l2_plane.bytesused` 和 `data_offset`
3. 构造包含多 plane fd 的 `FrameDescriptor`，经 DataPlaneV2 传递给 subscriber
4. subscriber（如 MPP import probe 或 codec server）正确 import 并消费

**风险点:**
- **硬件缺失**: 当前无真实 MIPI sensor，所有验证只能基于代码审查和 readiness probe 推演
- 驱动行为差异: 不同 RKISP/RKVpss 节点的 plane 数量、fd 共享策略、bytesused 规则可能不同
- STREAMON 失败: 如果没有 sensor/media pipeline，STREAMON 可能直接失败或 DQBUF 永远阻塞

**依赖条件:** **必须有真实 MIPI sensor 和媒体管道配置**

**是否可立即开始:** ❌ **否（硬件阻塞）**

---

## 3. 下一步推荐

综合"优先级、不依赖硬件、可文档先行、风险可控"四个维度，推荐以下推进顺序：

| 顺序 | 目标 | 阶段 | 产出物 | 预计工作量 |
|------|------|------|--------|------------|
| **1** | **目标 A：CameraSessionManager 回调持锁重构** | ✅ 已完成 | `CameraSessionManager` 不再持锁执行 callback；新增并发订阅/退订单测 | 中 |
| **2** | **目标 B：multi-camera-topology 强校验** | ✅ 已完成 | topology smoke 增加 identity 冲突检测、lease 隔离验证、并发启停验证 | 小 |
| **3** | **目标 C：DataPlaneV2 → MPP 低拷贝录制设计** | ✅ 已完成（仅文档） | 设计文档明确 copy/fd path 选择、MPP import 契约、release 时序、fallback 策略 | 中 |
| **4** | **目标 D：FrameBroker 背压参数化** | ✅ 已完成 | `BackpressureConfig`、DropPolicy、慢消费者检测 | 中 |
| **5** | **目标 E：统一 Metrics 接口** | ✅ 已完成 | `core::StreamMetrics` + `IMetricsProvider` + `MetricsAggregator` | 小 |
| **阻塞等待** | **目标 F：真实 MIPI/RKISP live STREAMON** | 待 sensor 到位 | MPLANE 骨架已就绪，待真实 sensor 到位后立即进入 | 大 |

> **为什么不先做目标 C 的代码实现？**
>
> DataPlaneV2 → MPP 低拷贝录制涉及 DataPlaneV2 subscriber 侧全新接入路径、MPP 异步编码与同步 release 的时序摩擦、多路硬件资源限制。在没有真实 MIPI 帧的情况下，fd path 无法板端验证，容易写出"只能编译、无法确认正确"的代码。因此必须先完成设计文档，标注所有待验证假设，等 MIPI sensor 到位后再编码。

---

## 4. 写代码前置检查清单

本清单为硬性约束，**任何代码开发前必须逐项确认并输出对应文档**，不得跳过。

### 4.1 需求分析（必须输出到文档或注释）

- [ ] 本次修改解决的具体问题是什么？（用一句话描述，禁止模糊表述如"优化性能"）
- [ ] 不修改的边界是什么？（明确哪些模块、哪些场景不在本次范围内）
- [ ] 修改后如何验证正确性？（具体指标、具体命令、具体预期输出）

### 4.2 架构设计（必须更新对应架构文档）

- [ ] 如涉及接口变更，已更新 `API_REFERENCE.md`
- [ ] 如涉及 DMA-BUF/DataPlaneV2，已阅读并引用 `DMA_BUF_ZERO_COPY_ARCHITECTURE.md`
- [ ] 如涉及多路摄像头，已阅读并引用 `MULTI_CAMERA_ARCHITECTURE.md`
- [ ] 如涉及录制/编码，已阅读并引用 `CODEC_SERVER_ARCHITECTURE.md`
- [ ] 如涉及架构评审中的风险项，已阅读并引用 `ARCHITECTURE_REVIEW.md` 对应 ARCH-* 编号
- [ ] 新增公共数据结构或协议字段已说明生命周期、并发安全性和版本兼容性

### 4.3 风险点评估（必须列出并给出缓解策略）

- [ ] 是否存在新的死锁窗口？（锁顺序、join 时机、callback 持锁）
- [ ] 是否影响现有板端 smoke？（列出受影响的 smoke 脚本和预期行为变化）
- [ ] 是否引入向后不兼容的协议或接口变更？（版本协商、fallback、deprecated 计划）
- [ ] 是否在无硬件条件下无法验证的路径？（标注"待 MIPI sensor 验证"或提供模拟方案）
- [ ] 多路并发下是否可能出现 frame_id 碰撞、lease 泄漏或 fd drift？

### 4.4 文档就绪标准

以下情况**视为文档已就绪，可以开始编码**：

1. 所有相关架构文档已阅读并确认本次修改不与其冲突
2. 如需更新架构文档，文档草稿已完成并通过自我审查（无 TODO、无 "后续再定"、无模糊表述）
3. 风险清单中每一项都有明确缓解策略，不存在 "待讨论" 状态
4. 验证方案已明确：本地构建命令、单测场景、板端 smoke 命令、预期通过标准

以下情况**视为文档未就绪，禁止编码**：

1. 架构文档中存在 "TODO"、"FIXME"、"后续再评估" 等未闭合项
2. 无法回答 "如果真实 MIPI sensor 行为与预期不同，代码如何表现"
3. 接口变更未同步到 `API_REFERENCE.md`
4. 风险清单中任一项的缓解策略为 "先写代码再看"

---

**分支约束:**

- 所有开发必须在 `personal_kimi_vibe_develop` 分支进行
- 未经用户明确命令，**严禁执行 `git checkout main`、`git merge main`、`git rebase main` 或任何合回 main 的操作**
- 提交前执行 `git status --short` 确认修改范围，禁止提交无关文件
