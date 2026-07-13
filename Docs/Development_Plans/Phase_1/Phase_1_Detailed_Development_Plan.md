# Personal Finance Hub (PFH) - Phase 1 Detailed Development Plan

Version: 1.1
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Active

---

## 1. 导言与执行目标

本文档是 `Phase_1_Development_Plan.md` 的细化子计划，用于描述 Phase 1 从创建工程目录结构开始，到第一阶段测试收尾为止的具体开发顺序、交付物和验收口径。

当前进度（2026-07-13）：P1-S01 至 P1-S09 已完成，P1-S10-01 REST 契约与遗留项收口也已交付。Windows GCC 16 基线为 254 个 unit/use-case、16 个 In-Memory integration 与 1 个 migration gate，共 271/271。V3 已在外部 PostgreSQL 16.14 空库复测通过；真实 Drogon/PostgreSQL 适配器、API、后台任务及完整 P1-S12 门禁尚未完成。

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
- 明确 `User` / `UserPreference` 持久化边界：
  - `User` 由 `IUserRepository` 负责聚合读取与保存。
  - `UserPreference` 保留独立 Repository 接口，供偏好单独读取与更新。
  - 如读取用户聚合时需要偏好，可由应用层通过 `IUserRepository` + `IUserPreferenceRepository` 组合装配，不在 Domain Service 内处理。
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
  - refresh_tokens。
  - revoked_access_tokens。
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

已交付基线：

- 定义 Repository 与 `IUnitOfWork` 接口，并用 In-Memory 实现固定事务提交、回滚、read-your-writes 和用户隔离语义。
- 通过同一批 integration scenarios 验证账户、流水、分类、汇率、余额缓存、转账聚合和 outbox 行为。

生产适配器内容（移至 P1-S10-03 完成）：

- 实现 `DrogonUnitOfWork`，Repository 写入必须使用同一事务上下文。
- 实现 PostgreSQL `UserRepository` 与 `UserPreferenceRepository`：
  - `UserRepository` 负责 `User` 聚合持久化。
  - `UserPreferenceRepository` 负责偏好数据的独立读取与更新。
- 实现 PostgreSQL `AccountRepository`、`TransactionRepository` 与 `CategoryRepository`。
- 实现 `TransferAggregate` 持久化写路径：
  - 提供 `saveTransfer(const TransferAggregate&)` 或等价边界。
  - 按聚合约束同时落 `transfer_groups` 与底层派生流水。
  - 不允许把转账聚合拆成应用层手工拼接的多次普通流水写入。
- 实现 `ExchangeRateRepository`。
- 实现余额缓存更新路径。
- 实现 outbox 写入路径。

验收标准：

- S08 的 In-Memory 语义基线通过；这不等同于真实数据库验收。
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

执行前置：

- P1-S01 至 P1-S09 的 Windows 本地门禁持续通过。
- 生产 composition root 不得装配 In-Memory Repository。
- `Docs/Architecture/10_REST_API_Design.md` 与当前 Application Command/DTO 保持一致。

开发顺序：

#### P1-S10-01 REST 契约与遗留项收口

状态：**已完成（2026-07-13）**。

- 将流水请求统一为 `accountId/type/amount/currencyCode/categoryId/description/occurredAt`，金额仅接受十进制字符串。
- 将转账请求统一为 `sourceAccountId/targetAccountId/mode/outgoingAmount/incomingAmount/rate/feeAmount/feeSource/feeAccountId/description/occurredAt`；三种 mode 只接受各自输入字段，派生字段必须为空。
- 完成任务 #48：手续费按选中账户币种接收正数 magnitude，聚合内落为负数 signed `Adjustment`；Source/Target/ThirdParty 三种来源、三账户有序锁定、原子保存/回滚与级联删除均有测试。
- 固定金额符号边界：Income/Expense 请求使用正数 magnitude，Adjustment 使用 signed amount；Presentation 对外返回业务 magnitude，数据库带符号金额不得直接泄漏。
- 明确 Phase 1 不开放转账删除接口；在 `DeleteTransferUseCase` 完成前，路由表中不得注册 `DELETE /api/v1/transfers/{id}`。
- 明确流水采用追加 + 软删除模型，Phase 1 不对流水普通更新增加行级 `version`；账户聚合并发继续使用 `Account.version`。

产物：更新后的 REST/Application 契约、手续费领域与持久化路径、任务状态及 `Phase_1_S10_Delivery_Summary.md`。HTTP parser/OpenAPI 代码随 P1-S10-05 实现。

