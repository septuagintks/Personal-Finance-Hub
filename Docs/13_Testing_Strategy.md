# Personal Finance Hub - Testing Strategy

Version: 1.0  
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

Repository Integration Test 使用真实 PostgreSQL 测试库或 Testcontainer。

必须覆盖：

- UserRepository
- AccountRepository
- TransactionRepository
- ExchangeRateRepository
- CategoryRepository
- TagRepository
- AuditLogRepository
- UnitOfWork

重点用例：

1. `account_balance_cache` 命中和失效重建
2. `exchange_rates` append-only，不允许覆盖历史
3. 历史汇率查询使用 `fetched_at <= target_time` 的最新记录
4. Dangerous Delete 在同一事务中清理 transactions、balance cache、account，并写 AuditLog
5. 重复分类名触发 Conflict
6. Tag 同用户唯一，跨用户可同名
7. 同步导入通过 `(provider, external_transaction_id)` 防重

---

## 5. API Tests

API Test 启动测试版 Drogon App，使用测试数据库。

必须覆盖：

- `POST /api/v1/transactions`
- `POST /api/v1/transfers`
- `GET /api/v1/accounts`
- `GET /api/v1/accounts/{id}/balance`
- `GET /api/v1/reports/net-worth`
- `GET /api/v1/reports/cash-flow`
- `GET /api/v1/reports/dashboard-summary`

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

示例：

```text
CreateTransfer_WhenOutgoingAndIncomingProvided_CalculatesRate
CashFlowReport_WhenTransferExists_ExcludesTransferFromIncomeAndExpense
ImportSync_WhenExternalTransactionRepeated_SkipsDuplicate
DangerousDeleteAccount_WhenConfirmed_CleansDataAndWritesAuditLog
```

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
```

合并阻断规则：

1. Unit Test 失败禁止合并
2. Domain Layer 覆盖率低于目标禁止合并
3. Dangerous Delete、Transfer、ExchangeRate 相关测试失败禁止合并
4. 新增 API 必须包含成功路径和至少一个错误路径测试

---

## 10. Final Rules

1. Domain 测试不依赖数据库
2. Repository 测试必须使用真实 PostgreSQL 行为
3. API 测试必须校验 HTTP 状态码和 JSON 格式
4. 所有金额测试避免 float/double
5. 所有转账测试必须验证 Transfer 不进入收入/支出
6. 同步测试必须验证幂等
7. 审计测试必须验证关键操作写入 AuditLog
