# Personal Finance Hub (PFH) - Phase 1 待办任务跟踪

Version: 2.0
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Complete

---

## 1. 使用规则

### 1.1 任务状态

- `[ ]` 表示未开始或未完成。
- `[x]` 表示已经完成，并且相关文档、代码或验证结果已经落地。
- `[~]` 表示部分完成：接口/规则已交付并被测试覆盖，但仍有明确的剩余验证或实现（通常是真实 DB 连库或后台接线），在同一任务下用子项注明剩余范围与归属任务。
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

1. P1-S12-07 已完成，Phase 1 分支满足交付与合并门槛。
2. 由维护者确认后，将 `feature/phase1-foundation` 合并到 `main`。
3. 合并完成后，在独立 Phase 2 分支接续产品化开发与延期能力。

### 2.2 当前前置条件

- P1-S01 至 P1-S09 的 Domain、Application 与 In-Memory 验证基线已完成。
- P1-S10 以 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md` 的 3.10 节为执行边界。
- P1-S10-01 至 S10-11 已完成本地实现与全量 review；Windows GCC 16 / PostgreSQL OFF 基线为 272 个 unit/use-case、17 个 In-Memory integration、28 个 framework-neutral API 和 4 个静态门禁，共 321/321。
- CMake configure 必须通过 `std::chrono` IANA tzdb 能力探测；Linux 运行环境必须安装 `tzdata`。
- PostgreSQL/Drogon/Argon2/OpenSSL 核心适配器与 production composition root 已在 macOS/Colima Ubuntu ARM64 完成真实依赖 Debug/Release、V1-V6、双角色、完整 PostgreSQL fixture、API/runtime 和 Docker 门禁。
- request RLS DbClient 与后台 BYPASSRLS/default-read-only DbClient 已分离装配，并通过连接池复用、并发、故障注入、数值边界与容器后置权限断言。
- P1-S11-01 至 S11-07 已完成本地实现与全量 review；Windows GCC 16 / PostgreSQL OFF 当前为 292 个 unit/use-case、17 个 In-Memory integration、28 个 framework-neutral API 和 4 个静态门禁，共 341/341。
- S11 后台写任务只使用普通 request-role client 访问非 RLS 表；BYPASSRLS `background_db` 仅用于 `PostgresActiveCurrencyQuery`。该权限边界已在 P1-S12 fixture 与容器中通过真实角色验证。
- P1-S12-01 已在两个全新构建目录完成 Windows GCC 16.1 / PostgreSQL OFF Debug 与 Release：两者均 104/104 build steps、341/341 CTest，三类 production compile gate 通过；详细结果见 `Docs/Archive/Phase_1_S12_Delivery_Summary.md`。
- S12-02 至 S12-06 原基线已在 macOS/Colima 完成 production ON 343/343、真实 PostgreSQL/Drogon/Outbox/Scheduler 和 Docker 验证。
- Provider 修正后的 Windows PostgreSQL OFF Debug/Release 均为 349/349；新增测试固定 FreeCurrencyAPI 严格响应、exchangerate.fun superset、外部 rate Half-Even 归一、整批切换、双源失败脱敏及新环境变量优先级。
- Provider corrective round 已在 `ef66d99` 完成 Linux production ON Debug/Release 351/351、PostgreSQL OFF 349/349、四个真实 Scheduler 场景和新 Docker 冷构建/runtime；测试 API key 已轮换。
- Windows 已在返回提交 `9c470dd` 上完成 P1-S12-07：Debug/Release PostgreSQL OFF 均为 349/349，质量检查、三类 production compile gate、文档检查和全项目设计一致性 review 均通过。

---

## 3. 待办任务列表

### 3.1 迭代与计划 (Planning)

- [x] 编写 Phase 1 开发计划文档 `Docs/Development_Plans/Phase_1_Development_Plan.md` <!-- id: 1 -->
- [x] 编写 Phase 1 细化子计划文档 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md` <!-- id: 2 -->
- [x] 根据 Phase 1 细化子计划评审结果，回写并细化 P1-S10 至 P1-S12 的执行顺序、产物与阻断门禁 <!-- id: 3 -->
- [x] 在 Phase 1 每个里程碑完成后回写任务状态和风险记录，保持计划与实际进度同步 <!-- id: 4 -->

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
- [x] 为 `JsonConfigLoader` 增加环境变量 overlay 支持，关键字段（JWT_SECRET、DB_PASSWORD、DB_HOST 等）优先从环境变量读取，详见 `Docs/Archive/Config_Env_Overlay_Design.md` <!-- id: 9f -->

### 3.3 测试与质量门禁 (Testing & Quality Gates)

- [x] 搭建 GoogleTest 单元测试框架，并提供统一测试命令 <!-- id: 10 -->
- [x] 建立测试数据目录和测试命名规范，覆盖正常路径、边界路径和错误路径 <!-- id: 11 -->
- [x] 编写核心金融原语与领域服务的单元测试 <!-- id: 12 -->
- [x] 编写 Repository 集成测试，覆盖事务和 outbox 落库行为 <!-- id: 13 -->
  - P1-S12 新增 12 个真实 PostgreSQL scenario，覆盖 UoW/outbox 原子性与回滚、RLS/连接池、主要 Repository、并发锁、余额缓存、NUMERIC、历史汇率和 Transfer 聚合；与 17 个 In-Memory integration 并行保留。
