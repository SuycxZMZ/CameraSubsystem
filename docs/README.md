# CameraSubsystem 文档索引

**最后更新:** 2026-05-17

> **文档硬规范**
>
> - 本项目的系统架构图、模块框图、部署拓扑图、数据路径框图和工程结构框图必须使用 `architecture-diagram` skill 生成独立 HTML / inline SVG 图表产物；每个 HTML 图必须同步导出同名 `.svg`，Markdown 中默认直接显示 SVG，并附完整 HTML 图表链接。
> - 时序图、状态机图、纯目录结构图等仍使用 Mermaid fenced code block（语言标识为 `mermaid`）。
> - 禁止新增 ASCII art/text 框图；普通日志、命令输出、代码片段按其原始语言使用 fenced code block。
> - 每份项目文档必须在文档元信息和硬规范之后维护 `## 目录`，目录至少覆盖二级标题，并使用相对链接或页内锚点。
> - `README.md` 是团队入口文档，开头必须维护工程结构概览、项目文档索引和常用入口链接。
> - 评审建议、风险、ARCH-* 跟踪项只维护在 [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)，其他文档只链接引用，避免重复漂移。
> - Git 提交信息必须遵循 [../AGENTS.md](../AGENTS.md) 中的硬性规范；禁止附带外部 AI 生成标记，禁止擅自删除或还原用户已有改动。

---

## 目录