验证：Windows GCC 16 Debug 构建通过，271/271 CTest 通过。真实 PostgreSQL adapter 验证仍归 P1-S10-03/S10-04 与 P1-S12。

#### P1-S10-02 Drogon、PostgreSQL 与 CMake 目标接入

- 启用 Drogon/PostgreSQL 依赖发现，落地 `pfh_application`、`pfh_infrastructure`、`pfh_presentation` 和可执行程序目标。
- 保持依赖方向：Domain 不链接 Drogon/PostgreSQL，Application 只依赖 Domain 端口。
- 将已增长的 header-only 用例按需迁移到 `.cpp`，避免 Controller 编译单元重复实例化。
- 增加 `tests/api` 目标和服务测试启动入口。

产物：可配置、可编译并能启动空 HTTP 服务的分层 CMake 目标。

#### P1-S10-03 PostgreSQL Repository 与 DrogonUnitOfWork

- 实现 `DrogonUnitOfWork`，把同一 `Transaction` 上下文显式传给所有写 Repository 和 outbox。
- 实现 PostgreSQL 版 User、UserPreference、Account、Transaction（含 Transfer 聚合持久化）、ExchangeRate、Category、Tag、AuditLog、RefreshToken 和 RevokedToken 适配器。
- 完成任务 #46/#47/#51：真实分类解析、事务内 read-your-writes、余额缓存 `source_version` 与 schema `version` 语义严格一致。
- 以迁移 schema 为事实来源，所有查询和约束显式包含 `user_id`；不得照搬 In-Memory 的简化字段或计数规则。

产物：可供 composition root 装配的生产持久化适配器及可复用的 PostgreSQL integration scenarios。

#### P1-S10-04 Composition Root、DbClient 与 RLS 上下文

- 在 bootstrap 层创建 DbClient、Repository、UnitOfWork、Use Case、QueryService、Controller 和 Filter 的唯一装配入口。
- 鉴权后在每请求/每事务执行 `SET LOCAL app.current_user_id` 或等价安全方案；连接归还连接池前不得残留用户上下文。
- 未设置、非法或跨用户上下文必须 fail closed，并由测试证明连接池复用不会串租户。
- 启动时校验数据库配置、JWT 密钥和必要依赖，失败时给出不含敏感信息的明确日志。

产物：仅装配真实生产适配器的可启动服务。

#### P1-S10-05 HTTP DTO、解析与统一响应

- 实现 JSON 到 Application Command 的防御性映射，ID、枚举、可选时间和字符串金额逐字段校验。
- 实现 Application DTO 到响应 JSON 的映射，统一金额、RFC 3339 时间、空值和分页口径。
- 建立统一成功响应、错误响应和 TraceId；将应用错误单向映射为 400/401/403/404/409/422/500。
- 注册全局异常处理器，生产响应不包含堆栈、SQL、路径、密钥或底层异常文本。

产物：可复用的 request parser、response mapper、error mapper 和 exception boundary。

#### P1-S10-06 注册、登录与 Token 生命周期

- 实现 register、login、refresh、logout 四条认证路径。
- 实现密码哈希、JWT 签发与校验、`iss/aud/sub/sid/jti/iat/nbf/exp` 校验和 `JwtFilter` 请求上下文注入。
- Refresh Token 只持久化哈希，刷新时执行 rotation；复用已撤销 token 时撤销整个 token family/session。
- logout 同事务撤销 refresh token、记录 access token `jti` 并写审计/outbox 事实。

产物：认证 Controller、Filter、Token Service 和持久化适配器。

#### P1-S10-07 基础资源 API

- 在 Controller 前补齐 CreateAccount、ArchiveAccount、Category、Tag 和 UserPreference 所需的 Application Command/Use Case；Presentation 不直接编排 Repository。
- 按 Account、Category、Tag、UserPreference、Currency metadata 的顺序接入 API。
- 所有资源读取、更新和删除均验证当前 `user_id`；系统模板与用户自定义分类边界保持清晰。
- Account API 至少覆盖列表、创建、余额快照、归档和已设计的危险删除确认路径。

产物：基础资源 Controller 与成功/越权/不存在/冲突测试。

#### P1-S10-08 Transaction API

- 接入创建和软删除流水；禁止直接创建 Transfer 派生流水。
- Income/Expense 只接受正数 magnitude，Adjustment 按 signed 语义映射；分类由 `ICategoryRepository` 校验真实 board。
- 拒绝 JSON number、超出 `NUMERIC(20,8)`、币种不匹配、跨用户账户与分类。

产物：TransactionController 及金额、分类、权限、时间默认值测试。