- [x] 编写 framework-neutral API 接口集成测试，并以 OpenAPI/route 静态门禁和 Drogon compile gate 覆盖生产 adapter 形状；P1-S12 已补真实 Drogon/PostgreSQL API smoke <!-- id: 14 -->
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
  - 澄清（S09 review item 16）：本任务交付的是**聚合结构**——三种构造模式、`FeeSource` 枚举、以及承载手续费/汇兑损益的 `adjustments` 集合，并由领域测试覆盖。**从 `CreateTransferCommand` 解析手续费并构造独立 `Adjustment` 流水的应用层路径属于 #48**，两者不冲突：#24 是领域能力，#48 是用例接线。
- [x] 实现 `TransferDomainService`，只负责纯领域规则，不访问 Repository 或发布事件 <!-- id: 25 -->
- [x] 实现 `BalanceCalculationService`，覆盖余额重建、转账排除和调整流水处理 <!-- id: 26 -->
- [x] 实现分类 board 校验规则，确保收入、支出和调整类型不能误用分类 <!-- id: 27 -->

### 3.6 持久化与事务 (Repository & Persistence)

- [x] 配置 PostgreSQL 16+ 数据库连接与 Flyway 迁移脚本 <!-- id: 28 -->
  - P1-S12：PostgreSQL 16.14 空库 V1-V6、`info`/`validate`、第二次 no-op、legacy processing 升级、双角色和 8 张 FORCE RLS 表全部通过。
- [x] 编写 Phase 1 初始迁移，覆盖用户、偏好、账户、分类、流水、汇率、余额缓存和 outbox 表 <!-- id: 29 -->
- [x] 实现 `DrogonUnitOfWork`，确保业务写入和 outbox 写入使用同一数据库事务上下文 <!-- id: 30 -->
  - P1-S12 真实验证 commit、action error/exception/outbox error rollback、read-your-writes、commit callback 和业务事实 + outbox 原子性。
- [x] 实现 `UserRepository` 与 `UserPreferenceRepository` <!-- id: 31 -->
  - P1-S12 真实验证用户/偏好写读、fallback、注册默认值、RLS 与连接池复用隔离。
- [x] 实现 `AccountRepository` 与 `TransactionRepository`，覆盖用户隔离、乐观锁和余额缓存更新 <!-- id: 32 -->
  - P1-S12 真实验证 `FOR UPDATE NOWAIT`、乐观锁、余额缓存、signed 流水、Transfer + Adjustment、并发冲突和 NUMERIC round-trip。
- [x] 实现 `ExchangeRateRepository`，保证汇率 append-only 和历史时间点查询 <!-- id: 33 -->
  - P1-S12 真实验证 append-only trigger、latest-at-or-before、时间与 `NUMERIC(20,10)` round-trip。
- [x] 实现 `ICategoryRepository`，供创建流水校验真实分类并支持报表按一级分类聚合 <!-- id: 33a -->
  - P1-S12 真实验证租户读取、父节点锁、board 约束、root 回溯及 API 报表聚合。
- [x] 实现 `OutboxPublisherJob`，支持 pending/failed claim、processing lease、有限退避、dead letter、失败 handler/时间/摘要与旧 claim token 拒绝 <!-- id: 34 -->
  - P1-S12 已通过真实多连接 `SKIP LOCKED`、crashed-worker lease 恢复、旧 token 拒绝、完整退避/dead-letter、幂等补充审计和容器 Outbox 发布。

### 3.7 应用层用例 (Application Use Cases)

- [x] 实现 `CreateTransactionUseCase` 与 `DeleteTransactionUseCase`，包含权限校验、事务边界和领域错误映射 <!-- id: 35 -->
- [x] 实现 `CreateTransferUseCase`，串联账户读取、转账聚合构造、余额更新和 outbox 写入 <!-- id: 36 -->
- [x] 实现 `RefreshExchangeRatesUseCase`，负责外部汇率拉取、降级、告警事件和非阻塞调度入口 <!-- id: 37 -->
  - 当前 Provider 为 FreeCurrencyAPI 主源 + exchangerate.fun 整批备用源；Windows 脱敏端点契约与 mock transport、macOS 真实 libcurl HTTPS/Scheduler 主源/整批备用/双源失败路径均已通过。
- [x] 实现账户查询与余额查询用例，提供 API 所需 DTO，不暴露持久化模型 <!-- id: 38 -->
- [x] 实现报表 QueryService，支持 net worth、cash flow 和 dashboard summary 的最小查询 <!-- id: 39 -->
  - 备注：cash flow 显式排除 Transfer；跨币种折算走汇率仓储（直接/反向/USD 三角），缺失汇率报错、DB 故障映射为 InfrastructureFailure（不吞错）。
  - S09 review 增强：月窗按 `UserPreference.timezone` 计算（半开区间 `[start, end)`，未知时区显式报配置错误）；净资产按余额正负拆分资产/负债、资产分布按 `AccountType` 聚合；top-expense 支持按一级分类回溯聚合（未装配分类仓储时回退按原始 category_id）；signed Adjustment 正数计入收入、负数计入支出。

