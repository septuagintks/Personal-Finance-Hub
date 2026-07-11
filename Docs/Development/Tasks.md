# Personal Finance Hub (PFH) - Phase 1 待办任务跟踪

Version: 1.3
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Active

---

## 1. 使用规则

### 1.1 任务状态

- `[ ]` 表示未开始或未完成。
- `[x]` 表示已经完成，并且相关文档、代码或验证结果已经落地。
- 任务 ID 按本文档当前顺序连续编号；调整任务顺序后应同步刷新 ID。

### 1.2 更新规则

- 本文件只跟踪 Phase 1 范围内的开发任务。
- 新增任务时放入最合适的 Phase 1 分组，并按顺序刷新任务 ID。
- 已完成任务不删除；如任务被替代，应新增替代任务，并在原任务后注明替代关系。
- 代码实现、架构文档和 Phase 1 计划出现冲突时，优先更新设计文档，再更新任务状态。
- 每个任务项应尽量写出可验证产物，而不是只写模块名。

---

## 2. 当前执行优先级

### 2.1 近期顺序

1. 按 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md` 创建目录结构与工程骨架。
2. 搭建 CMake、编译选项、GoogleTest 入口和本地质量命令。
3. 实现 `Decimal`、`Currency`、`Money` 和 `ExchangeRate`，优先锁定金额与汇率的不可变规则。
4. 实现最小领域闭环：账户、流水、转账、汇率折算和余额规则。
5. 接入 PostgreSQL、Flyway、Repository 与 Unit of Work，形成可持久化写路径。

### 2.2 当前前置条件

- Phase 1 开发计划已创建：`Docs/Development_Plans/Phase_1_Development_Plan.md`。
- Phase 1 细化子计划已创建：`Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md`。
- 进入代码实现前，需要先完成工程骨架和测试入口，否则金融原语缺少回归验证。

---

## 3. 待办任务列表

### 3.1 迭代与计划 (Planning)

- [x] 编写 Phase 1 开发计划文档 `Docs/Development_Plans/Phase_1_Development_Plan.md` <!-- id: 1 -->
- [x] 编写 Phase 1 细化子计划文档 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md` <!-- id: 2 -->
- [ ] 根据 Phase 1 细化子计划评审结果，必要时回写 `Docs/Development_Plans/Phase_1_Development_Plan.md` <!-- id: 3 -->
- [ ] 在 Phase 1 每个里程碑完成后回写任务状态和风险记录，保持计划与实际进度同步 <!-- id: 4 -->

### 3.2 工程骨架与本地开发 (Project Foundation)

- [x] 创建 CMake 工程骨架，包含 `src/`、`include/`、`tests/`、`cmake/`、`config/`、`migrations/` 和应用入口 <!-- id: 5 -->
- [x] 建立 Clean Architecture 目录边界，区分 `domain`、`application`、`infrastructure`、`presentation` 和 `bootstrap` <!-- id: 6 -->
- [x] 配置统一编译选项，启用 C++23、警告级别和 Debug/Release 构建类型 <!-- id: 7 -->
- [x] 接入 `spdlog` 日志基础设施，并约定 TraceId、用户 ID、任务 ID 和错误上下文输出格式 <!-- id: 8 -->
- [x] 建立配置加载机制，支持数据库连接、JWT 密钥、日志级别和基础运行环境配置加载 <!-- id: 9 -->

### 3.2.1 基础类型与错误模型 (Foundation Types)

- [x] 定义强类型 ID（`UserId`、`AccountId`、`TransactionId` 等）<!-- id: 9a -->
- [x] 定义 `std::expected` 风格错误返回约定 <!-- id: 9b -->
- [x] 定义应用层错误类型，覆盖 Validation、Unauthorized、Forbidden、NotFound、Conflict、DomainRuleViolation、InfrastructureFailure <!-- id: 9c -->
- [x] 定义领域层错误类型，不依赖 HTTP 状态码 <!-- id: 9d -->
- [x] 编写强类型 ID 和错误处理的单元测试 <!-- id: 9e -->
- [x] 为 `JsonConfigLoader` 增加环境变量 overlay 支持，关键字段（JWT_SECRET、DB_PASSWORD、DB_HOST 等）优先从环境变量读取，详见 `Docs/Development/Config_Env_Overlay_Design.md` <!-- id: 9f -->

### 3.3 测试与质量门禁 (Testing & Quality Gates)

- [x] 搭建 GoogleTest 单元测试框架，并提供统一测试命令 <!-- id: 10 -->
- [x] 建立测试数据目录和测试命名规范，覆盖正常路径、边界路径和错误路径 <!-- id: 11 -->
- [x] 编写核心金融原语与领域服务的单元测试 <!-- id: 12 -->
- [x] 编写 Repository 集成测试，覆盖 PostgreSQL、事务和 outbox 落库行为 <!-- id: 13 -->
- [ ] 编写 API 接口集成测试 <!-- id: 14 -->
- [x] 增加本地质量检查命令，至少覆盖构建、测试和 Markdown 检查 <!-- id: 15 -->

