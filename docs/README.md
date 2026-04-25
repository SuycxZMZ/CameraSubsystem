# CameraSubsystem 文档索引

**最后更新:** 2026-04-25

> **文档硬规范**
>
> - 本项目所有流程图、框图、时序图、状态机图、目录结构图等图示必须使用 Mermaid fenced code block（语言标识为 `mermaid`）。
> - 禁止新增 ASCII art/text 框图；普通日志、命令输出、代码片段按其原始语言使用 fenced code block。
> - 每份项目文档必须在文档元信息和硬规范之后维护 `## 目录`，目录至少覆盖二级标题，并使用相对链接或页内锚点。
> - `README.md` 是团队入口文档，开头必须维护工程结构概览、项目文档索引和常用入口链接。
> - 评审建议、风险、ARCH-* 跟踪项只维护在 [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)，其他文档只链接引用，避免重复漂移。

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
3. [../structure.md](../structure.md)
4. [../API_REFERENCE.md](../API_REFERENCE.md)

### 做代码开发

1. [../README.md](../README.md)
2. [../API_REFERENCE.md](../API_REFERENCE.md)
3. [../NAMING_CONVENTION.md](../NAMING_CONVENTION.md)
4. [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)

### 做 RK3576 板端适配

1. [../README.md](../README.md)
2. [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)
3. [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)
4. `../cmake/toolchains/rk3576.cmake`
5. `../scripts/build-rk3576.sh`

---

## 2. 文档职责边界

| 文档 | 职责 | 不应承担 |
|------|------|----------|
| [../README.md](../README.md) | 团队入口、工程结构、常用命令、当前事实摘要 | 长篇设计细节、评审建议全集、API 全量说明 |
| [PROJECT_OVERVIEW.md](PROJECT_OVERVIEW.md) | 项目定位、技术栈、功能边界、快速开始 | 风险优先级、ARCH-* 详细跟踪 |
| [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md) | 系统/代码架构评审、风险、建议、ARCH-* 跟踪 | 基础使用教程、接口完整定义 |
| [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md) | 模块完成度、测试状态、下一步计划、技术债务执行状态 | 重复架构评审正文 |
| [../API_REFERENCE.md](../API_REFERENCE.md) | 公开接口、数据结构、IPC 协议、示例调用 | 设计争议与风险讨论 |
| [../NAMING_CONVENTION.md](../NAMING_CONVENTION.md) | 命名、目录、代码格式、跨平台约定 | 项目状态与路线图 |
| [../structure.md](../structure.md) | 完整架构设计长文、历史设计语境 | 当前唯一事实源 |

---

## 3. 文档维护规则

1. README 只保留入口级信息，超过两屏的细节应移动到专题文档。
2. 同一个事实只能有一个权威来源，其他文档用链接引用。
3. 架构评审建议只写在 [ARCHITECTURE_REVIEW.md](ARCHITECTURE_REVIEW.md)。
4. API 变化必须同步 [../API_REFERENCE.md](../API_REFERENCE.md)。
5. 构建、脚本、交叉编译入口变化必须同步 [../README.md](../README.md) 和 [../IMPLEMENTATION_STATUS.md](../IMPLEMENTATION_STATUS.md)。
6. 文档中的图示必须使用 Mermaid；目录结构图也使用 Mermaid，不使用 ASCII 树。
7. `third_party/` 下游文档不按本项目规范改写，避免污染上游来源。

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