### 3.8 表现层与 API (Presentation & APIs)

- [x] 实现注册、登录、刷新、登出、JWT Filter、Refresh Token rotation/revocation 与黑名单撤销（P1-S10-06）<!-- id: 40 -->
  - S10 基础预检：bootstrap tenant、真实 Argon2/OpenSSL/Drogon/PostgreSQL、refresh rotation/reuse session revocation、logout 黑名单、hash-only 存储及审计/outbox 后置断言通过。
- [x] 补齐账户创建/归档、分类、标签、用户偏好 Application Use Case，并实现基础资源、`AccountController` 与 `TransactionController`；Presentation 不直接编排 Repository，金额字段以字符串接收和返回（P1-S10-07/S10-08）<!-- id: 41 -->
- [x] 实现 `TransferController`，覆盖三种转账输入模式、手续费来源和 422 业务错误响应（P1-S10-09；#48 已完成）<!-- id: 42 -->
- [x] 实现 `ReportController`，支持 net worth、cash flow 与 dashboard summary（P1-S10-10）<!-- id: 43 -->
- [x] 实现 HTTP DTO/parser/mapper 与统一错误响应，将 `std::expected` 映射到 HTTP 状态码并附带 TraceId（P1-S10-05/S10-11）<!-- id: 44 -->
  - S10 完成态：认证、基础资源、流水、转账和报表 DTO 已全部接入统一 parser/mapper；OpenAPI 3.1 与实际 Drogon 路由由静态门禁保持一致。
- [x] 注册 Drogon 全局异常处理器，确保生产响应不泄露堆栈、SQL、路径、密钥或底层异常文本（P1-S10-05）<!-- id: 45 -->
  - S10 基础预检：framework-neutral 与真实 Drogon runtime 的稳定错误映射、TraceId 和异常脱敏均通过。

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
- 所有租户查询和唯一约束必须显式包含 `user_id` 或通过关联关系强约束用户边界；确需跨租户的后台查询必须使用独立 Application 端口、最小权限只读角色和专用连接，不得复用 request DbClient。

### 4.3 测试类任务

- 测试可通过统一命令执行。
- 测试失败时能够定位到明确模块或行为。
- 涉及数据库、API 或外部同步的测试应说明依赖环境和初始化方式。
- 金融核心规则、事务回滚路径和错误映射优先于机械覆盖率。

---

## 5. 未来待解决任务 (Deferred / Follow-up)

来源：S01–S09 全量设计一致性 review。以下为已识别但尚未落地的偏差与缺口，按优先级排列。已在本轮立即修复的项不在此列（分类 board 校验接入 CreateTransaction、`Account.version` 统一为 `int64_t`、迁移层多租户复合外键 + RLS、转账 ID 模型统一为 `BIGSERIAL` 并移除领域层静态 ID 生成器）。

### 5.1 高优先级（进入生产写路径前必须解决）

- [x] 接入真实持久化：实现 `DrogonUnitOfWork` 与 PostgreSQL 版 `*RepositoryImpl`，替换现有 In-Memory 实现；用同一批 integration scenarios 对真实测试库复跑 <!-- id: 46 -->
  - P1-S12 已完成 12 个强制真实 PostgreSQL scenario，覆盖主要 adapter、UoW、RLS/连接池、并发、失败注入、NUMERIC、Outbox/Scheduler 和数据库时钟；生产 composition root 与真实 API 同时通过。
  - 执行归属：适配器、composition root 与测试场景在 P1-S10-03/S10-04 落地；真实 PostgreSQL 复跑与签署在另一台机器的 P1-S12-03 完成。
  - RLS 依赖：租户仓储必须在固定 Drogon Transaction 上先执行 `SET LOCAL app.current_user_id`，并在同一 Transaction 上完成后续 SQL；事务结束自动清除 GUC。禁止对普通池化 `DbClient` 先 SET 再查询，也禁止依赖手工 RESET 修复连接亲和性。
  - In-Memory 模型仍缺 `transaction_tag_relations.user_id`、`account_balance_cache.user_id` 等新增列的对应；接 SQL 时以迁移 schema 为准。
- [x] 实现 `ICategoryRepository`，由仓储解析分类 board，替换 `CreateTransactionCommand.category_board` 的显式传入方式 <!-- id: 47 -->
  - 进展（S09 review 收尾）：已交付接口 + In-Memory 实现（含锁定读取与 `resolve_root_id_for_user`），`CreateTransactionUseCase` 已将分类仓储设为必需依赖并按 `user_id + category_id` 读取真实 board；命令中的 `category_board` 已移除，报表也已接入一级分类聚合。
  - P1-S12 已完成 PostgreSQL 真实事务、RLS、父节点锁与 API 路径复核。
- [x] 实现转账手续费 / 汇兑损益承载路径：`CreateTransferCommand` 支持 `FeeSource`、手续费金额与可选第三方账户，用例构造独立 signed `Adjustment`，并测试余额、cash flow、原子回滚与级联删除 <!-- id: 48 -->
  - P1-S10-01 已完成 Application/Domain/Repository 接线；P1-S10-09 只负责 Transfer API 暴露。手续费为负 Adjustment，未来 FX gain/loss 继续复用 signed Adjustment；在没有市场基准汇率输入时不自动虚构汇兑损益。

