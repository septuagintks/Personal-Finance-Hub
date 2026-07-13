# Personal Finance Hub - Testing Strategy

Version: 1.1
Backend: C++23
Architecture: Clean Architecture + GoogleTest

---

## 1. 目标

测试策略必须优先覆盖金融正确性、事务一致性和跨层错误映射。

核心目标：

- Money、Currency、ExchangeRate 的计算稳定
- TransferAggregate 的三种推导模式正确
- Repository 与 PostgreSQL 行为一致
- REST API 的错误码和 JSON 格式稳定
- 同步导入保持幂等
- 报表显式排除 Transfer

### 1.1 Phase 1 当前基线与最终门禁

- 截至 2026-07-14，Windows GCC 16 已通过 255 个 unit/use-case、16 个 In-Memory integration scenarios、1 个 migration gate 与 1 个 PostgreSQL adapter contract gate，共 273/273；PostgreSQL 翻译单元另由 OFF 模式 compile gate 覆盖。
- V3 已在外部 PostgreSQL 16.14 + Flyway 10.22.0 全新空库完成 `migrate/info/validate`、第二次 no-op 与种子断言；该结果不包含 PostgreSQL Repository fixture。
- In-Memory scenarios 用于快速固定 Repository/UoW 语义，不是 PostgreSQL Repository Integration Test 的替代品。
- P1-S12 必须在另一台机器完成 Linux、Docker、PostgreSQL 16+、Debug/Release、真实 Repository/UoW/RLS、API 和后台任务测试；取得可追溯结果前，Phase 1 不得签署完成或合并到 `main`。

---

## 2. Test Pyramid

推荐测试层级：

```text
Unit Tests
  ↑ 数量最多，速度最快

Integration Tests
  ↑ 覆盖 PostgreSQL、Repository、事务

API Tests
  ↑ 覆盖 Controller、DTO、HTTP 映射

End-to-End Smoke Tests
  ↑ 少量关键用户路径
```

---

## 3. Unit Tests

Unit Test 不访问数据库、不启动 Drogon、不依赖网络。

必须覆盖：

- Decimal
- Currency
- Money
- ExchangeRate
- CurrencyConversionService
- TransferDomainService
- BalanceCalculationService
- Category board rule
- AccountType / AccountCategory behavior

重点用例：

1. 同币种 Money 可加减，不同币种 Money 不能直接加减
2. 汇率方向明确，反向汇率可重现
3. 同币种转账出入金额必须一致
4. 跨币种转账支持三种模式：
   - Outgoing + Rate => Incoming
   - Outgoing + Incoming => Rate
   - Incoming + Rate => Outgoing
5. 手续费、汇兑损耗必须作为 Adjustment
6. Transfer 不计入收入/支出统计
7. 分类板块校验：Income 不能使用 Expense 分类
8. 账户余额允许负数

---

## 4. Repository Integration Tests

Repository Integration Test 必须使用真实 PostgreSQL 16+ 测试库。Phase 1 的最终连库验证固定在另一台具备 Linux/Docker 环境的机器执行；可以使用容器或独立测试实例，但必须从空库执行迁移并记录 commit hash、数据库版本、命令与结果。

本地 In-Memory integration scenarios 必须与 PostgreSQL 测试尽量共享场景定义，但只能作为快速回归，任务状态应保持 `[~]` 直到真实数据库测试通过。

必须覆盖：

- UserRepository
- AccountRepository
- TransactionRepository
- ExchangeRateRepository
- CategoryRepository
- TagRepository
- AuditLogRepository
- UnitOfWork
- OutboxPublisherJob
- RefreshToken / RevokedAccessToken persistence

重点用例：

1. `account_balance_cache` 命中和失效重建
2. `exchange_rates` append-only，不允许覆盖历史
3. 历史汇率查询使用 `fetched_at <= target_time` 的最新记录
4. Dangerous Delete 在同一事务中清理 transactions、balance cache、account，并写 outbox 事件
5. 重复分类名触发 Conflict
6. Tag 同用户唯一，跨用户可同名
7. RLS 未设置用户上下文时 fail closed，连接池复用不残留前一用户上下文
8. `NUMERIC(20,8/10)` 边界、Half-Even 舍入和超界拒绝与 Domain 一致
9. 事务提交后 outbox 记录与业务事实同事务落盘
10. outbox 失败重试不会重复破坏业务事实
11. 余额缓存 `source_version` 与 schema 的账户版本语义一致

---

## 5. API Tests

API Test 启动测试版 Drogon App，使用测试数据库。

必须覆盖：

