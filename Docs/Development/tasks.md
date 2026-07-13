# Personal Finance Hub (PFH) - Phase 1 待办任务跟踪

Version: 1.8
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Active

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

1. 执行 P1-S10-07，补齐 Account、Category、Tag、UserPreference Application 用例与基础资源 API；S10-04 至 S10-06 已完成专项 review。
2. 执行 P1-S10-08 至 S10-10，按 Transaction、Transfer、Report 顺序接入既有统一 HTTP 边界。
3. 执行 P1-S10-11，补齐全 API 回归并完成 S10 交付总结；S10 完整通过后进入 P1-S11。
4. P1-S12 在另一台机器完成 Linux、Docker、PostgreSQL 16+、Debug/Release 和真实运行时阻断门禁。

### 2.2 当前前置条件

- P1-S01 至 P1-S09 的 Domain、Application 与 In-Memory 验证基线已完成。
- P1-S10 以 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md` 的 3.10 节为执行边界。
- P1-S10-01 至 S10-06 已完成实现与本地离线复核；S10-06 检查点的 Windows GCC 16 基线为 265 个 unit/use-case、16 个 In-Memory integration、11 个 framework-neutral API、1 个 migration gate 和 1 个 PostgreSQL adapter contract gate，共 294/294。
- CMake configure 必须通过 `std::chrono` IANA tzdb 能力探测；Linux 运行环境必须安装 `tzdata`。
- PostgreSQL/Drogon/Argon2/OpenSSL 核心适配器与 production composition root 已实现并纳入离线编译门禁，但尚未在真实依赖和 PostgreSQL 上执行 fixture；生产写路径不得以静态门禁或 In-Memory 结果代替 #46/S12 验收。
- request RLS DbClient 与后台 BYPASSRLS/default-read-only DbClient 已分离装配；目标环境仍需证明角色权限与连接池复用不会泄漏 tenant context。

---

## 3. 待办任务列表

### 3.1 迭代与计划 (Planning)

- [x] 编写 Phase 1 开发计划文档 `Docs/Development_Plans/Phase_1_Development_Plan.md` <!-- id: 1 -->
- [x] 编写 Phase 1 细化子计划文档 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md` <!-- id: 2 -->
- [x] 根据 Phase 1 细化子计划评审结果，回写并细化 P1-S10 至 P1-S12 的执行顺序、产物与阻断门禁 <!-- id: 3 -->
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
- [x] 为 `JsonConfigLoader` 增加环境变量 overlay 支持，关键字段（JWT_SECRET、DB_PASSWORD、DB_HOST 等）优先从环境变量读取，详见 `Docs/Archive/Config_Env_Overlay_Design.md` <!-- id: 9f -->

### 3.3 测试与质量门禁 (Testing & Quality Gates)

- [x] 搭建 GoogleTest 单元测试框架，并提供统一测试命令 <!-- id: 10 -->
- [x] 建立测试数据目录和测试命名规范，覆盖正常路径、边界路径和错误路径 <!-- id: 11 -->
- [x] 编写核心金融原语与领域服务的单元测试 <!-- id: 12 -->
- [~] 编写 Repository 集成测试，覆盖事务和 outbox 落库行为 <!-- id: 13 -->
  - 更正（S09 review item 16）：集成测试当前运行在 In-Memory 语义等价实现上，**尚未对真实 PostgreSQL 执行**。事务、outbox 同事务落库、用户隔离、乐观锁、余额缓存、数值边界、历史汇率和转账聚合规则已被 13 个集成用例覆盖，但连库验证归入 #46，S12 用同一批 scenarios 对真实测试库复跑后方可标记为 `[x]`。
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
  - 澄清（S09 review item 16）：本任务交付的是**聚合结构**——三种构造模式、`FeeSource` 枚举、以及承载手续费/汇兑损益的 `adjustments` 集合，并由领域测试覆盖。**从 `CreateTransferCommand` 解析手续费并构造独立 `Adjustment` 流水的应用层路径属于 #48**，两者不冲突：#24 是领域能力，#48 是用例接线。
- [x] 实现 `TransferDomainService`，只负责纯领域规则，不访问 Repository 或发布事件 <!-- id: 25 -->
- [x] 实现 `BalanceCalculationService`，覆盖余额重建、转账排除和调整流水处理 <!-- id: 26 -->
- [x] 实现分类 board 校验规则，确保收入、支出和调整类型不能误用分类 <!-- id: 27 -->

### 3.6 持久化与事务 (Repository & Persistence)

- [~] 配置 PostgreSQL 16+ 数据库连接与 Flyway 迁移脚本 <!-- id: 28 -->
  - S10-06 进展：V1–V4、request/background 双配置、env overlay、运行期双 Drogon DbClient、角色分离和启动权限校验已接线；V4 与真实双角色连接尚未在目标 PostgreSQL 执行，随 #46/#57 验收。