### 5.2 中优先级

- [x] 实现 `ITagRepository` 与 `IAuditLogRepository` 及对应用例，打通标签、同步业务审计与幂等补充系统审计闭环 <!-- id: 49 -->
  - P1-S12 已通过真实 PostgreSQL receipt + AuditLog 同事务回滚/幂等和 dead-letter 审计复核。
- [x] 明确 `transactions` 并发更新策略：Phase 1 采用追加 + 软删除，不提供普通更新，不增加行级 `version`；账户聚合并发继续使用 `Account.version` <!-- id: 50 -->
- [x] PostgreSQL `AccountRepositoryImpl` 的余额缓存 `source_version` 必须严格对齐 schema 的 `version` 语义（`MAX(version)` 或等价），不得照搬 In-Memory 的「未删除流水条数」简化实现（P1-S10-03/S12-03）<!-- id: 51 -->
  - P1-S12 已真实验证 `MAX(version)` + 最新流水 ID、cache miss rebuild、写路径失效、乐观锁和并发行为。
- [x] 明确转账删除边界：Phase 1 不注册转账删除路由，普通流水删除同时拒绝 Transfer 双边与同组 Adjustment；待 `DeleteTransferUseCase` 支持完整聚合级联并通过测试后再开放 <!-- id: 52 -->

### 5.3 低优先级 / 技术债

- [x] 落地 `pfh_application` / `pfh_infrastructure` / `pfh_presentation` CMake 库目标；Application/Presentation 已在 S10-05 随认证编排和 HTTP 边界转为静态库（P1-S10-02/S10-05）<!-- id: 53 -->
- [x] 报表命名对齐：Phase 1 以 `ReportQueryService` 承载最小报表读路径，不另设 `GenerateMonthlyReportUseCase` <!-- id: 54 -->
- [x] DTO 金额符号说明：API 设计文档已明确业务 magnitude、signed Adjustment 与存储层带符号金额的边界 <!-- id: 55 -->
- [x] `TransferResultDto` 与 Transaction mapper 已统一对外金额口径：Transfer 双边/手续费为正数 magnitude，Income/Expense 由 type 表达方向，Adjustment 保留 signed 语义 <!-- id: 56 -->
### 5.4 Phase 1 外部环境阻断门禁

- [x] 在另一台机器执行 P1-S12：Linux Debug/Release 构建、Docker 服务启动、PostgreSQL 16+ 空库迁移、真实 Repository/UoW/RLS/并发/数值边界、API smoke、Outbox/Scheduler 测试，并记录 commit hash、环境版本、命令和结果 <!-- id: 57 -->
  - S12-02 至 S12-06：Colima Ubuntu ARM64 production ON Debug/Release 343/343、V1-V6 与 legacy 升级、12 个 PostgreSQL scenario、真实 Drogon API、Outbox/Scheduler、双角色和最终 Docker 镜像均 `PASS`；PostgreSQL OFF 341/341 回归通过。
  - Provider corrective round：`ef66d99` 上 production ON Debug/Release 351/351、PostgreSQL OFF 349/349；真实 FreeCurrencyAPI 主源、exchangerate.fun 整批备用、双源失败完整/不完整历史四个场景均 `PASS`。
  - 新 Docker 冷构建/runtime `PASS`：image `sha256:86d3ef5d0c29a26fc4a4d13548ba1969bf4302d0509aad27ae66ddf64c7fed1e`，healthy/non-root、双角色、8/8 FORCE RLS、Outbox/lease、唯一 JSON Content-Type、SIGTERM exit 0、无 OOM。Windows S12-07 已在返回提交上完成最终回归与签署。

- [x] V3 修复后在 PostgreSQL 16.14 + Flyway OSS 10.22.0 环境对 V1-V3 执行真实空库 `migrate` / `info` / `validate`、第二次 no-op、种子数据断言和完整 CTest，确认 28 处 enum cast 修复有效 <!-- id: 58 -->
  - 外部复测提交 `4621f69`：33 条币种、55 条分类模板、27 root + 28 child、40 expense + 15 income 全部符合预期，254/254 CTest 通过；该结论只关闭迁移缺陷，不替代 #46 的真实 Repository/UoW/RLS 验收。

### 5.5 Phase 2 移交

- [x] 明确完整加密货币定价源不属于 Phase 1：当前 FreeCurrencyAPI 不覆盖 TWD/加密货币，exchangerate.fun 可补 TWD/BTC 但缺 ETH、USDT、USDC、BNB、XRP、ADA、DOGE、SOL、TRX、MATIC、DOT、WBTC；实现任务已转入 `Phase_2_Development_Plan.md`，完成前继续按明确失败进入历史汇率降级，不拆批混源 <!-- id: 59 -->

---

## 6. S09 收尾 review 本轮修复记录

来源：S09 交付后第二、三轮一致性 review。以下为本轮**已落地并有测试覆盖**的修复，按临时清单编号归档。当前 Windows GCC 16 基线：240 单元 + 13 In-Memory 集成测试。

### 6.1 报表收尾（S10 Report API 前）