#### P1-S10-09 Transfer API

- 复用已完成的任务 #48，接入三种转账模式、手续费来源和 signed Adjustment 响应映射。
- 响应中的 outgoing/incoming/fee 均使用正数 magnitude；存储层 outgoing 负号不直接返回。
- 验证同用户账户、同账户拒绝、同币种约束、汇率精度、`NUMERIC(20,8/10)` 边界和事务原子性。
- Phase 1 仅支持创建与查询，不注册转账删除路由。

产物：TransferController 及三种模式、手续费、回滚和 422 测试。

#### P1-S10-10 Report API

- 暴露 net worth、cash flow 和 dashboard summary。
- 复用 `ReportQueryService` 的用户时区月窗、历史汇率、signed Adjustment、Transfer 排除和一级分类聚合语义。
- 所有查询显式绑定 `user_id`；缺失汇率和无效时区不得静默回退。

产物：ReportController 及月边界、跨币种、分类聚合和用户隔离测试。

#### P1-S10-11 API 回归与交付总结

- 启动测试版 Drogon App，覆盖认证、主要成功路径、错误映射、TraceId 和异常脱敏。
- 运行 Windows Debug 构建、当前 271 个既有测试和新增 API 测试。
- 回写 `Docs/Development/Tasks.md`，更新 `Phase_1_S10_Delivery_Summary.md`，记录尚待外部机器验证的项目。

产物：可重复执行的 API 测试集与 S10 交付总结。

验收标准：

- API 路由统一以 `/api/v1` 开头。
- 金额字段在请求和响应中均为字符串。
- 生产 composition root 只使用 PostgreSQL Repository 与 `DrogonUnitOfWork`。
- register/login/refresh/logout 与 token rotation/revocation 均有测试。
- Account、Transaction、Transfer 和 Report 的最小路径均有成功与错误场景。
- 未认证、无权限、找不到资源、冲突、业务规则错误和系统错误能返回正确 HTTP 状态码。
- 生产响应不泄露堆栈、SQL、文件路径或密钥。
- S10 本机门禁通过；真实 PostgreSQL 与 Linux/Docker 的最终签署保留到 P1-S12。

### 3.11 P1-S11 Outbox、调度与后台任务基础

目标：

- 完成事务后事件投递和汇率刷新任务的最小基础。

开发顺序：

#### P1-S11-01 Outbox 领取与状态机

- 使用 `FOR UPDATE SKIP LOCKED` 或等价机制批量 claim pending 事件，定义 processing、published、failed/dead-letter 状态转换。
- claim、锁超时恢复、重试次数、下一次重试时间和最后错误摘要必须可观测。

#### P1-S11-02 发布、重试与死信

- 实现 `OutboxPublisherJob`，业务事务提交后才允许发布。
- 使用有上限的指数退避；达到阈值后进入 dead letter，不得无限热重试。
- 发布失败不得回滚已经提交的业务事实。

#### P1-S11-03 Handler 幂等与审计闭环

- 每个事件处理器以 event id 建立幂等边界，重复投递不得重复写坏缓存、审计或通知事实。
- 接入 AuditLog 处理器，敏感 payload 只记录必要摘要。

#### P1-S11-04 汇率 HTTP Provider

- 实现真实 HTTP Provider，严格校验响应集合、币种、时间戳、正汇率和重复项。
- 网络失败、部分响应和非法响应进入既定历史降级路径，并发出 `ExchangeRateRefreshFailedEvent`。

#### P1-S11-05 Scheduler 与 JobManager

- 实现汇率刷新、outbox 发布、过期 Refresh Token 和 Access Token 撤销记录清理任务。
- 定义启动、停止、优雅退出、单实例防重入和任务超时行为。

#### P1-S11-06 非阻塞约束

- 网络、数据库等待和 CPU 密集工作不得阻塞 Drogon Event Loop；使用异步 client、协程或专用 worker。
- 增加慢任务日志、job id、trace id、执行耗时和失败原因。

#### P1-S11-07 后台任务测试与交付总结

- 覆盖并发 claim、崩溃恢复、重复投递、退避、dead letter、历史汇率降级、任务防重入和优雅停止。
- 回写 Tasks 并新增 `Phase_1_S11_Delivery_Summary.md`。

验收标准：

- outbox 事件不会在业务事务提交前派发。
- 任务失败可以重试并进入 failed 或 dead letter。
- 多 worker 不会同时成功处理同一事件，重复投递不会产生重复副作用。
- 汇率刷新失败可降级并记录告警事件。
- Drogon Event Loop 不执行阻塞式网络、数据库或长时间 CPU 工作。
- 过期 token 与撤销记录具备可重复执行的清理任务。

