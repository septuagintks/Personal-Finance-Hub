# Personal Finance Hub 报表与分析设计

Version: 2.0
Backend: C++23
Architecture: Clean Architecture + Lightweight CQRS
Status: Approved

---

## 1. 架构定位

Phase 1 的报表读路径由 Application 层 `ReportQueryService` 统一承载。`FinanceApplicationService` 从认证上下文取得 `UserId`，创建 request-scoped `IRequestScope`，再使用 Account、Transaction、UserPreference、ExchangeRate 与 Category Repository 读取事实并组装 DTO。

```text
ReportController
    -> FinanceApplicationService
        -> IRequestScope
            -> ReportQueryService
                -> Repository ports
                    -> PostgreSQL adapters + RLS
```

持续边界：

- Controller 只处理查询参数、认证上下文和 HTTP 映射。
- Application 不依赖 Drogon、PostgreSQL 或 SQL 类型。
- Domain Service 不访问 Repository；跨仓储读取和汇率选择由 Application 编排。
- 报表金额使用 `Money` 与 `Decimal` 计算，并以十进制字符串离开 API。
- 所有查询由 JWT 中的用户身份限定，不接受客户端传入 `userId`。

---

## 2. 现行报表接口

### 2.1 净资产

`GET /api/v1/reports/net-worth` 返回：

```json
{
  "baseCurrency": "CNY",
  "totalAssets": "12000.00000000",
  "totalLiabilities": "-3000.00000000",
  "netWorth": "9000.00000000",
  "generatedAt": "2026-07-16T00:00:00Z"
}
```

计算规则：

1. 基准币种来自 `UserPreference.base_currency`。
2. 只读取当前用户未归档账户。
3. 每个账户余额按 `generatedAt` 可用汇率折算。
4. 折算后正数进入资产，负数进入负债；`netWorth = totalAssets + totalLiabilities`。
5. 空账户和同币种零金额不要求汇率。

### 2.2 月度现金流

`GET /api/v1/reports/cash-flow` 接受：

- `startDate=YYYY-MM`。
- `endDate=YYYY-MM`，首尾月份均包含。
- `periodType=MONTH`。

范围最长 120 个月。响应按月返回 `income` 与 `expense`，全部使用用户基准币种。

计算规则：

1. 每个月按用户 IANA 时区转换为 UTC 半开窗口 `[month_start, next_month_start)`。
2. Income 和正数 Adjustment 计入收入。
3. Expense 和负数 Adjustment 的绝对值计入支出。
4. Transfer 双腿不计入收入或支出。
5. 每笔流水使用 `fetched_at <= occurred_at` 的历史汇率，不使用未来或最新汇率猜测。
6. 未知时区、非法月份、倒置范围或缺失汇率返回明确错误。

### 2.3 Dashboard Summary

`GET /api/v1/reports/dashboard-summary` 不接受自定义日期。它以用户时区中的当前自然月返回：

- 净资产、总资产与总负债。
- 当月收入与支出。
- 活跃账户数量。
- 按 `AccountType` 聚合的资产分布。
- 按一级 root 分类聚合的 Top 支出分类。
- UTC 表示的报表窗口与生成时间。

Dashboard 使用同一个注入时刻计算净资产、月份窗口和 `generatedAt`，避免跨月边界产生不一致结果。百分比使用 Half-Even 舍入并以字符串表示。

---

## 3. 分类与交易语义

### 3.1 signed Adjustment

Adjustment 的符号是报表事实：

| 符号 | 业务含义 | 现金流 | 支出分类 |
| ---- | -------- | ------ | -------- |
| 正数 | 返利、补贴、FX Gain | 收入 | 不计入 |
| 负数 | 手续费、更正、FX Loss | 支出 | 计入 |

零额 Adjustment 在写入边界拒绝。

### 3.2 分类回溯

Top 支出分类通过 `ICategoryRepository` 将流水分类回溯到当前用户的一级 root：

- 软删除分类仍可用于历史名称解析。
- 物理缺失的历史分类归入未分类。
- 无分类流水归入未分类。
- 分类读取必须同时验证用户归属。

结果按金额降序返回；相同金额保持稳定顺序。

---

## 4. 汇率转换

报表转换按以下顺序查找，并始终使用十进制定点运算：

1. 同币种直接返回。
2. 零金额直接构造基准币种零值。
3. 使用直接历史汇率 `source -> base`。
4. 使用反向历史汇率 `base -> source` 并做除法。
5. 使用同一时间点的 USD 枢纽汇率推导交叉汇率。
6. 无完整路径时返回汇率不可用。

现金流使用流水发生时刻，净资产使用注入的当前时刻。数据库缺失汇率时不得使用 `0`、`1`、未来快照或前端默认值。

---

## 5. 多租户与错误边界

- Presentation 从已验证 JWT 提取 `UserId`。
- request-scoped Repository 在同一短事务中设置 RLS tenant。
- SQL 参数化并显式约束用户归属；FORCE RLS 提供数据库防线。
- 其他用户资源按 404 处理，避免资源枚举。
- Repository、汇率、时区和输入错误映射为稳定、脱敏的 Application 错误。
- 报表失败不返回部分聚合值，不在日志中输出用户财务明细。

---

## 6. 性能与扩展边界

当前实现通过 request-scoped Repository 读取领域事实并在 Application 层折算，优先保证与写模型一致的金融语义。性能优化必须由实际数据量和测量结果驱动。

若 Phase 2 引入数据库聚合、缓存或物化视图，必须保持：

- 现有 REST DTO 与错误语义。
- 用户时区月份窗口。
- 历史汇率时间点选择。
- Transfer 排除与 signed Adjustment 语义。
- root 分类回溯和用户隔离。

账户维度、标签维度、净资产趋势和可配置图表属于 Phase 2 增强范围，不是当前后端接口。

---

## 7. 验收规则

测试至少覆盖：

1. 正负余额的资产、负债与净资产恒等式。
2. Transfer 排除和 signed Adjustment 分流。
3. 用户时区月初、月末、夏令时与未知时区。
4. 直接、反向、USD 枢纽、历史汇率和缺失汇率。
5. root 分类聚合、软删除分类与物理缺失分类。
6. 两用户隔离、连接池复用与 RLS fail closed。
7. 金额字符串、月份参数、120 个月上限和稳定错误映射。
8. Dashboard 的单时刻一致性与百分比舍入。

API 契约以 [REST API 设计](10_REST_API_Design.md) 和 [OpenAPI 3.1](10_REST_API_OpenAPI.json) 为准。