- 报表月窗按 `UserPreference.timezone` 和 IANA tzdb 计算，半开区间 `[month_start, next_month_start)`；月初/月末附近流水不再因 UTC 偏移归错月份，未知时区不再静默回退 UTC。（对应用户点名的报表时区项）
- top-expense 分类按**一级（root）分类**回溯聚合并解析分类名；无 `ICategoryRepository` 时回退按原始 category_id。（对应用户点名的占位实现项 + #47 部分）

### 6.2 领域 / 应用语义

- **item 9 金额符号 / Adjustment 语义**：Adjustment 改为带符号——正数=流入（返利/补贴/FX Gain），负数=流出（手续费/更正/FX Loss）；余额、cash flow、支出分类三处一致；零额 Adjustment 拒绝。
- **item 11 命令默认时间**：`CreateTransaction/Transfer/DeleteTransaction` 命令时间改为可选，用例缺省补 `now()`，杜绝落到 1970 epoch。
- **item 13 Domain/schema 漂移**：`Account` 增加可选 `category_override`（默认从 type 派生，可持久化覆盖）；`Category` 对齐 `source`/`template_id`/`sort_order`/`deleted_at`/`created_at`/`updated_at`；系统分类模板补 `locale` 并将唯一键纳入 locale；`UserPreference` 默认值改为 `zh-CN`/`Asia/Shanghai` 对齐 DB。

### 6.3 事件 / 事务 / 配置

- **item 12 事件契约**：新增强类型领域事件；payload 携带必备字段并对字符串执行 JSON 转义，`ExchangeRateRefreshed` 按返回币种对逐条发出且包含 `targetCurrency`；迁移与 outbox 示例均落 `occurred_at`。生产用例不再使用测试性 `SimpleDomainEvent`。
- **item 14 In-Memory 事务语义**：User/Preference 仓储改为 staged 优先；Account、Preference、Category、Transaction 增加用户归属、父分类同用户同 board、分类与流水类型等外键/约束等价校验。剩余真实事务上下文与数据库约束复核归入 #46。
- **item 15 Config / 汇率降级**：config overlay 补 `PFH_ENVIRONMENT` 与汇率 key；当前优先读取 `PFH_FREECURRENCYAPI_API_KEY`，旧 `PFH_EXCHANGE_RATE_API_KEY` / `EXCHANGE_RATE_API_KEY` 仅作兼容别名。非法端口改为报错；汇率降级只有在**全部请求币种对**均有历史值时才标记 fallback 可用；成功响应必须精确覆盖请求集合且不得重复，并按币种对发刷新事件。
- **item 10 Currency / Decimal 与 DB 边界**：Domain 币种白名单与 V2 种子统一（20 法币 + 13 加密）；用户输入在 Decimal 内部舍入前拒绝超过 `NUMERIC(20,8/10)` 的有效小数位和范围；普通流水、三种转账输入及 Repository 写入均有边界校验，转账派生金额显式按 Half-Even 舍入到 scale 8。真实 PostgreSQL 范围/舍入复核随 #46、S12 验证。

### 6.4 仍需在 S12 收尾核对（item 16）

- #13/#28/#30/#31/#32/#33/#37 已从 `[x]` 更正为 `[~]`，注明「In-Memory 语义等价已交付、真实 PostgreSQL/Drogon 连库与后台接线属 #46/S10/S11」。S12 终审需对真实测试库复跑同批 scenarios 后再定稿。
- item 14 的事务上下文真实性、item 10 的连库数值边界，均需 S12 在真实 PostgreSQL 上验证。

### 6.5 S10 报告第 4.2 节：V3 空库迁移 enum cast 修复 + 离线门禁

S10 报告 §4.2 暴露：V3 中 7 段二级分类 `INSERT ... SELECT ... UNION ALL` 的 `default_board` 列把 `'expense'`/`'income'` 推断为 `text`，写入 `category_board` enum 列触发 SQL State 42804。

本轮已：
1. 修复 V3：所有 28 处二级分类段的 `default_board` 字面量改 `'expense'::category_board` / `'income'::category_board`；28 处全部覆盖（食品 6 + 日常 5 + 交通 5 + 财务 3 + 工资 3 + 投资 4 + 红包 2）。
2. 新增 `tests/sql/validate_enum_casts.py` 静态扫描器：扫描 `migrations/V*.sql`，对 enum 列赋值处的裸字符串字面量报错；区分 CREATE TYPE 定义块与赋值语句；跳过 TEXT 列（如 `group_name`）。
3. 在根 `CMakeLists.txt` 用 `add_test(migration_enum_casts ...)` 挂入 CTest，离线可跑，无需 PostgreSQL/Flyway/Docker；带 `migrations` / `sql` 标签。
4. Mutation 验证：临时把一行 `'expense'::category_board` 改回 `'expense'`，门禁 FAIL；还原后 PASS（不漏报、不误报）。

后续已在外部 macOS ARM64 + Colima Ubuntu 24.04 环境完成真实复测：PostgreSQL 16.14 空库 V1-V3、Flyway `migrate/info/validate`、第二次 no-op、全部种子断言与 254/254 CTest 均通过。详细结果见 `Docs/Archive/Phase_1_S10_PostgreSQL_Persistence_Validation_Report.md`。