- [1. 推荐阅读路径](#1-推荐阅读路径)
- [2. 文档职责边界](#2-文档职责边界)
- [3. 文档维护规则](#3-文档维护规则)
- [4. 文档健康检查](#4-文档健康检查)

---

## 1. 推荐阅读路径

### 新成员快速了解项目

1. [../README.md](../README.md)
2. [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md)
3. [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)

### 做架构设计或评审

1. [../README.md](../README.md)
2. [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)
3. [MULTI_CAMERA_ARCHITECTURE.md](MULTI_CAMERA_ARCHITECTURE.md)
4. [DMA_BUF_ZERO_COPY_ARCHITECTURE.md](DMA_BUF_ZERO_COPY_ARCHITECTURE.md)
5. [CODEC_SERVER_ARCHITECTURE.md](CODEC_SERVER_ARCHITECTURE.md)
6. [METRICS_INTERFACE_DESIGN.md](METRICS_INTERFACE_DESIGN.md)
7. [DATAPLANEV2_MPP_LOW_COPY_RECORDING_DESIGN.md](DATAPLANEV2_MPP_LOW_COPY_RECORDING_DESIGN.md)
8. [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md)
9. [../API_REFERENCE.md](../API_REFERENCE.md)
10. [../structure.md](../structure.md)（历史背景材料）

### 做代码开发

1. [../README.md](../README.md)
2. [../API_REFERENCE.md](../API_REFERENCE.md)
3. [../NAMING_CONVENTION.md](../NAMING_CONVENTION.md)
4. [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)

### 做板端适配与交叉编译

1. [../README.md](../README.md)
2. [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)
3. [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)
4. [BOARD_WEB_DEBUG_GUIDE.md](BOARD_WEB_DEBUG_GUIDE.md)
5. 参考当前已接入的 RK3576 示例：`../cmake/toolchains/rk3576.cmake`
6. 参考当前已接入的 RK3576 示例：`../scripts/build-rk3576.sh`
7. 统一部署与启动入口：`../scripts/rk3576-build-deploy-debug.sh`

### 做 Web 预览与录制联调

1. [BOARD_WEB_DEBUG_GUIDE.md](BOARD_WEB_DEBUG_GUIDE.md)
2. [../extensions/web_preview/README.md](../extensions/web_preview/README.md)
3. [CODEC_SERVER_ARCHITECTURE.md](CODEC_SERVER_ARCHITECTURE.md)
4. [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)

---

## 2. 文档职责边界

| 文档 | 职责 | 不应承担 |
|------|------|----------|
| [../README.md](../README.md) | 团队入口、工程结构、常用命令、当前事实摘要 | 长篇设计细节、评审建议全集、API 全量说明 |
| [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md) | 项目定位、技术栈、功能边界、快速开始 | 风险优先级、ARCH-* 详细跟踪 |
| [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md) | 系统/代码架构评审、风险、建议、ARCH-* 跟踪 | 基础使用教程、接口完整定义 |
| [MULTI_CAMERA_ARCHITECTURE.md](MULTI_CAMERA_ARCHITECTURE.md) | USB + MIPI 多路摄像头同时接入的目标架构、当前偏差、身份模型和迁移计划 | 单路 smoke 命令、底层 DMA-BUF fd 生命周期细节 |
| [DMA_BUF_ZERO_COPY_ARCHITECTURE.md](DMA_BUF_ZERO_COPY_ARCHITECTURE.md) | DMA-BUF 数据面阶段性设计与验证记录，包含 RK3576 验证、FrameLease、DataPlaneV2、RGA/MPP import 边界 | H.264 编码录制架构、当前完成度统计、接口全量参考 |
| [CODEC_SERVER_ARCHITECTURE.md](CODEC_SERVER_ARCHITECTURE.md) | H.264 编码录制服务架构，包含 `camera_codec_server`、Web 录制控制、USB 首阶段链路和 MIPI/RKISP 扩展路径 | DMA-BUF 底层协议细节、具体 C++ 实现 |
| [METRICS_INTERFACE_DESIGN.md](METRICS_INTERFACE_DESIGN.md) | 统一 Metrics 接口设计：`core::StreamMetrics`、`IMetricsProvider`、`MetricsAggregator`；按 `stream_id` 标签聚合；与现有零散统计的整合路径 | 具体性能优化方案、生产级监控后端选型 |
| [DATAPLANEV2_MPP_LOW_COPY_RECORDING_DESIGN.md](DATAPLANEV2_MPP_LOW_COPY_RECORDING_DESIGN.md) | DataPlaneV2 → MPP 低拷贝录制设计：copy path / fd path 选择条件、MPP import 契约、ReleaseFrame 时序、fallback 策略 | 具体编码实现（待 MIPI sensor 到位） |
| [DEVELOPMENT_ROADMAP.md](DEVELOPMENT_ROADMAP.md) | 开发路线图：当前阶段划分、主线优先级、暂缓项和编码准入门槛 | 各模块详细架构设计 |
| [BOARD_WEB_DEBUG_GUIDE.md](BOARD_WEB_DEBUG_GUIDE.md) | RK3576 板端统一部署、板端一键启动、浏览器调试和 smoke 回归入口 | 架构取舍、API 全量说明、长期路线图 |
| [RKNN_SDK_DEMO_GUIDE.md](RKNN_SDK_DEMO_GUIDE.md) | 当前 Omni3576 SDK 的 RKNN demo 适配说明：为什么先收敛到 SDK 自带 demo、如何交叉编译、如何部署到板端运行、主机侧模型转换环境限制 | 最新模型选型、升级后的 runtime/toolkit 兼容矩阵 |
| [RKNN_OFFICIAL_STACK_GUIDE.md](RKNN_OFFICIAL_STACK_GUIDE.md) | 官方最新 RKNN 工具链并行接入说明：host 侧同步 `rknn-toolkit2` / `rknn_model_zoo`、默认 `yolo11` 转换与 RK3576 离线部署链路；当前已完成 host + board 闭环，`rknn-llm` 仅作为未来可选扩展记录 | 旧 SDK `2.0.0b0` 基线细节 |
| [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) | 模块完成度、测试状态、下一步计划、技术债务执行状态 | 重复架构评审正文 |
| [../API_REFERENCE.md](../API_REFERENCE.md) | 公开接口、数据结构、IPC 协议、示例调用 | 设计争议与风险讨论 |
| [../NAMING_CONVENTION.md](../NAMING_CONVENTION.md) | 命名、目录、代码格式、跨平台约定 | 项目状态与路线图 |
| [../structure.md](../structure.md) | 完整架构设计长文、历史设计语境 | 当前唯一事实源；如与 README/IMPLEMENTATION_STATUS/ARCHITECTURE_REVIEW 冲突，以后三者为准 |

---

## 3. 文档维护规则

1. README 只保留入口级信息，超过两屏的细节应移动到专题文档。
2. 同一个事实只能有一个权威来源，其他文档用链接引用。
3. 架构评审建议只写在 [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)。
4. API 变化必须同步 [../API_REFERENCE.md](../API_REFERENCE.md)。
5. 构建、脚本、交叉编译入口变化必须同步 [../README.md](../README.md) 和 [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)。
6. 文档中的系统架构图、模块框图、部署拓扑图、数据路径框图和工程结构框图必须使用 `architecture-diagram` skill；每个 HTML 图必须同步导出同名 `.svg`，Markdown 中默认直接显示 SVG，并附完整 HTML 图表链接。目录结构说明、时序图、状态机图继续使用 Mermaid，不使用 ASCII 树。
7. `third_party/` 下游文档不按本项目规范改写，避免污染上游来源。
8. 已完成阶段的临时设计文档应优先并回 README、IMPLEMENTATION_STATUS、DEVELOPMENT_ROADMAP 或对应专题权威文档，避免长期滞留在 `docs/design/` 下。

---

## 4. 文档健康检查

建议在较大文档改动后执行：

```bash
rg -n -P '[\x{2500}-\x{257F}]' README.md docs IMPLEMENTATION_STATUS.md API_REFERENCE.md NAMING_CONVENTION.md structure.md
git diff --check
```

预期结果：

1. 第一条命令不应在项目自有文档中发现 ASCII 框线图。
2. `git diff --check` 不应出现 trailing whitespace 或 whitespace error。