- [x] 编写 Phase 1 初始迁移，覆盖用户、偏好、账户、分类、流水、汇率、余额缓存和 outbox 表 <!-- id: 29 -->
- [~] 实现 `DrogonUnitOfWork`，确保业务写入和 outbox 写入使用同一数据库事务上下文 <!-- id: 30 -->
  - S10-03 进展：`DrogonUnitOfWork` 已实现同一 Drogon Transaction 内业务写入 + outbox、异常回滚、事务级 RLS 和 commit callback 确认；离线 adapter compile gate 通过。真实提交失败、回滚和 outbox 原子性仍随 #46/S12 连库验证。
- [~] 实现 `UserRepository` 与 `UserPreferenceRepository` <!-- id: 31 -->
  - S10-03 进展：Domain、In-Memory 与 PostgreSQL 实现均已交付；偏好 fallback 与 RLS 读取固定在同一短事务。composition root 接线和真实 read-your-writes 复核仍归 #46/S10-04/S12。
- [~] 实现 `AccountRepository` 与 `TransactionRepository`，覆盖用户隔离、乐观锁和余额缓存更新 <!-- id: 32 -->
  - S10-03 进展：PostgreSQL 实现已覆盖 request-scoped RLS、`FOR UPDATE NOWAIT`、同事务乐观锁检查、余额缓存、signed 流水、Transfer + Adjustment 聚合保存/级联删除；真实并发和 NUMERIC round-trip 仍归 #46/S12。
- [~] 实现 `ExchangeRateRepository`，保证汇率 append-only 和历史时间点查询 <!-- id: 33 -->
  - S10-03 进展：PostgreSQL append 与 latest/historical/pair 查询已实现并通过离线编译门禁；真实 DB append-only trigger 与时间 round-trip 仍归 #46/S12。
- [~] 实现 `ICategoryRepository`，供创建流水校验真实分类并支持报表按一级分类聚合 <!-- id: 33a -->
  - S10-03 进展：PostgreSQL 实现已覆盖租户读取、锁定读、父子 board 校验与 root 回溯；真实 RLS/并发场景复核仍随 #46/#47/S12 落地。
- [ ] 实现 `OutboxPublisherJob`，支持 pending、failed、重试次数和 dead letter 记录 <!-- id: 34 -->

### 3.7 应用层用例 (Application Use Cases)

- [x] 实现 `CreateTransactionUseCase` 与 `DeleteTransactionUseCase`，包含权限校验、事务边界和领域错误映射 <!-- id: 35 -->
- [x] 实现 `CreateTransferUseCase`，串联账户读取、转账聚合构造、余额更新和 outbox 写入 <!-- id: 36 -->
- [~] 实现 `RefreshExchangeRatesUseCase`，负责外部汇率拉取、降级、告警事件和非阻塞调度入口 <!-- id: 37 -->
  - 更正（S09 review item 15/16）：Provider 端口 + 降级路径 + 降级告警事件（`ExchangeRateRefreshFailedEvent`，含历史汇率可用性标记）已实现；**真实 HTTP Provider 与后台非阻塞调度入口（Drogon event loop）尚未接线**，在 S10/S11 完成后方可标记 `[x]`。
- [x] 实现账户查询与余额查询用例，提供 API 所需 DTO，不暴露持久化模型 <!-- id: 38 -->
- [x] 实现报表 QueryService，支持 net worth、cash flow 和 dashboard summary 的最小查询 <!-- id: 39 -->
  - 备注：cash flow 显式排除 Transfer；跨币种折算走汇率仓储（直接/反向/USD 三角），缺失汇率报错、DB 故障映射为 InfrastructureFailure（不吞错）。
  - S09 review 增强：月窗按 `UserPreference.timezone` 计算（半开区间 `[start, end)`，未知时区显式报配置错误）；净资产按余额正负拆分资产/负债、资产分布按 `AccountType` 聚合；top-expense 支持按一级分类回溯聚合（未装配分类仓储时回退按原始 category_id）；signed Adjustment 正数计入收入、负数计入支出。

### 3.8 表现层与 API (Presentation & APIs)

- [~] 实现注册、登录、刷新、登出、JWT Filter、Refresh Token rotation/revocation 与黑名单撤销（P1-S10-06）<!-- id: 40 -->
  - S10-06 进展：bootstrap tenant 单次绑定、Argon2id、OpenSSL HS256、refresh hash/rotation/reuse detection、`iss+jti` 黑名单、session-family 撤销、同步审计/outbox 与 framework-neutral API 回归已完成；真实 Drogon/OpenSSL/Argon2/PostgreSQL 行为待 #57/S12。