新增跟踪项：
- 当前机器无可用 WSL 发行版或 Docker，未执行当前 HEAD 的 Linux 测试；仅确认 Windows GCC 16 的 CMake tzdb 探针通过。合并 Phase 分支前必须按 `Docs/Guides/Linux_Development_Workflow.md` 在安装 `tzdata` 的 Linux 环境重新构建并全量测试。

---

## 7. P1-S10-01 交付记录

- 固定三种转账 mode 的严格字段组合，派生字段不得与输入字段同时出现。
- `CreateTransferCommand` 增加 `fee_amount`、`fee_source`、`fee_account_id`；Source/Target 禁止第三方 ID，ThirdParty 要求同用户、未归档且不同于两端账户。
- 手续费按所选账户币种解析正数 magnitude，并在 `TransferAggregate` 内构造负数 `Adjustment`；Transfer 双边流水不计收支，手续费计入 expense。
- 源、目标和第三方手续费账户在同一事务内按 ID 升序锁定；聚合保存为 group + 双边流水 + Adjustment + outbox 原子边界。
- 修复 In-Memory grouped Adjustment 未替换真实 group ID、聚合删除遗漏 Adjustment、普通流水仓储未校验账户币种三个缺口。
- 新增 17 项测试；当前 Windows GCC 16 Debug 基线为 254 unit/use-case + 16 In-Memory integration + 1 migration gate，合计 271/271 PASS。
- 该 271 项数字是 S10-01 当时基线；S10-02/S10-03 review 后当前基线见第 8 节。

---

## 8. P1-S10-02/S10-03 review 收尾记录

- 修复 CMake 错用未设置的 `PFH_HAS_POSTGRESQL` 条件，确保 ON 模式真实加入/链接 PostgreSQL 源；落地 Application/Infrastructure/Presentation 与 test-support 分层目标。
- 增加 OFF 模式 PostgreSQL 全源 compile gate 和 `postgresql_adapter_contracts`，当前 Windows GCC 16 Debug 基线为 273/273。
- 修复全部 TypedId `.value` 误用、`HomePage` 类型漂移和头文件 include 顺序依赖。
- 租户 Repository 改为 request-scoped；所有 RLS 读取固定在短 Transaction 内执行 `SET LOCAL`，不再依赖池化 DbClient 连接亲和性或手工 RESET。
- 系统级活跃币种查询从 `IAccountRepository` 拆为 `IActiveCurrencyQuery`；PostgreSQL adapter 明确要求独立后台只读角色，避免无用户 Scheduler 与 FORCE RLS 语义冲突。
- `DrogonUnitOfWork` 改用 Transaction 生命周期 + commit callback 确认提交；异常、outbox 失败和 action error 均回滚并清空事件。
- PostgreSQL 流水写入补账户币种、分类 board、NUMERIC 与 tenant 校验；Transfer Adjustment 使用新 group ID，危险删除覆盖 tag relations、第三方手续费账户与完整聚合。
- 余额缓存实现 `MAX(version)` + 最新流水 ID 双校验、同事务 rebuild/UPSERT 与全写路径失效。
- S10-03 当时的真实 Drogon ABI、PostgreSQL fixture、RLS/锁/事务/NUMERIC 阻断项，后续已由 #46 与 P1-S12 全部关闭。

---

## 9. P1-S10-04/S10-05/S10-06 review 收尾记录

- production composition root 仅装配 PostgreSQL adapter；request 与 background DbClient 使用不同角色和对象，启动时校验 RLS/BYPASSRLS/superuser/default-read-only 边界。
- 注册 bootstrap UoW 支持 User INSERT 后在同一 Transaction 上 `bind_tenant_once`，Preference、默认分类、refresh hash、同步 audit 和 outbox 任一失败均整体回滚。
- 通用 HTTP 边界已覆盖严格 JSON type/字段、RFC 3339、稳定错误映射、TraceId header/body 和异常脱敏；Application/Presentation 已转为静态库。
- 认证已覆盖 Argon2id、OpenSSL HS256 完整 claims 校验、Refresh Token hash-only rotation、旧 token 复用整 session 撤销、logout `iss+jti` 黑名单。
- 新增 V4 `revoked_sessions` 与认证 audit actions；该迁移在本检查点只通过纸面/静态门禁，后续已在 P1-S12 外部 PostgreSQL 门禁通过。
- 专项 review 修复未知用户登录时序差异、密钥/pepper 生命周期清零、refresh 事务内 session 检查、空模板初始化和成功响应 TraceId 缺失。
- S10-06 检查点：Windows GCC 16 Debug 构建与 294/294 CTest 通过；PostgreSQL/production bootstrap/security 三类 compile gate 通过。当时保留的真实 ABI、数据库、角色权限和连接池阻断项后续已由 #46/#57/P1-S12 关闭。

---

## 10. P1-S10-07 至 S10-11 review 收尾记录

