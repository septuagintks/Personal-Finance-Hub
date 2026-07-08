# Personal Finance Hub (PFH) - Phase 1 Development Plan

Version: 1.0
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Draft

---

## 1. 导言与阶段目标

Phase 1 的目标是完成 PFH 后端最小可验证闭环：从工程骨架、金融原语、核心领域模型、数据库持久化到基础 REST API，形成一个可以通过自动化测试持续演进的 C++23 后端基础版本。

### 1.1 阶段原则

- **先锁定金融正确性**：`Decimal`、`Money`、`ExchangeRate` 和汇率折算规则必须先有测试保护。
- **先小闭环再扩展**：优先完成账户、流水、转账和报表的最小路径，不提前实现外部平台同步的完整能力。
- **事务与事件一致**：业务写入和 outbox 写入必须在同一个数据库事务中完成。
- **文档驱动实现**：代码行为应追随 `Docs/Architecture/` 中已经确认的规则。

### 1.2 阶段范围

Phase 1 包含：

- CMake + C++23 工程骨架。
- GoogleTest 单元测试入口。
- PostgreSQL 16+ 与 Flyway 迁移基础。
- Domain 层核心金融原语和领域模型。
- Application 层核心用例。
- Infrastructure 层 Repository、Unit of Work 和 outbox 基础设施。
- Presentation 层账户、流水、转账、报表和认证相关最小 API。

Phase 1 不包含：

- 完整外部平台同步导入。
- 前端 Vue 3 完整实现。
- Redis、RabbitMQ、Kafka 等额外基础设施。
- 多节点分布式调度优化。

### 1.3 细化子计划

Phase 1 的具体开发顺序、目录结构、实现步骤和测试收尾清单见 [Phase_1/Phase_1_Detailed_Development_Plan.md](Phase_1/Phase_1_Detailed_Development_Plan.md)。

---

## 2. 架构定位与职责边界

### 2.1 Phase 1 分层路径

```text
Presentation
  -> Application Use Case / QueryService
    -> Domain Entity / Value Object / Domain Service
      <- Infrastructure Repository / UnitOfWork
        -> PostgreSQL / Flyway
```

### 2.2 依赖规则

- Domain 层不得依赖 Drogon、PostgreSQL、JSON 库或系统时间 I/O。
- Application 层负责事务边界、用例编排和错误映射，不定义泛化的 `AccountingService`、`ExchangeRateService` 或 `ReportService`。
- Infrastructure 层实现 Repository、Unit of Work、Flyway 迁移和 outbox 投递。
- Presentation 层只负责 HTTP、DTO、认证上下文和错误响应格式。

---

## 3. 里程碑计划

### 3.1 P1.0 文档与任务基线

目标：

- 完成 Phase 1 开发计划。
- 优化 `Docs/Development/Tasks.md`，让任务项具备可验证产物。
- 同步 `Docs/README.md` 与 `Docs/Guides/Directory_Guidance.md`。

交付物：

- `Docs/Development_Plans/Phase_1_Development_Plan.md`
- `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md`
- `Docs/Development/Tasks.md`
- 更新后的目录说明文档

验收标准：

- `git diff --check` 不出现 whitespace 错误。
- 目录树只列出实际存在的文件和目录。

### 3.2 P1.1 工程骨架与测试入口

目标：

- 创建 CMake 工程骨架。
- 建立 Clean Architecture 目录边界。
- 接入 GoogleTest、基础日志和配置加载。

交付物：

- `CMakeLists.txt`
- `src/`、`include/`、`tests/`、`cmake/`
- 可运行的空测试或示例测试

验收标准：

- 本地可以执行 CMake configure 和 build。
- GoogleTest 可以通过统一命令运行。
- 编译目标启用 C++23。

### 3.3 P1.2 金融原语

目标：

- 实现 `Decimal`、`Currency`、`Money` 和 `ExchangeRate`。
- 实现 `CurrencyConversionService` 的纯内存折算逻辑。
- 用单元测试固定金额、汇率和舍入规则。

交付物：

- Domain 层金融值对象。
- 金融原语单元测试。
- 汇率折算单元测试。

验收标准：

- 金额不得通过 `float`、`double` 或 JSON number 进入领域模型。
- `Money` 禁止跨币种直接加减。
- 汇率支持直接、反向和 USD 枢纽三角折算。
- 缺失汇率返回明确错误，不使用默认 `0` 或 `1`。

### 3.4 P1.3 核心领域闭环

目标：