### 3.4 核心金融原语 (Core Financial Primitives)

- [x] 实现 `Decimal` 定点数类型，覆盖字符串解析、规范化、比较、运算、Half-Even 舍入和溢出保护 <!-- id: 16 -->
- [x] 实现 `Currency` 值对象，校验受控支持的 ISO-4217 法币子集与加密货币白名单，并保持内部代码不可变 <!-- id: 17 -->
- [x] 实现 `Money` 值对象，禁止跨币种直接加减，并确保 JSON 金额只通过字符串进出 <!-- id: 18 -->
- [x] 实现 `ExchangeRate` 值对象，明确方向、时间戳、反向汇率和汇率精度规则 <!-- id: 19 -->
- [x] 实现 `CurrencyConversionService`，支持直接汇率、反向汇率、USD 枢纽三角折算和缺失汇率错误 <!-- id: 20 -->

### 3.5 领域模型与业务规则 (Domain Models & Rules)

- [x] 实现 `User` 与 `UserPreference` 领域对象，覆盖默认基准货币与扩展偏好映射 <!-- id: 21 -->
- [x] 实现 `Account` 与 `Transaction` 领域实体基础，覆盖账户类型、币种、归档状态、流水类型、负余额和版本字段 <!-- id: 22 -->
- [x] 补充 `Transaction` 业务分类规则，区分收入、支出、调整和转账派生流水 <!-- id: 23 -->
- [x] 实现 `TransferAggregate` 聚合根，支持三种构造模式、手续费来源和汇兑损益记录 <!-- id: 24 -->
- [x] 实现 `TransferDomainService`，只负责纯领域规则，不访问 Repository 或发布事件 <!-- id: 25 -->
- [x] 实现 `BalanceCalculationService`，覆盖余额重建、转账排除和调整流水处理 <!-- id: 26 -->
- [x] 实现分类 board 校验规则，确保收入、支出和调整类型不能误用分类 <!-- id: 27 -->

### 3.6 持久化与事务 (Repository & Persistence)

- [x] 配置 PostgreSQL 16+ 数据库连接与 Flyway 迁移脚本 <!-- id: 28 -->
- [x] 编写 Phase 1 初始迁移，覆盖用户、偏好、账户、分类、流水、汇率、余额缓存和 outbox 表 <!-- id: 29 -->
- [x] 实现 `DrogonUnitOfWork`，确保业务写入和 outbox 写入使用同一数据库事务上下文 <!-- id: 30 -->
  - 备注：当前交付为 `IUnitOfWork` + `InMemoryUnitOfWork`（语义等价，可无 DB 验证）。`DrogonUnitOfWork` SQL 适配器待 Drogon/PostgreSQL 依赖接入后替换，接口不变。
- [x] 实现 `UserRepository` 与 `UserPreferenceRepository` <!-- id: 31 -->
  - 备注：Domain 接口 + In-Memory 实现已完成；PostgreSQL 实现待接线。
- [x] 实现 `AccountRepository` 与 `TransactionRepository`，覆盖用户隔离、乐观锁和余额缓存更新 <!-- id: 32 -->
  - 备注：同上，规则已由 integration tests 覆盖。
- [x] 实现 `ExchangeRateRepository`，保证汇率 append-only 和历史时间点查询 <!-- id: 33 -->
  - 备注：同上。
- [ ] 实现 `OutboxPublisherJob`，支持 pending、failed、重试次数和 dead letter 记录 <!-- id: 34 -->

### 3.7 应用层用例 (Application Use Cases)

- [x] 实现 `CreateTransactionUseCase` 与 `DeleteTransactionUseCase`，包含权限校验、事务边界和领域错误映射 <!-- id: 35 -->
- [x] 实现 `CreateTransferUseCase`，串联账户读取、转账聚合构造、余额更新和 outbox 写入 <!-- id: 36 -->
- [x] 实现 `RefreshExchangeRatesUseCase`，负责外部汇率拉取、降级、告警事件和非阻塞调度入口 <!-- id: 37 -->
  - 备注：Provider 端口 + 降级路径已实现；真实 HTTP Provider 与后台调度入口在 S10/S11 接线。
- [x] 实现账户查询与余额查询用例，提供 API 所需 DTO，不暴露持久化模型 <!-- id: 38 -->
- [x] 实现报表 QueryService，支持 net worth、cash flow 和 dashboard summary 的最小查询 <!-- id: 39 -->
  - 备注：cash flow 显式排除 Transfer；跨币种折算走汇率仓储，缺失汇率报错。

### 3.8 表现层与 API (Presentation & APIs)