- 新增 authenticated `IRequestScope` 与 `FinanceApplicationService`，每个操作创建 tenant-scoped Repository/UoW，Controller 不直接访问持久化。
- 完成 Account、Category、Tag、UserPreference、Currency、Transaction、Transfer、Net Worth、Cash Flow 与 Dashboard 路由；Phase 1 不注册 Transfer 删除接口。
- 完成 `ITagRepository` 的 In-Memory/PostgreSQL adapter、标签关系替换、软删除和资源同步审计；历史分类报表可解析已软删除的 root 名称。
- OpenAPI 3.1 契约与 Drogon route table 由 `openapi_contract` 对照；Domain 33 币种 catalog 与 V2 种子由 `currency_catalog_parity` 逐项对照。
- 全量 review 修复 Application 独立 ID/枚举校验、4096 字节说明边界、128 字节严格普通十进制输入、分类 64 层深度、父节点锁、汇率快照 `NUMERIC(20,10)`、异常响应 TraceId 和文档漂移。
- 最终 review 统一 Transaction Repository create 约束、locale 规则、删除流水同步审计、同额分类稳定排序和负时间戳取整，并修复 OpenAPI closed-object 组合冲突。
- 配置安全终审拒绝未替换的 JWT/password pepper 模板值；可选 pepper 只允许留空或使用真实密钥，loader 与 production composition root 双层 fail fast。
- 现行架构文档中的汇率数据库边界已统一为 `NUMERIC(20,10)`；`NUMERIC(30,10)` 只保留在 V1 迁移及 S07 历史说明中，并由 V5 收紧。
- 当前 Windows GCC 16 / PostgreSQL OFF：272 unit/use-case + 17 In-Memory integration + 28 framework-neutral API + 4 static gates，共 321/321 PASS；PostgreSQL/production bootstrap/security compile gates PASS。
- S10 的 V1-V5、真实 Drogon/OpenSSL/Argon2 ABI、双角色启动和核心 API smoke 已完成基础预检；当时保留的完整 PostgreSQL Repository/UoW/RLS/并发/NUMERIC、连接池复用、S11 V6/runtime、应用镜像和最终发布门禁后续已由 #46/#57/P1-S12 关闭。

---

## 11. P1-S11 review 收尾记录

- V6 新增 Outbox processing lease、claim token、失败 handler/时间、幂等 handler receipt、User/System Audit actor 和 token-guarded scheduled job lease；迁移会先恢复 legacy `processing` 行再启用约束。
- `PostgresOutboxRepository` 使用 `FOR UPDATE SKIP LOCKED` 批量 claim；状态更新必须匹配当前 claim token，失败按 1m/5m/15m/1h/6h 退避并在上限后进入 dead letter；PostgreSQL due/lease/退避时间统一基于数据库 `NOW()`，Application 只传时长，不依赖应用主机时钟。
- `SupplementalAuditHandler` 只记录汇率系统事件和 dead letter 等补充事实；普通补充审计与死信使用不同 receipt identity，避免重复或漏记。
- 本节记录 S11 当时的 OpenExchangeRates adapter；该实现已由 FreeCurrencyAPI、exchangerate.fun 与整批 Failover Provider 替代，当前规则见第 14 节。
- `RefreshExchangeRatesUseCase` 改为注入 `IClock`，失败事件携带真实 Provider identity；历史 fallback 仍要求全部目标币种对可用。
- `BoundedThreadPool`、`RecurringJob`、`DrogonTimerScheduler` 与 `JobManager` 固定 Event Loop 非阻塞边界；本机重入、队列溢出、executor/lease 异常收束、软超时、启动/停止和优雅 drain 均有测试。
- 汇率刷新和认证数据清理使用 scheduled lease；Outbox 依靠行级 claim 并行消费。过期清理覆盖 refresh token、revoked access token 和 revoked session 三张表，并以数据库 `NOW()` 判断安全记录是否真正过期。
- Production composition root 中所有后台写 adapter 使用普通 request-role DbClient；BYPASSRLS/default-read-only client 仍只注入跨租户活跃币种查询。
- Windows GCC 16 Debug、PostgreSQL OFF 全量 341/341 通过；PostgreSQL adapter、production bootstrap 和 security compile gates 通过。当时保留的 V6 与真实 runtime 阻断项后续已由 #57/P1-S12 关闭。

---

## 12. P1-S12-01 Windows 本地门禁记录

- 测试对象：`6cd41bc2c60af1298544d975c58819cc8c0600a9`，分支 `feature/phase1-foundation`，测试前主工作区干净并与远端一致。
- Windows `10.0.22631.6060` x64、GCC 16.1、CMake 4.3.2、Ninja 1.13.2；Debug 与 Release 分别使用全新构建目录，`PFH_BUILD_POSTGRESQL=OFF`。
- Debug：configure PASS、104/104 build steps、341/341 CTest；Release：configure PASS、104/104 build steps、341/341 CTest。
- 两个配置均覆盖 292 unit/use-case、17 In-Memory integration、28 framework-neutral API 和 4 static gates；PostgreSQL adapter、production bootstrap 与 production security compile gate 均编译通过。
- 本机阶段未发现产品缺陷。当时由 #57 跟踪的真实数据库、运行时和容器阻断项后续已在 S12-02 至 S12-06 及 Provider corrective round 完成。

---

## 13. P1-S12-02 至 S12-06 macOS/Linux 门禁记录