- `POST /api/v1/auth/register`
- `POST /api/v1/auth/login`
- `POST /api/v1/auth/refresh`
- `POST /api/v1/auth/logout`
- `POST /api/v1/transactions`
- `POST /api/v1/transfers`
- `GET /api/v1/accounts`
- `GET /api/v1/accounts/{id}/balance`
- `GET /api/v1/reports/net-worth`
- `GET /api/v1/reports/cash-flow`
- `GET /api/v1/reports/dashboard-summary`
- outbox 投递后的缓存失效与审计日志生成
- 事件重试不重复写坏业务事实
- TraceId、统一错误结构与生产异常脱敏

错误映射必须覆盖：

| 场景                 | 期望状态码 |
| -------------------- | ---------- |
| JSON 非法            | 400        |
| 未登录               | 401        |
| 访问其他用户资源     | 403        |
| 账户不存在           | 404        |
| 重复分类名或版本冲突 | 409        |
| 转账金额不平衡       | 422        |
| 数据库故障           | 500        |

金额字段规则：

1. 请求中的金额必须是字符串
2. 响应中的金额必须是字符串
3. API Test 必须拒绝 JSON number 金额输入

---

## 6. Business Rule Tests

业务规则测试应以场景命名。

重点场景：

- 跨币种转账
- 手续费
- 汇兑损耗
- 历史汇率
- 余额重建
- 重复导入
- 默认分类初始化
- 删除预设分类后历史流水展示
- 信用账户负余额
- 报表排除 Transfer
- DashboardSummaryDTO 聚合正确
- Dangerous Delete 提交后通过 outbox 生成审计和通知
- OutboxPublisherJob 对 pending / failed 事件的重试与 dead letter 行为

示例：

```text
CreateTransfer_WhenBothAmountsProvided_CalculatesRate
CashFlowReport_WhenTransferExists_ExcludesTransferFromIncomeAndExpense
ImportSync_WhenExternalTransactionRepeated_SkipsDuplicate
DangerousDeleteAccount_WhenConfirmed_CleansDataAndEmitsOutboxEvent
```

其中同步导入属于 Phase 3 预留能力，不作为 Phase 1 合并门禁；对应测试在实现 Provider/Import Use Case 时启用。

---

## 7. Frontend Test Scope

前端测试以关键数据协同为主：

- 金额输入组件只提交字符串
- 转账表单三种模式联动正确
- DashboardSummaryDTO 能一次渲染首页
- 401 自动退出登录
- 422 展示业务规则错误
- 用户偏好切换主题、日期格式、默认报表周期后持久化

---

## 8. Coverage Target

建议覆盖率目标：

| Layer                | Target |
| -------------------- | ------ |
| Domain Layer         | >= 90% |
| Application Layer    | >= 80% |
| Infrastructure Layer | >= 60% |
| Presentation Layer   | >= 60% |

覆盖率不是唯一目标。
金融核心规则、错误路径、事务回滚路径必须优先于机械覆盖率。

---

## 9. CI Rules

CI 至少执行：

```text
cmake configure
cmake build
unit tests
repository integration tests
api smoke tests
format/lint check
markdownlint check
```

P1-S12 的外部机器门禁还必须执行：

```text
Linux Debug configure/build/test
Linux Release configure/build/test
PostgreSQL empty-schema migration and repository tests
Docker service startup and API smoke tests
Outbox/Scheduler real-runtime tests
```

合并阻断规则：

1. Unit Test 失败禁止合并
2. Domain Layer 覆盖率低于目标禁止合并
3. Dangerous Delete、Transfer、ExchangeRate 相关测试失败禁止合并
4. 新增 API 必须包含成功路径和至少一个错误路径测试
5. 文档必须通过 Markdown 校验；每个 Phase 交付总结记录对应 Git Commit Hash，设计文档用 `Version` 跟踪内容版本，避免在提交前写入尚不存在的 hash。
6. Linux/Docker/PostgreSQL 外部门禁缺少 commit hash、环境版本、命令或结果时，禁止合并 Phase 分支。

---

## 10. Final Rules

1. Domain 测试不依赖数据库
2. Repository 测试必须使用真实 PostgreSQL 行为
3. API 测试必须校验 HTTP 状态码和 JSON 格式
4. 所有金额测试避免 float/double
5. 所有转账测试必须验证 Transfer 不进入收入/支出
6. 同步测试必须验证幂等
7. 审计测试必须验证关键操作写入 AuditLog
8. In-Memory integration scenarios 通过不能替代真实 PostgreSQL 验收
