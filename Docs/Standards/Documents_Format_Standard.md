# Personal Finance Hub 文档格式与治理规范

Version: 2.0
Status: Active

---

## 1. 内容原则

### 1.1 结果投影

现行文档只保留读者完成开发、测试、部署或维护所需的信息：

- 当前有效的行为和架构边界。
- 已交付能力与最终验收结果。
- 可执行的命令、配置和接口契约。
- 明确且持续有效的能力限制。
- 下一阶段已确认的范围与进入条件。

以下内容默认不保留在工作树：

- 普通已修复缺陷的发现过程。
- 被最终门禁取代的中间测试基线。
- 未采用方案和迂回实现。
- 临时任务编号、个人工作日志和逐轮 review 记录。
- 与 Git 历史、交接仓库或自动化测试重复的证据。

确有长期约束价值的修复应改写为正向规则，而不是保留故障叙事。

### 1.2 单一事实来源

| 信息 | 事实来源 |
| ---- | -------- |
| 当前架构与业务规则 | `Docs/Architecture/` |
| Phase 范围、顺序与门禁 | `Docs/Development_Plans/` |
| 构建、配置、迁移与测试操作 | `Docs/Guides/`、`config/README.md` |
| 文档和开发治理 | `Docs/Standards/` |
| 已完成阶段的结果 | `Docs/Archive/` |
| 完整历史与提交证据 | Git history、独立 `.codex` 交接仓库 |

同一事实不得在多个文档中复制完整说明；其他文档只给简短结论和链接。

---

## 2. 排版规范

### 2.1 中英文与标点

- 中文与英文、数字、代码之间保留一个空格。
- 中文叙述使用中文全角标点。
- 代码、路径、命令、标识符和格式字面量使用反引号。
- C++23、PostgreSQL、Drogon、Clean Architecture、Domain Service、Application、Infrastructure、DTO、API、JWT、Unit of Work 等术语保持统一拼写。

### 2.2 标题

- 每份文档只有一个一级标题。
- 二级标题用于主要模块并使用数字编号。
- 三级标题用于二级标题下的主题并使用级联编号。
- 不跨级使用标题。

### 2.3 代码块

- 所有代码块指定语言，例如 `cpp`、`sql`、`json`、`bash`、`powershell`、`text`、`mermaid`。
- 示例必须符合当前接口和命名，不保留已停用 API。
- 架构文档优先使用接口片段、契约或伪代码，不复制大段实现。
- Secret、Token、完整外部 URL query、响应正文和真实凭据不得出现在示例中。

---

## 3. 目录与命名

### 3.1 Architecture

使用两位数字 + PascalCase 单词：

```text
01_Technical_Architecture.md
08_Exchange_Rate_System_Design.md
```

### 3.2 Development Plans

```text
Development_Plans/
├── Overall_Development_Plan.md
└── Phase_N/Phase_N_Development_Plan.md
```

计划只包含目标、范围、顺序、门禁和能力边界。Phase 完成后将状态改为 Complete，保留最终路线，不另建详细计划重复同一内容。

### 3.3 Guides 与 Standards

英文文件名使用 PascalCase 单词并以下划线连接：

```text
Linux_Development_Workflow.md
Documents_Format_Standard.md
```

指南只保留当前可执行流程。历史环境和旧命令应删除或合并到交付摘要。

### 3.4 Archive

完成 Phase 默认使用：

```text
Phase_N_Development_Record.md
Phase_N_Sxx-Syy_Delivery_Summary.md
```

步骤摘要按自然模块合并，体量应大致接近。单独的 bug 报告、临时验证报告、完成任务清单和文档优化轮次应合并为少量结果文档。

### 3.5 Development 临时目录

`Docs/Development/` 只用于确有必要的进行中文档。进行中设计可使用 `_Plan` 后缀；完成后必须把结果合并到 Architecture、Development Plans、Guides 或 Archive，并删除临时文件。

---

## 4. 文档类型要求

### 4.1 架构文档

应包含：目标、职责边界、当前数据/接口契约、错误和测试规则。可选扩展仅在已确认会影响当前设计时保留。

不得包含：已经停用的实现类、大段历史反例、普通缺陷过程和与当前代码冲突的示例。

### 4.2 开发计划

应包含：阶段目标、明确范围、推荐顺序、每一步验收和完成定义。

完成计划使用结果时态，不保留“待执行”“随后复测”或已关闭 blocker。

### 4.3 交付摘要

应包含：交付范围、最终能力、持续规则、最终验证矩阵和能力边界。

测试只记录有决策价值的环境、数量和结论，不复制原始日志。

### 4.4 操作指南

应包含：前置依赖、可直接执行的命令、成功判断和安全注意事项。过时命令必须删除。

---

## 5. 项目架构统一规则

### 5.1 事务与事件

- Repository 与 Outbox 使用同一个 Unit of Work 事务上下文。
- 领域事件不在业务提交前直接派发。
- Outbox claim、重试、dead letter 与 Handler receipt 以 PostgreSQL 为事实来源。

### 5.2 金额与汇率

- 金额和汇率不经 `float`、`double` 或 JSON number。
- 汇率存储为 USD 枢纽方向。
- 默认舍入为 Half-Even。
- 缺失汇率返回不可用或明确历史降级，不使用默认 `0`、`1`。

### 5.3 分层与多租户

- Domain Service 无 Repository、事务、事件和 I/O 副作用。
- Application 负责事务、权限和用例编排。
- 所有租户事实受 `user_id` 或等价强约束保护。
- request/background 数据库角色分离，后台角色不得进入请求路径。

### 5.4 调度

- Drogon Event Loop 只触发轻量回调。
- 网络、数据库和长任务进入专用执行器。
- 当前一致性优先使用 PostgreSQL，不引入不必要的消息中间件。

---

## 6. 变更检查

文档变更至少执行：

1. `git diff --check`。
2. Markdown 相对链接检查。
3. `Docs/README.md` 与 `Directory_Guidance.md` 目录一致性检查。
4. 旧路径、旧状态和秘密模式扫描。
5. 与代码、OpenAPI、migration 和配置指南的一致性 review。

目录或命名调整必须在同一提交中修复全部引用。