- Colima Ubuntu 24.04 ARM64、GCC 13.3、Drogon 1.8.7、PostgreSQL 16.14、Flyway 10.22.0、OpenSSL 3.0.13、Argon2 20190702。
- production ON Debug/Release configure/build 与 CTest 均 343/343；全新 PostgreSQL OFF Debug 88/88 build steps、341/341。
- V1-V6 空库 migrate/info/validate/no-op、V1-V5 legacy processing 升级、seed/schema/8 张 FORCE RLS 断言通过。
- 12 个真实 PostgreSQL scenario 与真实 Drogon API smoke 通过；runtime client 禁用宿主代理并连续 10/10 稳定复跑。
- Ubuntu 24.04 多阶段镜像冷构建通过；最终 arm64 image `sha256:b2e161b3a551b06c50d8a31760397e2e15f49e70e8049e391692f4b6a5af9217`，non-root、healthy、双角色、11/11 Outbox、两个 lease 释放、SIGTERM exit 0、无 OOM。
- 已修复 Drogon 1.8 ending ABI 与重复 Content-Type；详细缺陷、命令、环境和 blocker 见 `Docs/Archive/Phase_1_S12_Delivery_Summary.md`。

---

## 14. P1-S12 Provider 替换与 Windows 复测记录

- 外部源统一为 FreeCurrencyAPI 主源与 exchangerate.fun 无密钥备用源；主源 transport、HTTP、超时或非法/不完整响应会触发原请求批次整体切换，成功批次不混用 source。
- 两个适配器均以 SAX 保留原始 numeric token，不经 `double`；外部 feed 显式 Half-Even 归一到 10 位后再校验正数与 `NUMERIC(20,10)`，用户输入的严格 scale 拒绝规则不变。
- 配置优先使用 `PFH_FREECURRENCYAPI_API_KEY`，旧环境变量仅作兼容别名；production root 使用两个独立 HTTPS transport，并要求两次串行 hard timeout 不超过 Scheduler 软期限。
- 脱敏真实端点探测：FreeCurrencyAPI 支持批次返回 200 且集合精确；加入 TWD 后主源返回 422，exchangerate.fun 返回 200 并覆盖 CNY/TWD；未记录 key、URL 或响应正文。
- Windows GCC 16.1 / PostgreSQL OFF Debug、Release configure/build 与 CTest 均 `PASS`，349/349；三类 production compile gate 和 4 个静态门禁通过。
- 外部能力边界：主源覆盖 USD + 18 法币；备用源覆盖 20 法币 + BTC。其余 12 种加密货币定价由 #59 延期，不得伪装为本轮 Provider 已覆盖。

---

## 15. P1-S12 Provider macOS/Colima corrective round

- 初始 Drogon outbound transport 在 Ubuntu 24.04 的 Trantor 1.5.12 上无 TLS backend，稳定把明文 HTTP 发到 HTTPS 443；`ef66d99` 改用系统 libcurl 8.5.0/OpenSSL 3.0.13，并保持证书校验、HTTPS-only、无 redirect、硬超时和有界响应。
- Linux production ON Debug/Release 均 351/351，PostgreSQL OFF 349/349；两个 production ON 外部 target 均实际执行，Provider unit tests 13/13。
- 真实 Scheduler：CNY/EUR 主源 2 条快照全部来自 `FreeCurrencyAPI`；CNY/TWD 整批备用 2 条全部来自 `exchangerate.fun` 且无主源部分写入；EUR/ETH 双源失败的完整与不完整历史语义均正确。
- 新 Docker image `sha256:86d3ef5d0c29a26fc4a4d13548ba1969bf4302d0509aad27ae66ddf64c7fed1e`，36,770,560 bytes；真实主源刷新、11/11 Outbox、lease 释放、双角色、8/8 FORCE RLS、唯一 JSON Content-Type、non-root、healthy、SIGTERM exit 0 和无 OOM 均通过。
- 所有日志与仓库扫描均无 key、Authorization、完整 query URL、response body 或临时诊断材料。测试 API key 状态为已轮换。

---

## 16. P1-S12-07 最终签署记录

- Windows 已 fast-forward 到 macOS 返回提交 `9c470dd1d7c75ffc6848a741c1b8ff186620aa18`；父链包含 TLS transport 修复 `ef66d995f0f9f51e7936f43af9ddc9d524fc6e56`，主仓库接收时与远端同哈希且工作区干净。
- 两个返回提交均携带预期 macOS key id `81BFB01482975987`。Windows 当前密钥环缺少该公钥，无法独立建立信任；macOS 返回证据已记录两次 `git verify-commit` 为 `Good signature`，提交哈希、父链、主题和远端均一致。
- Windows Debug / PostgreSQL OFF 通过 `quality_check.ps1` 与 349/349 CTest；独立 Release 配置、编译与 349/349 CTest 通过。Release 仅复用项目锁定版本的 FetchContent 源码，未复用 Debug 二进制。
- 最终 review 覆盖 Clean Architecture 依赖边界、金融十进制路径、Provider 整批主备、配置与密钥注入、Scheduler/Outbox、PostgreSQL source 语义、OpenAPI、迁移、Docker 与文档状态；未发现新的产品缺陷或设计偏离。
- 全部 Phase 1 交付记录已按规范移入 `Docs/Archive/`。完整加密货币定价源作为已知能力边界转入 Phase 2，不阻断 Phase 1 签署。
- 结论：Phase 1 已完成并满足合并到 `main` 的门槛；本记录不表示分支已经实际合并。
