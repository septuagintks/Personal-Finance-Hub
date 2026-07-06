# Personal Finance Hub (PFH) - 待办任务跟踪

Version: 1.2
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

- 新增任务时放入最合适的阶段与分组，并按顺序刷新任务 ID。
- 已完成任务不删除；如任务被替代，应新增替代任务，并在原任务后注明替代关系。
- 代码实现、架构文档和阶段计划出现冲突时，优先更新设计文档，再更新任务状态。
- 每个任务项应尽量写出可验证产物，而不是只写模块名。

---

## 2. 当前执行优先级

### 2.1 近期顺序

1. 先确认三阶段总开发计划大纲，再决定是否调整 `Phase_1_Development_Plan.md`。
2. 搭建 CMake 工程骨架、目录结构和 GoogleTest 入口。
3. 实现 `Decimal`、`Currency`、`Money` 和 `ExchangeRate`，优先锁定金额与汇率的不可变规则。
4. 实现最小领域闭环：账户、流水、转账、汇率折算和余额规则。
5. 接入 PostgreSQL、Flyway、Repository 与 Unit of Work，形成可持久化写路径。

### 2.2 当前前置条件

- 三阶段总开发计划大纲已创建：`Docs/Develop_Plan/Overall_Development_Plan.md`。
- Phase 1 开发计划已创建：`Docs/Develop_Plan/Phase_1_Development_Plan.md`。
- 是否调整 Phase 1 详细计划，等待总开发计划大纲确认后再决定。

---

## 3. 待办任务列表

### 3.1 迭代与计划 (Planning)

- [x] 编写三阶段总开发计划大纲，明确基础闭环、产品增强和预留功能阶段边界 <!-- id: 1 -->
- [x] 编写 Phase 1 开发计划文档 `Docs/Develop_Plan/Phase_1_Development_Plan.md` <!-- id: 2 -->
- [x] 制定开发计划文档格式与规范，更新到 `Docs/Standards/Documents_Format_Standard.md` <!-- id: 3 -->
- [x] 归档文档优化计划，生成 `Docs/Completed_Modifications/Documents_Optimize_3.md` <!-- id: 4 -->
- [ ] 根据三阶段总纲评审结果，决定是否调整 `Docs/Develop_Plan/Phase_1_Development_Plan.md` <!-- id: 5 -->
- [ ] 在 Phase 1 每个里程碑完成后回写任务状态和风险记录，保持计划与实际进度同步 <!-- id: 6 -->

### 3.2 工程骨架与本地开发 (Project Foundation)

- [ ] 创建 CMake 工程骨架，包含 `src/`、`include/`、`tests/`、`cmake/` 和应用入口 <!-- id: 7 -->
- [ ] 建立 Clean Architecture 目录边界，区分 `domain`、`application`、`infrastructure` 和 `presentation` <!-- id: 8 -->
- [ ] 配置统一编译选项，启用 C++23、警告级别和 Debug/Release 构建类型 <!-- id: 9 -->
- [ ] 接入 `spdlog` 日志基础设施，并约定 TraceId、用户 ID 和错误上下文输出格式 <!-- id: 10 -->
- [ ] 建立配置加载机制，支持数据库连接、JWT 密钥、日志级别和运行环境变量注入 <!-- id: 11 -->

### 3.3 测试与质量门禁 (Testing & Quality Gates)

- [ ] 搭建 GoogleTest 单元测试框架，并提供统一测试命令 <!-- id: 12 -->
- [ ] 建立测试数据目录和测试命名规范，覆盖正常路径、边界路径和错误路径 <!-- id: 13 -->
- [ ] 编写核心金融原语与领域服务的单元测试 <!-- id: 14 -->
- [ ] 编写 Repository 集成测试，覆盖 PostgreSQL、事务和 outbox 落库行为 <!-- id: 15 -->
- [ ] 编写 API 接口集成测试 <!-- id: 16 -->
- [ ] 增加本地质量检查命令，至少覆盖构建、测试和 Markdown 检查 <!-- id: 17 -->

### 3.4 核心金融原语 (Core Financial Primitives)

