# CameraSubsystem Agent 工作指南

**最后更新:** 2026-05-14<br>
**开发路线:** [docs/DEVELOPMENT_ROADMAP.md](docs/DEVELOPMENT_ROADMAP.md)<br>
**适用范围:** 本文件面向在 `CameraSubsystem/` 目录内执行开发、评审、验证和文档维护任务的 AI Coding Agent。<br>
**上级指南:** 仓库根目录 `../AGENTS.md` 提供全景导航、跨子项目关系和通用构建命令；本文档只补充 CameraSubsystem 特有的**当前上下文、决策树和检查清单**。

---

## 目录

- [1. 当前开发上下文](#1-当前开发上下文)
- [2. Agent 工作流](#2-agent-工作流)
  - [2.1 修改前检查清单](#21-修改前检查清单)
  - [2.2 修改中约束](#22-修改中约束)
  - [2.3 修改后验证清单](#23-修改后验证清单)
- [3. 常见修改场景决策树](#3-常见修改场景决策树)
- [4. 当前主线优先级](#4-当前主线优先级)
- [5. 文档边界与引用规则](#5-文档边界与引用规则)
- [6. 板端上下文](#6-板端上下文)

---

## 1. 当前开发上下文

截至 2026-05-14，项目处于 **"USB/RK3576 主链路收敛 + MIPI/RKISP 实帧阻塞等待 + 低拷贝录制准备"** 阶段。

| 领域 | 当前事实 | Agent 决策影响 |
|------|----------|----------------|
| **本机构建** | `./scripts/build.sh` 可用 | 常规代码变更优先跑本机构建 |
| **RK3576 交叉编译** | `./scripts/build-rk3576.sh` 可用 | 平台相关变更必须跑交叉编译 |
| **DataPlaneV2** | 跨进程 fd 传递、ReleaseFrame、超时/断连回收、fd 泄漏长稳已完成 | 不要重复设计 release 协议；后续围绕 MIPI descriptor 扩展 |
| **多路身份模型** | `CameraStreamIdentity`、控制面/日志/DataPlaneV2/release key 已完成 M1-M3 纠偏 | **任何跨模块对象必须携带稳定 `stream_id`**，禁止把裸 `frame_id` 当全局键 |
| **Web Preview** | W1-W2 已完成（`stream_index -> stream_id` 映射） | **W3/W4 暂缓**。除必要 bugfix 不扩大前端功能 |
| **Codec Server** | 多 `RecordingSession`、raw H.264 和最小 MP4 已完成 | **快速收敛**。除 smoke/bugfix/低拷贝输入适配外不扩展新能力 |
| **MIPI/RKISP** | MPLANE readiness probe 和 probe-only 初始化骨架已完成 | **尚未完成真实 MIPI live STREAMON**。不要标记为已完成 |
| **板端 smoke** | `quick/full/extended` 三档 + `multi-camera-topology` 已接入 | 平台相关变更应通过 smoke；真实 MIPI 接入后升级为联合 smoke |
| **背压参数化** | `BackpressureConfig` / `DropPolicy` / 慢消费者检测已完成 | stress test 兼容；新增 4 个单元测试 |
| **统一 Metrics** | `core::StreamMetrics` + `IMetricsProvider` + `MetricsAggregator` 已完成 | CameraSource / FrameBroker 已接入；publisher 示例已替换手动聚合；10 个单元测试 |
| **CameraSource 恢复/降级** | USB 断连恢复、物理热插拔、`mmap/v1` enable=1 降帧降级已完成 | 降级默认关闭；DMA-BUF 重配置边界待后续独立验证，不要扩大为“全路径已完成” |

**最近关键板端结果（截至 2026-05-14）：**
- USB `/dev/video45` DataPlaneV2 live：`v2_sent=254`、`release_pending=0`、subscriber `frames=254`
- RKISP/RKVpss MPLANE readiness：`pass=10`、`fail=0`
- 统一 Metrics 接口：本地构建 13/13 测试通过，RK3576 交叉编译通过
- FrameBroker 背压：本地 4 个单元测试 + stress test 通过
- USB 物理热插拔恢复：拔出后进入 retrying，重插 `/dev/video45` 后恢复采集
- CameraSource 降帧降级：RK3576 `/dev/video45` USB UVC `mmap/v1` enable=1 已验证降级、恢复和 Stop 清理

---

## 2. Agent 工作流

### 2.1 修改前检查清单

- [ ] 阅读 `README.md` 当前状态节，确认是否已有相关实现
- [ ] 如涉及架构调整，先读 `docs/ARCHITECTURE_REVIEW.md` 对应 ARCH-* 项
- [ ] 如涉及多路摄像头/USB/MIPI，先读 `docs/MULTI_CAMERA_ARCHITECTURE.md`
- [ ] 如涉及 DMA-BUF/DataPlaneV2/release，先读 `docs/DMA_BUF_ZERO_COPY_ARCHITECTURE.md`
- [ ] 如涉及录制/编码，先读 `docs/CODEC_SERVER_ARCHITECTURE.md`
- [ ] 检查 `git status --short`，确认没有覆盖用户未提交改动
- [ ] 确认修改范围：只改必要的文件，不顺带格式化无关代码

### 2.2 写代码前置规则（硬性约束）

**任何代码开发前必须先完成文档迭代，禁止直接动手写代码。** 这是为了避免在全局状态未理清时产生垃圾代码、污染仓库。

1. **需求必须收敛到一句话**
   - 本次修改解决的具体问题是什么？禁止模糊表述如"优化性能"。
   - 不修改的边界是什么？明确哪些模块、哪些场景不在本次范围内。
   - 修改后如何验证？列出具体指标、命令、预期输出。

2. **架构文档必须先读、先引用、先更新**
   - 如涉及接口变更，先更新 `API_REFERENCE.md`。
   - 如涉及 DMA-BUF/DataPlaneV2，先读并引用 `DMA_BUF_ZERO_COPY_ARCHITECTURE.md`。
   - 如涉及多路摄像头，先读并引用 `MULTI_CAMERA_ARCHITECTURE.md`。
   - 如涉及录制/编码，先读并引用 `CODEC_SERVER_ARCHITECTURE.md`。
   - 如涉及架构评审风险，先读并引用 `ARCHITECTURE_REVIEW.md` 对应 ARCH-* 编号。

3. **风险清单必须闭合**
   - 是否存在新的死锁窗口？（锁顺序、join 时机、callback 持锁）
   - 是否影响现有板端 smoke？列出受影响的脚本和预期行为变化。
   - 是否引入向后不兼容的协议或接口变更？
   - 是否在无硬件条件下无法验证？标注"待 MIPI sensor 验证"或提供模拟方案。
   - 多路并发下是否可能出现 frame_id 碰撞、lease 泄漏或 fd drift？
   - **风险清单中不允许存在 "TODO"、"后续再定"、"先写代码再看"。**

4. **文档就绪标准**
   - ✅ 可以编码：所有相关架构文档已阅读并确认不冲突；如需更新架构文档，草稿已完成且无未闭合项；风险清单每项都有明确缓解策略；验证方案已明确。
   - ❌ 禁止编码：架构文档中存在 TODO/FIXME/后续再评估；无法回答 "如果真实 MIPI sensor 行为与预期不同，代码如何表现"；接口变更未同步到 API_REFERENCE；风险清单中存在 "先写代码再看"。

### 2.3 修改中约束

1. **多路身份强制规则**
   - 任何新增跨进程/跨模块对象（ReleaseFrame、DataPlaneV2 descriptor、record status、Web event、关键日志）必须能追溯到 `stream_id`
   - pending lease、release tracker 的 key 必须使用 `stream_id + frame_id + buffer_id` 多字段语义，禁止裸 `frame_id`

2. **接口调整前置规则**
   - 大范围接口调整（DataPlaneV2、MPLANE、Codec 低拷贝路径）**必须先更新对应架构文档**，讨论确认后再写代码
   - 新增公共接口必须同步更新 `API_REFERENCE.md`
   - 新增命名或目录约定必须同步更新 `NAMING_CONVENTION.md`

3. **代码风格**
   - 格式化以 `.clang-format` 为准，Allman 大括号、4 空格缩进、100 列
   - 命名规范见 `NAMING_CONVENTION.md`
   - 手工编辑优先使用补丁方式，避免无关重写

4. **Git 提交规范（硬性约束）**
   - 提交风格必须对齐项目既有提交 `eef4dcbfe74d2dbdbc87f1e9fd104c17f2b0527b`：标题使用 `[中文类别] 动词短语`，正文使用多条 `- ` 列表说明修改事实、边界和影响。
   - 标题方括号 `[]` 内必须是中文，不允许使用 `[docs]`、`[metrics]`、`[web_preview]`、`[chore]`、`[ARCH-008]` 这类英文、拼音或纯编号类别。
   - 标题类别必须具体，优先使用中文模块、阶段或用途，例如 `[架构] 接入 CameraSource 降帧降级策略`、`[网页预览] 修复录制停止后连接中断`、`[文档] 同步 RK3576 板端验证状态`、`[板端验证] 增加拓扑 smoke 自动判定`。ARCH 编号可以写在标题动词短语或正文中，例如 `[架构] 收敛 ARCH-008 降帧降级状态`。
   - 正文必须写 2-5 条 `- ` 列表；每条说明一个事实性修改范围，不写空泛描述，不写营销式总结。
   - 禁止写独立的 `验证：XXX` 段落；验证结果如需记录，只能作为正文列表中的事实，例如 `- RK3576 /dev/video45 mmap/v1 enable=1 降级与恢复验证通过`。
   - 禁止附带外部 AI 生成标记，例如 `Generated with ...`、`Co-Authored-By: ...`，除非用户明确要求。
   - 禁止把未完成事项写成已完成；如果板端、MIPI、DMA-BUF 或 enable=1 场景未验证，提交正文必须明确写出边界。
   - 拆提交必须按语义边界，不按文件类型机械拆分：核心代码、示例入口、配套单测可以在同一功能提交；纯文档收敛可单独提交；无关脚本重构必须单独提交。
   - 不允许为了凑提交数量拆出只有重复文档状态的提交；如果文档只是跟随某个功能状态变化，优先并入对应功能提交。
   - 提交前必须先检查 `git status --short` 和 `git diff --stat`，确认暂存范围只包含本次提交意图内的文件。
   - **严禁擅自删除、还原、覆盖用户已有改动**。任何不属于当前任务但已经存在的修改（例如 `scripts/count_loc.sh`）必须保留；如果需要排除，只能通过不暂存来拆分提交，不能 `git restore`、`git checkout --`、删除文件或重写内容。
   - 如果发现无关改动与当前任务混在一起，必须向用户说明拆分建议，并保持工作区内容不变；只有用户明确授权后才能清理或回滚。

   推荐提交示例：

   ```text
   [架构] 接入 CameraSource 降帧降级策略

   - 扩展 CameraConfig 与 StreamMetrics 降级字段，保持 CameraConfig ABI 大小不变
   - 在 CaptureLoop 成功 DQBUF 后接入滑动窗口 fps 评估与受控 STREAMOFF/S_PARM/STREAMON 重配置
   - 为 publisher 示例新增 enable-degradation 相关 CLI 开关，支持 RK3576 板端显式验证
   - 记录 mmap/v1 已验证边界，DMA-BUF enable=1 重配置留待后续独立验证
   ```

5. **生成文件隔离**
   - 生成文件、日志、构建产物、node_modules 不要加入提交
   - `.claude/` `.codeartsdoer/` 等外部助手目录是否提交由用户决定，不要默认整理或删除

### 2.4 修改后验证清单

| 修改范围 | 必做验证 |
|----------|----------|
| 核心库代码（`src/core/` `src/camera/` `src/broker/` `src/ipc/` `src/platform/`） | `./scripts/build.sh` 通过 + `ctest --output-on-failure` 通过 |
| 示例程序 | 本机构建后运行 `./bin/camera_publisher_example` + `./bin/camera_subscriber_example` 至少 5 秒无崩溃 |
| 平台相关代码（V4L2、DMA-BUF、MMAP） | `./scripts/build-rk3576.sh` 交叉编译通过 |
| 控制面/数据面 IPC 协议变更 | 单元测试通过 + 本机双进程示例通过 |
| Web Preview Gateway | `./extensions/web_preview/scripts/build-gateway.sh` 通过 |
| Web 前端 | `cd extensions/web_preview/web && npm run build` 通过 |
| Codec Server | `./scripts/build-rk3576.sh` 通过（交叉编译自动启用 codec_server） |
| 文档变更 | 确认对应架构文档、README、API_REFERENCE 已同步更新 |
| 板端相关变更 | 优先运行 `./scripts/rk3576-board-smoke-suite.sh` 或对应专项脚本；如果当前 Agent 环境无 SSH/密码能力，再提醒用户执行 |

---

## 3. 常见修改场景决策树

```
开始修改
│
├─→ 改的是文档？
│   ├─→ README / IMPLEMENTATION_STATUS → 只写事实，不重复评审建议
│   ├─→ ARCHITECTURE_REVIEW → 评审建议、风险、ARCH-* 唯一入口
│   ├─→ DMA_BUF_ZERO_COPY_ARCHITECTURE → DMA-BUF / DataPlaneV2 / release 权威文档
│   ├─→ MULTI_CAMERA_ARCHITECTURE → 多路身份、纠偏路线
│   ├─→ CODEC_SERVER_ARCHITECTURE → 录制状态机、MPP 输入路径
│   └─→ 其他文档 → 检查是否需要同步更新索引
│
├─→ 改的是接口/公共头文件？
│   └─→ 必须同步更新 API_REFERENCE.md
│
├─→ 改的是 V4L2/DMA-BUF/DataPlaneV2？
│   └─→ 先读 DMA_BUF_ZERO_COPY_ARCHITECTURE.md 对应章节
│       大范围调整先写文档评审，再编码
│
├─→ 改的是多路摄像头/stream identity？
│   └─→ 先读 MULTI_CAMERA_ARCHITECTURE.md
│       确认 stream_id 已贯通所有受影响模块
│
├─→ 改的是 Web Preview / Codec Server？
│   └─→ 只处理影响 smoke、错误收敛、多路身份正确性和低拷贝输入适配的问题
│       复杂 UI、自动续录、RTSP、H.265、MKV → 暂缓
│
└─→ 改的是板端脚本/smoke？
    └─→ 优先使用已有 RK3576 smoke 脚本验证；无法连接时记录阻塞条件并给出用户可执行命令
```

---

## 4. 当前主线优先级

开发重心按以下顺序推进，**不要偏离主线去扩展外围功能**。详细分析见 [docs/DEVELOPMENT_ROADMAP.md](docs/DEVELOPMENT_ROADMAP.md)。

### P0：硬件到位后立即执行

1. **真实 MIPI/RKISP live STREAMON** — MPLANE 骨架已就绪，需验证 STREAMON 后 DQBUF/QBUF 帧率稳定性、per-plane `bytesused` 真实性、timestamp 正确性、per-plane fd 端到端路径。
2. **DataPlaneV2 -> MPP 低拷贝录制实现** — 只在真实 NV12 DMA-BUF live frame 可验证后编码 fd path；USB MJPEG 继续保留 copy path。

### P1：不依赖新增摄像头的主线增强

3. **板端 Metrics smoke 自动判定** — 将 `StreamMetrics` 快照接入 smoke 阈值判断，减少日志 grep 和人工判断。
4. **DMA-BUF 降级重配置验证** — 针对 active lease 场景验证降级重配置的跳过、重试和清理边界。
5. **热插拔能力发现** — 针对不同 USB/MIPI 设备补 device discovery、节点重枚举和订阅端提示策略。

**明确暂缓项：** 复杂 UI 体验、自动续录、RTSP 推流、H.265、MKV 容器、分段录制、断电恢复

---

## 5. 文档边界与引用规则

| 文档 | 维护内容 | 其他文档如何处理 |
|------|----------|------------------|
| `README.md` | 团队入口、当前状态、工程结构、快速开始 | 只链接，不重复评审建议 |
| `IMPLEMENTATION_STATUS.md` | 完成度、测试状态、下一步计划 | 只链接，不重复风险 |
| `docs/ARCHITECTURE_REVIEW.md` | **评审建议、风险、ARCH-* 跟踪项的唯一权威入口** | 其他文档只链接引用 |
| `docs/MULTI_CAMERA_ARCHITECTURE.md` | USB + MIPI 多路目标架构、纠偏路线、验收口径 | 其他文档只链接引用 |
| `docs/DMA_BUF_ZERO_COPY_ARCHITECTURE.md` | DMA-BUF / DataPlaneV2 / release 生命周期、低拷贝边界 | 编码/录制文档引用，不重新定义 |
| `docs/CODEC_SERVER_ARCHITECTURE.md` | Codec server 架构、录制状态机、MPP 输入路径 | 引用 DMA-BUF 文档的生命周期契约 |
| `docs/BOARD_WEB_DEBUG_GUIDE.md` | RK3576 板端部署、Web 调试、录制和 smoke 方法 | 操作指南，不承载架构评审 |
| `API_REFERENCE.md` | 公开接口、数据结构、IPC 协议 | 只写接口与用法，不承载架构 |

**更新原则：**
1. 行为变化同步更新入口文档和对应架构文档
2. 已完成的阶段写成事实，未完成的 MIPI live 不能标记为完成
3. Web / Codec 的新增能力必须说明为什么服务主线 smoke 或低拷贝路径

---

## 6. 板端上下文

如果当前 Agent 环境具备 SSH 工具和用户授权，应优先使用已有脚本执行 RK3576 板端验证；如果无法连接，则基于以下上下文给出用户可直接执行的命令：

| 项目 | 当前值 |
|------|--------|
| 板卡 | Luckfox Omni3576 / RK3576 |
| 系统 | Debian 12 |
| IP | `192.168.31.9`（实验板） |
| SSH 用户 | `luckfox` |
| 统一部署目录 | `/home/luckfox/CameraSubsystem` |
| Web 地址 | `http://192.168.31.9:8080` |
| 当前 USB 摄像头 | `/dev/video45` |
| 常见 RKISP/RKVpss 节点 | `/dev/video22`、`/dev/video23`、`/dev/video31`、`/dev/video32`、`/dev/video41` 等 |

**板端目录约定：**

| 路径 | 内容 |
|------|------|
| `~/CameraSubsystem/bin/` | publisher、subscriber、codec_server、gateway 等二进制 |
| `~/CameraSubsystem/web_preview/dist/` | Web 前端静态资源 |
| `~/CameraSubsystem/recordings/` | `.h264` / `.mp4` 录制输出 |
| `~/CameraSubsystem/logs/` | 运行日志与 smoke 日志 |

**安全提醒：**
- 板端 smoke 会启停进程，运行前确认没有人工调试会话占用同一设备
- `/dev/video45` 当前是 USB 摄像头入口；RKISP/RKVpss readiness 只能证明 `REQBUFS + QUERYBUF + EXPBUF`，不能证明 live frame
- Web Gateway 默认监听 `0.0.0.0:8080`，仅适合受控局域网
- 遇到 `Text file busy` 或设备 busy，先检查残留进程，不要直接重启开发板