- 实现用户偏好、账户、流水、转账聚合和余额规则。
- 覆盖手续费来源、汇兑损益、转账排除报表统计等核心规则。

交付物：

- `User`、`UserPreference`、`Account`、`Transaction`、`TransferAggregate`。
- `TransferDomainService` 与 `BalanceCalculationService`。
- 对应业务规则单元测试。

验收标准：

- 同币种和跨币种转账均可通过三种模式构造。
- 手续费可从源账户、目标账户或第三方账户扣除。
- Transfer 不计入收入和支出统计。
- 分类 board 校验能阻止收入、支出和调整类型误用。

### 3.5 P1.4 持久化与事务闭环

目标：

- 建立 PostgreSQL 16+ 数据库连接。
- 编写 Flyway 初始迁移。
- 实现 Repository、Unit of Work 和 outbox 基础设施。

交付物：

- Flyway 初始迁移脚本。
- `UserRepository`、`AccountRepository`、`TransactionRepository`、`ExchangeRateRepository`。
- `DrogonUnitOfWork` 与 `OutboxPublisherJob`。
- Repository 集成测试。

验收标准：

- 所有跨用户数据访问都受 `user_id` 限制。
- 业务写入和 outbox 写入处于同一事务。
- 汇率记录 append-only，历史查询按 `fetched_at <= target_time` 选择最新记录。
- 事务回滚不会派发或持久化错误事件。

### 3.6 P1.5 应用层用例

目标：

- 实现账户、流水、转账、汇率刷新和报表查询的最小用例。
- 明确 `std::expected` 错误到 Presentation 层错误响应的边界。

交付物：

- `CreateTransactionUseCase`
- `DeleteTransactionUseCase`
- `CreateTransferUseCase`
- `RefreshExchangeRatesUseCase`
- 报表 QueryService

验收标准：

- 用例层统一负责权限校验、事务边界和 Repository 编排。
- Domain Rule Violation 可稳定映射为 422。
- Infrastructure Failure 不泄露数据库细节。

### 3.7 P1.6 REST API 与认证基础

目标：

- 实现 Phase 1 最小 REST API。
- 接入 JWT、Refresh Token、统一错误格式和全局异常处理器。

交付物：

- 账户 API。
- 流水 API。
- 转账 API。
- 报表 API。
- JWT 认证与 Refresh Token 过滤器。
- API 集成测试。

验收标准：

- API 路由统一以 `/api/v1` 开头。
- 金额字段在请求和响应中均为字符串。
- 错误响应统一为 `{"error_code": "STRING", "message": "Readable description"}`。
- 生产环境响应不泄露堆栈、SQL、文件路径或密钥。

---

## 4. 质量门禁

### 4.1 必跑检查

Phase 1 合并前至少执行：

```text
cmake configure
cmake build
unit tests
repository integration tests
api smoke tests
markdown check
```

### 4.2 阻断规则

- 金融原语单元测试失败，不进入持久化开发。
- Transfer、ExchangeRate、Decimal 相关测试失败，不合并。
- 事务、outbox、危险删除相关测试失败，不合并。
- API 金额字段接受 JSON number，不合并。

---

## 5. 风险与应对

### 5.1 Decimal 实现复杂度

风险：自研定点十进制容易在舍入、溢出和解析上产生隐藏错误。

应对：

- 先写边界测试，再实现。
- 明确底层使用 `__int128_t` 或 `boost::multiprecision::int128_t`。
- 所有外部输入必须先经字符串解析。

### 5.2 Drogon 事务上下文泄漏

风险：创建了事务对象，但 Repository 实际仍使用普通 `DbClient` 写库。

应对：

- `IUnitOfWork` 接口必须将同一个事务上下文传入 Repository。
- 集成测试验证业务表与 outbox 表同事务提交和回滚。

### 5.3 阶段范围膨胀

风险：提前实现外部同步、完整前端或多节点调度，导致 Phase 1 失焦。

应对：

- Phase 1 只实现后端最小闭环。
- 外部同步保留幂等设计和表结构基础，不实现完整 Provider。

---

## 6. 待决策选项

当前没有必须由维护者立即选择的架构项。

### 6.1 后续可评估事项

- **Decimal 底层实现**：优先尝试 `boost::multiprecision::int128_t`；如编译器与平台稳定支持 `__int128_t`，可在实现阶段再确认。
- **Repository 集成测试环境**：可先使用本地 PostgreSQL 测试库；后续如需要更强隔离，再引入 Testcontainer。
