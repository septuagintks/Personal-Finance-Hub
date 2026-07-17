# Personal Finance Hub 报表与分析设计

Version: 3.0
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

### 2.4 Phase 2 分析查询

`GET /api/v1/reports/analysis` 接受 `startDate=YYYY-MM`、`endDate=YYYY-MM` 与 `dimension=root_category|account|tag`，返回：

- 每个月末或当前查询时刻的净资产、资产、负债和估值状态。
- 所选维度的收入、支出与净额。
- 服务端确定的基准币种、估值时刻与 UTC 半开报表窗口。
- `current` 或 `historical` 汇率状态；缺少完整换算路径时整份请求失败。

历史账户余额直接由 Repository read model 按 `transaction_time <= valued_at` 聚合 signed amount。`Account.created_at` 是记录创建元数据，不作为业务开户日，因此后来录入的历史流水仍进入对应月份；账户从 `archived_at` 起退出净资产。

分类维度回溯到 root；账户维度使用历史账户名称；标签维度对每个标签保留完整金额，因此多标签流水会出现在多个 bucket，响应以 `dimensionOverlaps=true` 明示该语义。

### 2.5 流水 CSV 导出

`GET /api/v1/exports/transactions.csv` 复用流水的账户、类型、分类、标签、半开时间和关键字筛选。`from` 与 `to` 必填，范围最长 366 天，结果最多 10,000 行。

- Repository 以 200 行稳定 cursor 分页读取，前端不从当前页面拼接结果。
- 输出为 RFC 4180、UTF-8 BOM、CRLF 和服务端文件名，时间按用户 IANA 时区带 offset 表示。
- Expense 使用业务正 magnitude；Adjustment 与 Transfer 保留业务所需符号。
- category、tag 和 description 等文本字段若首个非空白字符为 `= + - @`，统一添加文本前缀；金额字段保持后端 Decimal 规范字符串。
- 10,000 行上限使服务端响应内存有界。是否引入 Drogon chunked transport 由 P2-S11 固定数据集的首字节和内存基准决定，不改变 CSV 契约。

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

当前实现通过 request-scoped Repository 读取领域事实并在 Application 层折算，优先保证与写模型一致的金融语义。历史净资产使用数据库聚合 read model；维度和导出使用有界游标页。性能优化必须由实际数据量和测量结果驱动。

若 Phase 2 引入数据库聚合、缓存或物化视图，必须保持：

- 现有 REST DTO 与错误语义。
- 用户时区月份窗口。
- 历史汇率时间点选择。
- Transfer 排除与 signed Adjustment 语义。
- root 分类回溯和用户隔离。

完整加密货币定价仍不在当前范围。需要未覆盖币种且没有完整历史快照时，报表返回不可用，不返回部分总计。

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
9. 净资产趋势、三种维度、标签重叠语义和账户历史回填。
10. CSV 用户时区、业务金额、RFC 4180、公式注入、范围与行数上限。

API 契约以 [REST API 设计](10_REST_API_Design.md) 和 [OpenAPI 3.1](10_REST_API_OpenAPI.json) 为准。