### 3.12 P1-S12 Phase 1 测试收尾与文档回写

目标：

- 用测试和文档收束 Phase 1。
- 确保进入 Phase 2 前，后端核心闭环稳定。

执行位置：

- Windows 本地门禁在当前开发机执行。
- Linux、Docker、PostgreSQL 16+ 和真实 Drogon 运行时验证在另一台具备对应环境的机器执行；该项必须保留在 Tasks 中，取得可追溯结果前不得视为 Phase 1 已签署通过。

开发顺序：

#### P1-S12-01 Windows 本地回归

- 执行 Debug configure/build、全部 unit/In-Memory integration/API tests、Markdown 检查和 `git diff --check`。
- 复核 Release configure/build，确认无仅在 Debug 可编译的路径。

#### P1-S12-02 外部机器环境准备

- 固定 Linux 发行版、GCC/libstdc++、CMake、Drogon、PostgreSQL、Flyway、Docker 与 `tzdata` 版本。
- 使用独立测试凭据和数据库；记录 commit hash、镜像/工具版本、执行命令与时间。

#### P1-S12-03 空库迁移与真实持久化

- 从空 PostgreSQL 16+ 数据库执行全部迁移，并验证重复启动不会破坏 schema。
- 用与 In-Memory 基线相同的 scenarios 复跑全部 PostgreSQL Repository 与 `DrogonUnitOfWork` 测试。
- 重点验证 RLS fail-closed、连接池用户上下文复用、事务回滚、并发锁、乐观锁、outbox 同事务、余额缓存 `source_version` 和历史汇率。
- 实测 `NUMERIC(20,8/10)` 的边界、舍入和超界拒绝，关闭 Tasks 中 item 10/14/16 的连库复核项。

#### P1-S12-04 API 与认证 Smoke Test

- 在真实 Drogon + PostgreSQL 上覆盖 register/login/refresh/logout、JWT 拒绝、资源用户隔离和主要业务写读路径。
- 验证 JSON number 拒绝、错误状态码、TraceId、异常脱敏、转账原子性和报表时区月边界。

#### P1-S12-05 Outbox 与 Scheduler 真实运行验证

- 验证事件只在 commit 后被 claim，失败重试、dead letter、幂等 handler 和进程重启恢复均符合设计。
- 验证真实汇率 HTTP Provider、历史降级、周期调度、token 清理和优雅停止。

#### P1-S12-06 Linux Debug/Release 与 Docker 门禁

- 在目标 Linux 工具链分别完成 Debug/Release configure、build 和全量测试。
- 构建并启动 Docker 运行环境，执行健康检查和关键 API smoke test；确认 `tzdata`、迁移和运行时配置齐全。

#### P1-S12-07 文档定稿与分支交付

- 回写 `Docs/Development/Tasks.md`、测试基线、环境记录和 `Phase_1_S12_Delivery_Summary.md`。
- 已验收的阶段交付总结按文档规范归档；未完成项继续留在 Tasks，不以说明文字代替验收。
- 审核架构、代码和 API 契约；存在偏差时先明确设计结论，再修代码或文档。
- 所有阻断项通过后，才允许将完整 Phase 1 分支合并到 `main`。

验收标准：

- `cmake configure` 通过。
- `cmake build` 通过。
- unit tests 通过。
- repository integration tests 通过。
- api smoke tests 通过。
- 真实 PostgreSQL 空库迁移、Repository/UoW/RLS/并发与数值边界测试通过。
- Linux Debug 构建通过。
- Linux Release 构建通过。
- Linux 对应测试集通过。
- Docker 服务启动、健康检查与关键 API smoke test 通过。
- `git diff --check` 通过。
- Phase 1 交付总结文档已回写完成。
- 当前 Phase 分支满足合并回 `main` 的交付门槛。
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

### 5.1 已确认事项

- **Decimal 底层实现**：已采用编译器原生 `__int128` 定点实现；CMake 必须在不支持的平台明确失败。
- **数据库测试方式**：开发机保留快速 In-Memory 回归；真实 PostgreSQL、Linux 和 Docker 阻断门禁在另一台机器于 P1-S12 执行。
- **转账删除**：Phase 1 暂不开放；完成 `DeleteTransferUseCase` 和聚合级联测试后再新增路由。
- **API smoke test 范围**：Phase 1 覆盖认证与核心记账闭环，完整前端端到端场景留待 Phase 2。