- [ ] 补齐账户创建/归档、分类、标签、用户偏好 Application Use Case，并实现基础资源、`AccountController` 与 `TransactionController`；Presentation 不直接编排 Repository，金额字段以字符串接收和返回（P1-S10-07/S10-08）<!-- id: 41 -->
- [ ] 实现 `TransferController`，覆盖三种转账输入模式、手续费来源和 422 业务错误响应（P1-S10-09；#48 已完成）<!-- id: 42 -->
- [ ] 实现 `ReportController`，支持 net worth、cash flow 与 dashboard summary（P1-S10-10）<!-- id: 43 -->
- [~] 实现 HTTP DTO/parser/mapper 与统一错误响应，将 `std::expected` 映射到 HTTP 状态码并附带 TraceId（P1-S10-05）<!-- id: 44 -->
  - S10-05 进展：通用 JSON/type/RFC 3339 parser、错误 mapper、TraceId header/body 与认证 DTO 已完成；S10-07 至 S10-10 继续接入资源 DTO。
- [~] 注册 Drogon 全局异常处理器，确保生产响应不泄露堆栈、SQL、路径、密钥或底层异常文本（P1-S10-05）<!-- id: 45 -->
  - S10-05 进展：framework-neutral exception boundary 与 Drogon 全局 handler 已实现并通过 compile gate/脱敏测试；真实 Drogon runtime 待 S12。

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

- [~] 接入真实持久化：实现 `DrogonUnitOfWork` 与 PostgreSQL 版 `*RepositoryImpl`，替换现有 In-Memory 实现；用同一批 integration scenarios 对真实测试库复跑 <!-- id: 46 -->
  - S10-06 进展：核心 PostgreSQL Repository/UoW、production composition root、request/background 双 DbClient 和启动角色校验已实现，并由 OFF 模式下的全源编译门禁和结构契约门禁覆盖；真实测试库 scenarios 尚未执行，因此不得标记完成。
  - 执行归属：适配器、composition root 与测试场景在 P1-S10-03/S10-04 落地；真实 PostgreSQL 复跑与签署在另一台机器的 P1-S12-03 完成。
  - RLS 依赖：租户仓储必须在固定 Drogon Transaction 上先执行 `SET LOCAL app.current_user_id`，并在同一 Transaction 上完成后续 SQL；事务结束自动清除 GUC。禁止对普通池化 `DbClient` 先 SET 再查询，也禁止依赖手工 RESET 修复连接亲和性。
  - In-Memory 模型仍缺 `transaction_tag_relations.user_id`、`account_balance_cache.user_id` 等新增列的对应；接 SQL 时以迁移 schema 为准。
- [~] 实现 `ICategoryRepository`，由仓储解析分类 board，替换 `CreateTransactionCommand.category_board` 的显式传入方式 <!-- id: 47 -->
  - 进展（S09 review 收尾）：已交付接口 + In-Memory 实现（含锁定读取与 `resolve_root_id_for_user`），`CreateTransactionUseCase` 已将分类仓储设为必需依赖并按 `user_id + category_id` 读取真实 board；命令中的 `category_board` 已移除，报表也已接入一级分类聚合。
  - 剩余：PostgreSQL `CategoryRepositoryImpl` 已实现；仍需用真实事务、RLS 与并发场景复核（随 #46、S12）。
- [x] 实现转账手续费 / 汇兑损益承载路径：`CreateTransferCommand` 支持 `FeeSource`、手续费金额与可选第三方账户，用例构造独立 signed `Adjustment`，并测试余额、cash flow、原子回滚与级联删除 <!-- id: 48 -->
  - P1-S10-01 已完成 Application/Domain/Repository 接线；P1-S10-09 只负责 Transfer API 暴露。手续费为负 Adjustment，未来 FX gain/loss 继续复用 signed Adjustment；在没有市场基准汇率输入时不自动虚构汇兑损益。

### 5.2 中优先级

- [~] 实现 `ITagRepository` 与 `IAuditLogRepository` 及对应用例，打通标签与审计闭环（Tag 随 P1-S10-07；高危同步审计随 S10-06/S10-07；异步审计处理器随 P1-S11-03）<!-- id: 49 -->
  - S10-06 进展：`IAuditLogRepository`、In-Memory/PostgreSQL adapter 与认证同步审计已完成；Tag、资源高危操作审计和 S11 异步 handler 尚待后续。
- [x] 明确 `transactions` 并发更新策略：Phase 1 采用追加 + 软删除，不提供普通更新，不增加行级 `version`；账户聚合并发继续使用 `Account.version` <!-- id: 50 -->
- [~] PostgreSQL `AccountRepositoryImpl` 的余额缓存 `source_version` 必须严格对齐 schema 的 `version` 语义（`MAX(version)` 或等价），不得照搬 In-Memory 的「未删除流水条数」简化实现（P1-S10-03/S12-03）<!-- id: 51 -->
  - S10-03 已实现 `MAX(version)` + 最新流水 ID 双校验、同事务 rebuild/UPSERT，以及全部流水写路径缓存失效；真实 PostgreSQL 命中、失效和并发复核保留到 S12-03。