- [ ] 实现 `Decimal` 定点数类型，覆盖字符串解析、规范化、比较、运算、Half-Even 舍入和溢出保护 <!-- id: 18 -->
- [ ] 实现 `Currency` 值对象，校验 ISO-4217 代码并保持内部代码不可变 <!-- id: 19 -->
- [ ] 实现 `Money` 值对象，禁止跨币种直接加减，并确保 JSON 金额只通过字符串进出 <!-- id: 20 -->
- [ ] 实现 `ExchangeRate` 值对象，明确方向、时间戳、反向汇率和汇率精度规则 <!-- id: 21 -->
- [ ] 实现 `CurrencyConversionService`，支持直接汇率、反向汇率、USD 枢纽三角折算和缺失汇率错误 <!-- id: 22 -->

### 3.5 领域模型与业务规则 (Domain Models & Rules)

- [ ] 实现 `User` 与 `UserPreference` 领域对象，覆盖默认基准货币与扩展偏好映射 <!-- id: 23 -->
- [ ] 实现 `Account` 与 `Transaction` 领域实体基础，覆盖账户类型、币种、归档状态、流水类型、负余额和版本字段 <!-- id: 24 -->
- [ ] 补充 `Transaction` 业务分类规则，区分收入、支出、调整和转账派生流水 <!-- id: 25 -->
- [ ] 实现 `TransferAggregate` 聚合根，支持三种构造模式、手续费来源和汇兑损益记录 <!-- id: 26 -->
- [ ] 实现 `TransferDomainService`，只负责纯领域规则，不访问 Repository 或发布事件 <!-- id: 27 -->
- [ ] 实现 `BalanceCalculationService`，覆盖余额重建、转账排除和调整流水处理 <!-- id: 28 -->
- [ ] 实现分类 board 校验规则，确保收入、支出和调整类型不能误用分类 <!-- id: 29 -->

### 3.6 持久化与事务 (Repository & Persistence)

- [ ] 配置 PostgreSQL 16+ 数据库连接与 Flyway 迁移脚本 <!-- id: 30 -->
- [ ] 编写 Phase 1 初始迁移，覆盖用户、偏好、账户、分类、流水、汇率、余额缓存和 outbox 表 <!-- id: 31 -->
- [ ] 实现 `DrogonUnitOfWork`，确保业务写入和 outbox 写入使用同一数据库事务上下文 <!-- id: 32 -->
- [ ] 实现 `UserRepository` 与 `UserPreferenceRepository` <!-- id: 33 -->
- [ ] 实现 `AccountRepository` 与 `TransactionRepository`，覆盖用户隔离、乐观锁和余额缓存更新 <!-- id: 34 -->
- [ ] 实现 `ExchangeRateRepository`，保证汇率 append-only 和历史时间点查询 <!-- id: 35 -->
- [ ] 实现 `OutboxPublisherJob`，支持 pending、failed、重试次数和 dead letter 记录 <!-- id: 36 -->

### 3.7 应用层用例 (Application Use Cases)

- [ ] 实现 `CreateTransactionUseCase` 与 `DeleteTransactionUseCase`，包含权限校验、事务边界和领域错误映射 <!-- id: 37 -->
- [ ] 实现 `CreateTransferUseCase`，串联账户读取、转账聚合构造、余额更新和 outbox 写入 <!-- id: 38 -->
- [ ] 实现 `RefreshExchangeRatesUseCase`，负责外部汇率拉取、降级、告警事件和非阻塞调度入口 <!-- id: 39 -->
- [ ] 实现账户查询与余额查询用例，提供 API 所需 DTO，不暴露持久化模型 <!-- id: 40 -->
- [ ] 实现报表 QueryService，支持 net worth、cash flow 和 dashboard summary 的最小查询 <!-- id: 41 -->

### 3.8 表现层与 API (Presentation & APIs)

- [ ] 实现 JWT 认证与 Refresh Token 过滤器，覆盖登录、刷新、登出和黑名单撤销 <!-- id: 42 -->
- [ ] 实现 `AccountController` 与 `TransactionController`，保证金额字段以字符串接收和返回 <!-- id: 43 -->
- [ ] 实现 `TransferController`，覆盖三种转账输入模式、手续费来源和 422 业务错误响应 <!-- id: 44 -->
- [ ] 实现 `ReportController`，支持轻量级 CQRS 报表查询 <!-- id: 45 -->
- [ ] 实现统一错误响应格式，将 `std::expected` 错误映射到 REST API 设计中的 HTTP 状态码 <!-- id: 46 -->
- [ ] 注册 Drogon 全局异常处理器，确保生产响应不泄露堆栈、SQL、路径或密钥 <!-- id: 47 -->

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
