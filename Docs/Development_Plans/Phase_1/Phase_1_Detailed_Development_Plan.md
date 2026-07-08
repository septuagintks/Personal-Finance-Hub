# Personal Finance Hub (PFH) - Phase 1 Detailed Development Plan

Version: 1.0
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Draft

---

## 1. 导言与执行目标

本文档是 `Phase_1_Development_Plan.md` 的细化子计划，用于描述 Phase 1 从创建工程目录结构开始，到第一阶段测试收尾为止的具体开发顺序、交付物和验收口径。

### 1.1 执行原则

- **先骨架后业务**：先建立 CMake、目录边界、测试入口和配置约定，再进入领域实现。
- **先 Domain 后外层**：先完成金融原语和领域规则，再接 Repository、Use Case 和 Controller。
- **先测试保护高风险规则**：`Decimal`、`Money`、`ExchangeRate`、Transfer 和 Unit of Work 必须优先拥有测试。
- **每一步都可运行**：每个小阶段结束时，至少保证构建或对应测试命令可执行。

### 1.2 阶段完成定义

Phase 1 细化计划完成时，应达到以下状态：

- 后端工程骨架可构建。
- Domain 层核心模型和服务有单元测试。
- PostgreSQL、Flyway、Repository 和 Unit of Work 有集成测试。
- 账户、流水、转账、报表和认证基础 API 有 smoke test。
- `git diff --check`、构建、测试和文档检查均通过。

---

## 2. 开发顺序总览

### 2.1 推荐顺序

```text
P1-S01 目录结构与工程骨架
P1-S02 CMake、依赖与编译选项
P1-S03 测试入口与质量命令
P1-S04 基础类型、错误模型与配置日志
P1-S05 金融原语
P1-S06 领域模型与领域服务
P1-S07 数据库迁移与持久化基础
P1-S08 Repository 与 Unit of Work
P1-S09 Application Use Case
P1-S10 REST API 与认证基础
P1-S11 Outbox、调度与后台任务基础
P1-S12 Phase 1 测试收尾与文档回写
```

### 2.2 依赖关系

- P1-S05 依赖 P1-S01 到 P1-S04。
- P1-S06 依赖 P1-S05。
- P1-S08 依赖 P1-S07。
- P1-S09 依赖 P1-S06 和 P1-S08。
- P1-S10 依赖 P1-S09。
- P1-S12 必须覆盖 P1-S05 到 P1-S11 的关键路径。

---

## 3. 细化开发步骤

### 3.1 P1-S01 目录结构与工程骨架

目标：

- 建立项目后端代码目录。
- 固定 Clean Architecture 的物理边界。
- 让后续代码可以按层放置，而不是边写边搬。

建议目录：

```text
.
├── CMakeLists.txt
├── cmake/
├── config/
├── include/
│   └── pfh/
│       ├── domain/
│       ├── application/
│       ├── infrastructure/
│       └── presentation/
├── src/
│   ├── domain/
│   ├── application/
│   ├── infrastructure/
│   ├── presentation/
│   └── bootstrap/
│       └── main.cpp
├── tests/
│   ├── unit/
│   ├── integration/
│   ├── api/
│   └── support/
└── migrations/
```

开发内容：

- 创建上述目录。
- 约定头文件位于 `include/pfh/`，实现位于 `src/`。
- 建立最小 `main.cpp`，只负责应用启动入口占位。
- 建立 `tests/support/`，用于测试夹具、测试数据和公共断言。

验收标准：

- 目录结构与本文档一致，或差异有明确理由。
- Domain 目录不包含 Drogon、PostgreSQL 或 JSON 相关依赖。
- 空工程可以进入 CMake 配置阶段。

### 3.2 P1-S02 CMake、依赖与编译选项

目标：

- 建立可持续扩展的 CMake 构建。
- 明确 C++23、警告级别和目标拆分。

开发内容：

- 根 `CMakeLists.txt` 设置 C++23。
- 建立可拆分的架构层目录与编译目标结构（`pfh_domain`、`pfh_application`、`pfh_infrastructure`、`pfh_presentation`），当前阶段优先落地已具备稳定实现内容的目标（如 `pfh_domain`），其余目标按实现进度逐步启用，避免空壳目标噪音。
- 测试目标单独放在 `tests/`。
- 引入或预留 Drogon、PostgreSQL client、spdlog、GoogleTest 和必要的 Decimal 支持库。
- 设置 Debug/Release 构建类型。

验收标准：

- 可以完成 CMake configure。
- Domain 目标可以独立编译，不链接 Drogon。
- 测试目标可以被 CTest 或统一命令发现。

### 3.3 P1-S03 测试入口与质量命令

目标：

- 在写金融原语前先建立测试入口。
- 让每个高风险规则都能被快速回归。

开发内容：

- 接入 GoogleTest。
- 创建第一个 smoke unit test。
- 创建测试命名规范，例如 `ClassName_WhenCondition_ExpectedBehavior`。
- 创建测试数据目录。
- 建立本地质量命令，至少覆盖构建、单元测试和文档空白检查。