- [x] 明确转账删除边界：Phase 1 不注册转账删除路由；待 `DeleteTransferUseCase` 支持两端与调整项聚合级联并通过测试后再开放 <!-- id: 52 -->

### 5.3 低优先级 / 技术债

- [x] 落地 `pfh_application` / `pfh_infrastructure` / `pfh_presentation` CMake 库目标；Application/Presentation 已在 S10-05 随认证编排和 HTTP 边界转为静态库（P1-S10-02/S10-05）<!-- id: 53 -->
- [x] 报表命名对齐：Phase 1 以 `ReportQueryService` 承载最小报表读路径，不另设 `GenerateMonthlyReportUseCase` <!-- id: 54 -->
- [x] DTO 金额符号说明：API 设计文档已明确业务 magnitude、signed Adjustment 与存储层带符号金额的边界 <!-- id: 55 -->
- [ ] `TransferResultDto` 金额来源为 Domain 正数幅度，与持久化带符号存储不同；在表现层统一对外表示口径（P1-S10-05/S10-09）<!-- id: 56 -->

### 5.4 Phase 1 外部环境阻断门禁

- [ ] 在另一台机器执行 P1-S12：Linux Debug/Release 构建、Docker 服务启动、PostgreSQL 16+ 空库迁移、真实 Repository/UoW/RLS/并发/数值边界、API smoke、Outbox/Scheduler 测试，并记录 commit hash、环境版本、命令和结果 <!-- id: 57 -->
  - 说明：当前开发机无可用 WSL/Docker；该任务必须保留到取得可追溯测试结果，不能用 Windows 或 In-Memory 基线代替。

- [x] V3 修复后在 PostgreSQL 16.14 + Flyway OSS 10.22.0 环境对 V1-V3 执行真实空库 `migrate` / `info` / `validate`、第二次 no-op、种子数据断言和完整 CTest，确认 28 处 enum cast 修复有效 <!-- id: 58 -->
  - 外部复测提交 `4621f69`：33 条币种、55 条分类模板、27 root + 28 child、40 expense + 15 income 全部符合预期，254/254 CTest 通过；该结论只关闭迁移缺陷，不替代 #46 的真实 Repository/UoW/RLS 验收。

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
- **item 15 Config / 汇率降级**：config overlay 补 `PFH_ENVIRONMENT`、`PFH_EXCHANGE_RATE_API_KEY`，非法端口改为报错；汇率降级只有在**全部请求币种对**均有历史值时才标记 fallback 可用；成功响应必须精确覆盖请求集合且不得重复，并按币种对发刷新事件。
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

后续已在外部 macOS ARM64 + Colima Ubuntu 24.04 环境完成真实复测：PostgreSQL 16.14 空库 V1-V3、Flyway `migrate/info/validate`、第二次 no-op、全部种子断言与 254/254 CTest 均通过。详细结果见 `Phase_1_S10_PostgreSQL_Persistence_Validation_Report.md`。

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
- S10-03 持久化实现已由 S10-04 composition root 接入；真实 Drogon ABI、PostgreSQL fixture、RLS/锁/事务/NUMERIC 行为仍由 #46 与 S12 阻断。

---

## 9. P1-S10-04/S10-05/S10-06 review 收尾记录

- production composition root 仅装配 PostgreSQL adapter；request 与 background DbClient 使用不同角色和对象，启动时校验 RLS/BYPASSRLS/superuser/default-read-only 边界。
- 注册 bootstrap UoW 支持 User INSERT 后在同一 Transaction 上 `bind_tenant_once`，Preference、默认分类、refresh hash、同步 audit 和 outbox 任一失败均整体回滚。
- 通用 HTTP 边界已覆盖严格 JSON type/字段、RFC 3339、稳定错误映射、TraceId header/body 和异常脱敏；Application/Presentation 已转为静态库。
- 认证已覆盖 Argon2id、OpenSSL HS256 完整 claims 校验、Refresh Token hash-only rotation、旧 token 复用整 session 撤销、logout `iss+jti` 黑名单。
- 新增 V4 `revoked_sessions` 与认证 audit actions；该迁移只通过纸面/静态门禁，尚未在外部 PostgreSQL 执行。
- 专项 review 修复未知用户登录时序差异、密钥/pepper 生命周期清零、refresh 事务内 session 检查、空模板初始化和成功响应 TraceId 缺失。
- S10-06 检查点：Windows GCC 16 Debug 构建与 294/294 CTest 通过；PostgreSQL/production bootstrap/security 三类 compile gate 通过。真实 ABI、数据库、角色权限和连接池行为继续由 #46/#57/S12 阻断。