- [ ] 实现 JWT 认证与 Refresh Token 过滤器，覆盖登录、刷新、登出和黑名单撤销 <!-- id: 40 -->
- [ ] 实现 `AccountController` 与 `TransactionController`，保证金额字段以字符串接收和返回 <!-- id: 41 -->
- [ ] 实现 `TransferController`，覆盖三种转账输入模式、手续费来源和 422 业务错误响应 <!-- id: 42 -->
- [ ] 实现 `ReportController`，支持轻量级 CQRS 报表查询 <!-- id: 43 -->
- [ ] 实现统一错误响应格式，将 `std::expected` 错误映射到 REST API 设计中的 HTTP 状态码 <!-- id: 44 -->
- [ ] 注册 Drogon 全局异常处理器，确保生产响应不泄露堆栈、SQL、路径或密钥 <!-- id: 45 -->

---

## 4. 完成验收口径

### 4.1 文档类任务

- 文档已创建或更新到指定路径。
- 相关目录树、入口 README 和引用链接已经同步。
- 如存在多个可行方案，已在文档中记录待决策选项。

### 4.2 代码类任务

- 实现符合对应 `Docs/Architecture/` 设计文档。
- 覆盖关键成功路径、边界条件和错误路径测试。
- 金额、汇率、认证、事务和事件相关代码不得绕过文档中的强约束。
- 所有跨用户查询和唯一约束必须显式包含 `user_id` 或通过关联关系强约束用户边界。

### 4.3 测试类任务

- 测试可通过统一命令执行。
- 测试失败时能够定位到明确模块或行为。
- 涉及数据库、API 或外部同步的测试应说明依赖环境和初始化方式。
- 金融核心规则、事务回滚路径和错误映射优先于机械覆盖率。

---

## 5. 未来待解决任务 (Deferred / Follow-up)

来源：S01–S09 全量设计一致性 review。以下为已识别但尚未落地的偏差与缺口，按优先级排列。已在本轮立即修复的项不在此列（分类 board 校验接入 CreateTransaction、`Account.version` 统一为 `int64_t`）。

### 5.1 高优先级（进入生产写路径前必须解决）

- [ ] 接入真实持久化：实现 `DrogonUnitOfWork` 与 PostgreSQL 版 `*RepositoryImpl`，替换现有 In-Memory 实现；用同一批 integration scenarios 对真实测试库复跑 <!-- id: 46 -->
  - 说明：当前 S08 交付为「Domain 接口 + In-Memory 语义等价实现」，规则可测但未连库；#30–#33 的 Drogon 适配器部分尚未完成。
- [ ] 实现 `ICategoryRepository`，由仓储解析分类 board，替换 `CreateTransactionCommand.category_board` 的显式传入方式 <!-- id: 47 -->
  - 说明：本轮已用「命令显式携带 board + 用例校验」堵住规则缺口；长期应由 Category 持久化解析 board，避免依赖调用方传值。
- [ ] 实现转账手续费 / 汇兑损益路径：`CreateTransferCommand` 支持 `FeeSource` 与手续费金额，用例构造独立 `Adjustment` 流水，并测试「手续费不误计入 income/expense」 <!-- id: 48 -->

### 5.2 中优先级

- [ ] 实现 `ITagRepository` 与 `IAuditLogRepository` 及对应用例，打通标签与审计闭环 <!-- id: 49 -->
- [ ] 明确 `transactions` 并发更新策略：确认「流水仅软删除/追加、不做行级乐观锁」，或为 Domain `Transaction` 补 `version` 字段并接入乐观锁 <!-- id: 50 -->
- [ ] PostgreSQL `AccountRepositoryImpl` 的余额缓存 `source_version` 必须严格对齐 schema 的 `version` 语义（`MAX(version)` 或等价），不得照搬 In-Memory 的「未删除流水条数」简化实现 <!-- id: 51 -->
- [ ] 补充 `DeleteTransferUseCase`：支持删除整个转账聚合（两端 + 调整项），否则在 API 文档明确「暂不支持删除转账」 <!-- id: 52 -->

### 5.3 低优先级 / 技术债

- [ ] 落地 `pfh_application` / `pfh_infrastructure` / `pfh_presentation` CMake 库目标；随实现规模增长把 header-only 用例拆出 `.cpp` <!-- id: 53 -->
- [ ] 报表命名对齐：在架构文档补注「Phase 1 以 `ReportQueryService` 承载最小报表读路径，替代 `GenerateMonthlyReportUseCase`」 <!-- id: 54 -->
- [ ] DTO 金额符号说明：在 API 设计文档写清「业务展示的正数金额 vs 存储层带符号金额」的边界与转换规则 <!-- id: 55 -->
- [ ] `TransferResultDto` 金额来源为 Domain 正数幅度，与持久化带符号存储不同；在表现层统一对外表示口径 <!-- id: 56 -->