验收标准：

- 空测试或示例测试可以通过统一命令运行。
- 测试失败时能定位到具体测试名。
- `git diff --check` 可作为文档和代码提交前检查。

### 3.4 P1-S04 基础类型、错误模型与配置日志

目标：

- 在业务代码前建立共享基础设施，避免每层各自定义错误和配置格式。

开发内容：

- 定义强类型 ID，例如 `UserId`、`AccountId`、`TransactionId`。
- 定义 `Result` / `std::expected` 风格错误返回约定。
- 定义应用层错误类型，覆盖 Validation、Unauthorized、Forbidden、NotFound、Conflict、DomainRuleViolation、InfrastructureFailure。
- 建立配置加载接口，支持数据库连接、JWT 密钥、日志级别和运行环境。
- 接入 spdlog，约定 TraceId、用户 ID、任务 ID 和错误上下文字段。

验收标准：

- Domain 层错误不依赖 HTTP 状态码。
- Presentation 层可以单向映射应用层错误到 HTTP 响应。
- 配置和日志模块不污染金融原语。

### 3.5 P1-S05 金融原语

目标：

- 实现金融系统最核心的不变量。
- 先通过单元测试固定金额、币种、汇率和舍入规则。

开发内容：

- 实现 `Decimal`：
  - 字符串解析。
  - 规范化。
  - 比较、加减乘除。
  - Half-Even 舍入。
  - 溢出保护。
- 实现 `Currency`：
  - 受控的 ISO-4217 fiat 子集 + 加密货币白名单校验（当前不支持全 ISO-4217 库，按需扩展）。
  - 不可变值对象。
  - 精确比较。
- 实现 `Money`：
  - 金额和币种绑定。
  - 同币种加减。
  - 禁止跨币种直接加减。
  - JSON 边界只接受字符串金额。
- 实现 `ExchangeRate`：
  - 明确 base / target 方向。
  - 支持反向汇率。
  - 保留时间戳与来源。
- 实现 `CurrencyConversionService`：
  - 直接汇率。
  - 反向汇率。
  - USD 枢纽三角折算。
  - 缺失汇率明确返回错误。

验收标准：

- 不使用 `float` 或 `double` 表示金额和汇率。
- `Money` 跨币种直接加减测试必须失败。
- 缺失汇率不得用 `0`、`1` 或其他默认值参与计算。
- 金融原语单元测试覆盖正常路径、边界路径和错误路径。

### 3.6 P1-S06 领域模型与领域服务

目标：

- 建立账户、流水、转账和余额规则的纯领域闭环。

开发内容：

- 实现 `User` 与 `UserPreference`。
- 实现 `Account`：
  - 账户类型。
  - 币种。
  - 归档状态。
  - 负余额规则。
  - 版本字段。
- 实现 `Transaction`：
  - Income。
  - Expense。
  - Adjustment。
  - Transfer 派生流水。
- 实现 `TransferAggregate`：
  - Outgoing + Rate => Incoming。
  - Outgoing + Incoming => Rate。
  - Incoming + Rate => Outgoing。
  - 手续费来源：源账户、目标账户、第三方账户。
  - 汇兑损益记录。
- 实现 `TransferDomainService`。
- 实现 `BalanceCalculationService`。
- 实现分类 board 校验规则。

验收标准：

- Domain Service 不访问 Repository、不打开事务、不发布事件。
- Transfer 不计入收入/支出统计。
- 分类 board 规则能阻止收入、支出和调整类型误用。
- 领域模型测试覆盖转账、手续费、余额重建和分类校验。

### 3.7 P1-S07 数据库迁移与持久化基础

目标：

- 建立 PostgreSQL 16+ 数据库结构和 Flyway 迁移基础。

开发内容：

- 配置数据库连接。
- 创建 Flyway 目录和初始迁移。
- 初始迁移覆盖：
  - users。
  - user_preferences。
  - accounts。
  - categories。
  - transactions。
  - transfer groups 或等价转账关联结构。
  - exchange_rates。
  - account_balance_cache。
  - domain_events_outbox。
  - audit logs。
- 建立必要索引、唯一约束和外键。

验收标准：

- 所有跨用户数据表必须包含 `user_id` 或能通过关联表强约束用户边界。
- 汇率记录 append-only。
- 金额字段使用 `NUMERIC`，应用层仍以字符串进入 `Decimal`。
- 迁移可在空库上完整执行。

### 3.8 P1-S08 Repository 与 Unit of Work

目标：

- 建立持久化读写路径和事务一致性边界。

开发内容：

- 实现 `DrogonUnitOfWork`。
- Repository 写入必须使用同一事务上下文。
- 实现 `UserRepository` 与 `UserPreferenceRepository`。
- 实现 `AccountRepository` 与 `TransactionRepository`。
- 实现 `ExchangeRateRepository`。
- 实现余额缓存更新路径。
- 实现 outbox 写入路径。

验收标准：

- 业务写入和 outbox 写入在同一事务提交。
- 事务回滚不会留下业务数据或 outbox 脏记录。
- Repository 集成测试覆盖用户隔离、乐观锁、余额缓存和历史汇率查询。
- 不出现创建事务对象但 Repository 使用普通 DbClient 写库的情况。

### 3.9 P1-S09 Application Use Case

目标：

- 将领域规则、Repository 和事务编排成可供 API 调用的应用用例。

开发内容：

- 实现 `CreateTransactionUseCase`。
- 实现 `DeleteTransactionUseCase`。
- 实现 `CreateTransferUseCase`。
- 实现 `RefreshExchangeRatesUseCase`。
- 实现账户查询与余额查询用例。
- 实现报表 QueryService：
  - net worth。
  - cash flow。
  - dashboard summary。

验收标准：

- Use Case 负责权限校验、事务边界和 Repository 编排。
- Domain Rule Violation 稳定映射到应用层错误。
- Infrastructure Failure 不泄露数据库细节。
- 报表 QueryService 不绕过金额和汇率规则。

### 3.10 P1-S10 REST API 与认证基础

目标：

- 暴露 Phase 1 最小可用 API。
- 接入 JWT、Refresh Token 和统一错误响应。

开发内容：

- 实现认证基础：
  - login。
  - refresh。
  - logout。
  - JWT Filter。
  - Refresh Token 轮换与撤销。
- 实现 Account API。
- 实现 Transaction API。
- 实现 Transfer API。
- 实现 Report API。
- 实现统一错误响应格式。
- 注册 Drogon 全局异常处理器。

验收标准：

- API 路由统一以 `/api/v1` 开头。
- 金额字段在请求和响应中均为字符串。
- 未认证、无权限、找不到资源、冲突、业务规则错误和系统错误能返回正确 HTTP 状态码。
- 生产响应不泄露堆栈、SQL、文件路径或密钥。

### 3.11 P1-S11 Outbox、调度与后台任务基础

目标：

- 完成事务后事件投递和汇率刷新任务的最小基础。

开发内容：

- 实现 `OutboxPublisherJob`。
- 支持 pending、failed、重试次数和 dead letter。
- 实现汇率刷新调度入口。
- 确保网络 I/O、数据库 I/O 或 CPU 密集任务不阻塞 Drogon Event Loop。
- 为任务执行补充日志和审计事件。

验收标准：

- outbox 事件不会在业务事务提交前派发。
- 任务失败可以重试并进入 failed 或 dead letter。
- 汇率刷新失败可降级并记录告警事件。

### 3.12 P1-S12 Phase 1 测试收尾与文档回写

目标：

- 用测试和文档收束 Phase 1。
- 确保进入 Phase 2 前，后端核心闭环稳定。

开发内容：

- 跑通金融原语单元测试。
- 跑通领域服务单元测试。
- 跑通 Repository 集成测试。
- 跑通 API smoke test。
- 检查报表是否排除 Transfer。
- 检查金额字段是否拒绝 JSON number。
- 检查危险删除、事务回滚、outbox 和历史汇率关键路径。
- 回写 `Docs/Development/Tasks.md`。
- 如实现与设计不一致，先更新架构文档，再调整任务状态。

验收标准：

- `cmake configure` 通过。
- `cmake build` 通过。
- unit tests 通过。
- repository integration tests 通过。
- api smoke tests 通过。
- `git diff --check` 通过。
- Phase 1 未完成项、风险和延期内容已记录。

---

## 4. 测试收尾清单

### 4.1 必测领域规则

- `Decimal` 字符串解析、舍入和溢出。
- `Money` 同币种运算与跨币种拒绝。
- `ExchangeRate` 方向、反向和三角折算。
- `TransferAggregate` 三种构造模式。
- 手续费来源和汇兑损益。
- Transfer 排除收入/支出统计。
- 分类 board 校验。

### 4.2 必测持久化规则

- 用户隔离。
- Repository 乐观锁。
- 余额缓存更新和重建。
- 汇率 append-only。
- 历史汇率按时间点查询。
- Unit of Work 同事务提交和回滚。
- outbox 写入与业务事实同事务落库。

### 4.3 必测 API 规则

- JSON 非法返回 400。
- 未登录返回 401。
- 访问其他用户资源返回 403。
- 资源不存在返回 404。
- 重复或版本冲突返回 409。
- 金融业务规则错误返回 422。
- 数据库故障返回 500。
- 金额输入拒绝 JSON number。

---

## 5. 待决策选项

当前没有必须由维护者立即选择的架构项。

### 5.1 后续可评估事项

- **Decimal 底层实现**：实现阶段在 `boost::multiprecision::int128_t` 和平台原生 `__int128_t` 之间做一次最终确认。
- **数据库测试方式**：可先使用本地 PostgreSQL 测试库；如环境漂移明显，再引入 Testcontainer。
- **API smoke test 范围**：Phase 1 可先覆盖核心路径，Phase 2 再扩展为完整端到端测试。
