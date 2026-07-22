# Personal Finance Hub - REST API Design

Version: 2.0

Backend: C++23

Framework: Drogon (HttpController)

Architecture: Clean Architecture (Presentation Layer)

---

## 1. 接口设计原则与规范

作为 Clean Architecture 的最外层——**表现层（Presentation Layer）**，REST API 模块仅负责处理 HTTP 协议细节（路由、状态码、JSON 序列化），并迅速将控制权移交给 Application 层的 Use Cases 或 Query Services。

### 1.1 核心设计规范

1. **版本化路由**：所有面向前端的 API 统一以 `/api/v1` 开头。
2. **凭证边界**：非浏览器客户端继续使用 `Authorization: Bearer <JWT>`；同源 Web 使用内存 Access Token 与 `HttpOnly; Secure; SameSite=Strict` Refresh Cookie。
3. **统一 JSON 响应结构**：

- 成功响应：直接返回数据对象（对象或数组）。
- 失败响应：统一采用标准错误格式：`error_code`、`message`、`trace_id`、`retryable` 和 `field_errors`。
- 认证资源和所有令牌端点统一返回 `Cache-Control: no-store`；公共货币目录保留独立的公开缓存与 ETag 规则。

### 1.2 C++23 std::expected 与 HTTP 状态码映射矩阵

表现层控制器必须将应用层返回的 `std::expected` 错误单向转换为精准的 HTTP 状态码：

| 应用层错误 (UseCaseError / RepositoryStatus) | HTTP 状态码                 | 语义与前端应对                           |
| -------------------------------------------- | --------------------------- | ---------------------------------------- |
| **成功 (Success)**                           | `200 OK` / `201 Created`    | 操作成功，前端直接消费数据               |
| **格式校验失败 (Validation Error)**          | `400 Bad Request`           | 请求 JSON 字段缺失、类型错误或数值不合规 |
| **未认证 / 令牌失效**                        | `401 Unauthorized`          | 踢回登录页                               |
| **已认证但无角色权限 (AuthorizationError)**  | `403 Forbidden`             | 当前角色无权执行某类全局操作             |
| **找不到资源 (NotFound)**                    | `404 Not Found`             | 资源不存在或不属于当前用户，避免枚举 ID  |
| **冲突 (Conflict)**                          | `409 Conflict`              | 重复分类名、版本冲突、重复提交           |
| **违反金融业务规则 (DomainRuleViolation)**   | `422 Unprocessable Entity`  | 账户已归档、跨币种金额不平衡、汇率非法   |
| **外部服务失败 (ExternalServiceError)**      | `502 Bad Gateway`           | 汇率 API 或同步 Provider 不可用          |
| **系统级故障 (InfrastructureFailure)**       | `500 Internal Server Error` | 数据库死锁、连接超时，前端提示“系统繁忙” |
| **请求队列已满**                              | `503 Service Unavailable`   | 稍后重试，响应携带 `Retry-After: 1`       |

写请求发生暂时性基础设施故障时，`retryable` 由服务端决定；前端不得据此自动重试非幂等请求。
所有已注册 API 都可能在进入 Application 前因有界请求队列饱和返回 `503`；该响应使用 `SERVICE_UNAVAILABLE`、原请求 `trace_id`、`retryable: true` 和 `Retry-After: 1`，且不会开始业务事务。

### 1.3 幂等与并发

- `POST /api/v1/accounts`、`POST /api/v1/categories`、`POST /api/v1/tags`、`POST /api/v1/transactions`、`POST /api/v1/transactions/{id}/correction` 和 `POST /api/v1/transfers` 必须携带可打印 ASCII 的 `Idempotency-Key`，长度为 1 至 128。
- 同一用户、操作和键的同一请求返回原始 DTO；请求指纹不同返回 `409 Conflict`。
- 用户主动重试同一写入意图时复用原键；客户端超时不得自动生成新键。
- 版本保护使用一个强 ETag，例如 `If-Match: "3"`；弱 ETag、多个值和非法版本返回结构化 `400`，版本冲突返回 `409`。

### 1.4 金额与符号边界

REST、Application、Domain 与 PostgreSQL 必须按以下边界转换，禁止把数据库符号约定直接泄漏给调用方：

1. 所有金额和汇率 JSON 字段都必须是十进制字符串；JSON number 一律返回 `400 Bad Request`。公开输入只接受 `-?[0-9]+(\.[0-9]+)?`（正数 magnitude 不允许负号），最长 128 字节；拒绝前导 `+`、首尾空白、`.5`、`1.` 和科学计数法。
2. Income/Expense 创建请求使用正数 magnitude，交易方向由 `type` 表达；Presentation 映射到 Application 后，由用例/Domain 生成存储符号。
3. Adjustment 创建请求使用 signed amount：正数表示流入（返利、补贴、FX Gain），负数表示流出（手续费、更正、FX Loss），零值拒绝。
4. Transaction 响应中 Income/Expense 使用业务 magnitude，方向由 `type` 表达；Adjustment 保留 signed 语义。不得把 Expense 的存储负号直接返回。
5. Transfer 的 `outgoingAmount`、`incomingAmount`、`feeAmount` 以及响应金额均为正数 magnitude；数据库 outgoing leg 和费用 Adjustment 的负号只属于持久化/领域内部。
6. 汇率使用正数十进制字符串，并满足 `NUMERIC(20,10)`；普通金额满足 `NUMERIC(20,8)`。
7. 响应金额与汇率使用 `Decimal::to_string()` 的规范形式，不承诺保留输入中的尾随零（例如输入 `"45.00"` 可响应为 `"45"`）。

---

## 2. 账户管理接口 (Account Management APIs)

### 2.1 账户 DTO 与列表

- **接口**：`GET /api/v1/accounts?status=active|archived|all`。
- `status` 默认 `active`；非法值返回 `400`。结果只包含当前认证用户的账户，并按 ID 稳定排序。
- Account DTO 不携带余额，余额始终由独立快照接口返回。

```json
{
  "id": 1,
  "name": "日常储蓄卡",
  "type": "savings",
  "subtype": "debit_card",
  "category": "asset",
  "currencyCode": "CNY",
  "description": "工资与日常支出",
  "isArchived": false,
  "archivedAt": null,
  "createdAt": "2026-07-16T01:00:00Z",
  "updatedAt": "2026-07-16T01:00:00Z",
  "version": 1
}
```

### 2.2 创建账户

- **接口**：`POST /api/v1/accounts`。
- 请求字段为 `name`、`type`、`subtype`、`currencyCode`、可选 `category` 和可选 `description`。
- `userId` 只来自认证上下文，不接受客户端传入。
- `category` 可显式覆盖类型推导的 Asset/Liability 分类；省略时 Credit 推导为 Liability，其他类型推导为 Asset。
- `currencyCode` 必须位于 Domain 白名单；初始版本为 1，初始余额由流水计算，请求不得直接写余额。
- 成功返回 `201` Account DTO，并在同一事务写入账户和 Audit。
- 请求必须携带 `Idempotency-Key`；同键同指纹重放第一次 Account DTO，同键不同指纹返回 `409`。

### 2.3 详情与余额

- `GET /api/v1/accounts/{accountId}` 返回 Account DTO 和强版本头 `ETag: "<version>"`。
- `GET /api/v1/accounts/{accountId}/balance` 返回余额快照：

```json
{
  "accountId": 1,
  "currencyCode": "CNY",
  "balance": "15430.5",
  "lastTransactionId": 4521,
  "updatedAt": "2026-07-16T04:00:00Z"
}
```

- 详情和余额都执行租户所有权校验；不存在或属于其他用户统一返回 `404`。

### 2.4 修改账户

- **接口**：`PUT /api/v1/accounts/{accountId}`。
- 必须携带详情响应中的强 `If-Match`；缺失或非法 ETag 返回 `400`，版本过期或行锁竞争返回 `409`。
- 请求完整提交 `name`、`type`、`subtype`、`category`、`currencyCode` 和 `description`，成功返回新 Account DTO 与新 ETag。
- 账户从未存在任何流水时可以修改币种；一旦存在活动或已软删除流水，币种永久冻结，修改返回 `422`。
- 版本检查、历史流水检查、账户更新、Audit 和 Outbox 在同一 Unit of Work 内完成。

### 2.5 归档与恢复

- `POST /api/v1/accounts/{accountId}/archive` 与 `POST /api/v1/accounts/{accountId}/restore` 都必须携带强 `If-Match`，成功返回 `204`。
- 归档后禁止新增流水或参与转账，历史流水和报表读取规则保持不变。
- 恢复后账户重新进入 active 列表并可参与新写入。
- 重复归档、重复恢复、过期版本和锁竞争返回 `409`；账户、Audit 和 Outbox 同事务提交。

### 2.6 危险删除账户

- **接口**：`DELETE /api/v1/accounts/{accountId}?confirmations=3`。
- `confirmations` 必须精确等于 3，否则返回 `400`；成功返回 `204`。
- 同一事务按依赖顺序清除触及账户的完整 Transfer 聚合、普通流水、标签关系、余额缓存和账户，并写入 DangerousDelete Audit 与 Outbox。
- 该接口不可恢复。Web 客户端必须展示影响范围、执行三阶段确认并校验账户名，但服务端仍以认证用户、资源归属和 `confirmations=3` 为权威边界。

---

## 3. 账务流水与转账接口 (Transaction & Transfer APIs)

### 3.1 创建单笔收支流水 (Income/Expense)

- **HTTP 方法**：`POST`
- **路径**：`/api/v1/transactions`
- **请求负载**：

```json
{
  "accountId": 1,
  "type": "expense",
  "amount": "45.00",
  "currencyCode": "CNY",
  "categoryId": 12,
  "tagIds": [7, 9],
  "description": "午餐打卡",
  "occurredAt": "2026-07-13T04:30:00Z"
}
```

- `occurredAt` 可省略；省略时由 Use Case 写入当前时间，不得默认成 Unix epoch。
- Income/Expense 的 `amount` 必须为正数 magnitude；Adjustment 按 1.3 节使用 signed amount。
- **响应负载 (201 Created)**：

```json
{
  "id": 4522,
  "accountId": 1,
  "type": "expense",
  "amount": "45",
  "currencyCode": "CNY",
  "categoryId": 12,
  "occurredAt": "2026-07-13T04:30:00Z"
}
```

请求时间可携带 RFC 3339 offset 和最多 9 位小数秒；写入 Application 后统一截到
PostgreSQL `TIMESTAMPTZ` 的 6 位微秒精度。所有响应时间规范化为 UTC `Z`，不回显
客户端原始 offset；非零小数秒裁剪尾随零，整秒不额外输出小数部分。

### 3.2 流水列表与详情

- `GET /api/v1/transactions` 使用 `(occurredAt DESC, id DESC)` 的不透明 keyset cursor，响应为 `{items, nextCursor}`；`pageSize` 为 1 至 200，默认 50。
- 组合筛选支持 `accountId`、`type`、`categoryId`、`tagId`、半开时间窗口 `from/to` 和最长 128 字符的 description 关键字；显式时间窗口最长 366 天。
- `GET /api/v1/transactions/{id}` 可读取活动或已软删除的当前用户流水，并返回历史分类名、历史标签、Transfer group 与更正双向链接。
- Income/Expense 响应金额为正数 magnitude，Adjustment 保留 signed 语义，Transfer 保留方向符号并显式返回 `transferGroupId`。

### 3.3 原子更正与普通删除

- `POST /api/v1/transactions/{id}/correction` 接收与普通流水创建相同的替代事实字段和标签集合。
- 同一 Unit of Work 按确定锁序创建替代流水、软删除原流水、写入 `transaction_corrections`、失效两个账户缓存并提交 Audit、幂等响应和 Outbox；任一步失败全部回滚。
- 成功响应通过 `correctsTransactionId` 指向原流水；原流水详情通过 `correctedByTransactionId` 指向替代流水。更正链可继续追加，不原地覆盖历史事实。
- 普通 `DELETE` 与 correction 都拒绝 Transfer leg 和带 `transfer_group_id` 的 Adjustment；转账只能由聚合级用例变更。

### 3.4 Transfer 聚合工作流

- **HTTP 方法**：`POST`
- **路径** : `/api/v1/transfers`
- **请求负载 (对应 `CreateTransferCommand`)**：

```json
{
  "sourceAccountId": 2,
  "targetAccountId": 1,
  "mode": "BothAmounts",
  "outgoingAmount": "100.00",
  "incomingAmount": "718.00",
  "rate": null,
  "feeAmount": "2.00",
  "feeSource": "SourceAccount",
  "feeAccountId": null,
  "description": "资金回国",
  "occurredAt": "2026-07-13T12:40:00+08:00"
}
```

模式与必填字段：

| `mode`             | 必填输入                              | 派生字段         |
| ------------------ | ------------------------------------- | ---------------- |
| `OutgoingAndRate`  | `outgoingAmount` + `rate`             | `incomingAmount` |
| `BothAmounts`      | `outgoingAmount` + `incomingAmount`   | `rate`           |
| `IncomingAndRate`  | `incomingAmount` + `rate`             | `outgoingAmount` |

- 每种 mode 的派生字段必须为 `null` 或省略；不得同时提交三个值并让服务端猜测优先级。
- 账户币种由 `sourceAccountId` 和 `targetAccountId` 解析，请求不重复接受可伪造的币种字段。
- `feeAmount` 为可选正数 magnitude；不提供手续费时，`feeSource` 与 `feeAccountId` 也必须省略。
- `SourceAccount` / `TargetAccount` 必须省略 `feeAccountId`，手续费分别使用源/目标账户币种。
- `ThirdParty` 必须提供属于当前用户、未归档且不同于两端账户的 `feeAccountId`，手续费使用该账户币种。
- P1-S10-01 已完成 Application/Domain 接线：手续费持久化为同一 transfer group 下的负数 `Adjustment`，不并入双边主金额。Controller 只做字段映射，不得自行拼接 Adjustment。
- Transfer 双边流水不计入 income/expense；负手续费 Adjustment 计入 expense。正 Adjustment 预留给返利或 FX gain。
- **响应负载 (201 Created)**：

```json
{
  "transferGroupId": 88,
  "mode": "BothAmounts",
  "sourceAccountId": 2,
  "targetAccountId": 1,
  "outgoingTransactionId": 4523,
  "incomingTransactionId": 4524,
  "adjustmentTransactionIds": [4525],
  "outgoingAmount": "100.00",
  "incomingAmount": "718.00",
  "sourceCurrencyCode": "USD",
  "targetCurrencyCode": "CNY",
  "rate": "7.18",
  "feeAmount": "2.00",
  "feeSource": "SourceAccount",
  "feeAccountId": 2,
  "feeCurrencyCode": "USD",
  "description": "资金回国",
  "occurredAt": "2026-07-13T04:40:00Z",
  "createdAt": "2026-07-13T04:40:01Z",
  "deletedAt": null,
  "correctsTransferGroupId": null,
  "correctedByTransferGroupId": null
}
```

- `GET /api/v1/transfers` 使用 `(occurredAt DESC, transferGroupId DESC)` 的不透明 cursor，支持账户与最长 366 天的半开时间窗口；响应只包含活动聚合。
- `GET /api/v1/transfers/{id}` 返回完整组级投影，包括模式、双边账户/币种/金额、rate、手续费、成员流水、删除时间及更正双向链接；当前用户可读取已软删除历史。
- `POST /api/v1/transfers/{id}/correction` 使用与创建相同的三模式请求和 `Idempotency-Key`。同一事务追加替代组、软删除旧组全部成员、写入 `transfer_corrections`、失效缓存并提交 Audit/Outbox；失败不改变旧聚合。
- `DELETE /api/v1/transfers/{id}` 在同一事务软删除双腿与全部 grouped Adjustment，保留 `transfer_groups` 历史事实；重复删除返回 `409`。
- 聚合更正和删除先锁定原组及成员，再按账户 ID 升序锁定旧、新与手续费账户。普通 Transaction 删除/更正继续拒绝任何 Transfer 成员，前端不得串联单腿操作模拟聚合写入。

---

## 4. 报表与分析接口 (Reporting & Analytics APIs)

基于轻量级 CQRS 架构，报表接口直接透传 QueryService 聚合出的高维 DTO。

### 4.1 获取首页聚合摘要 (Dashboard Summary)

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/reports/dashboard-summary`
- **请求参数 (Query)**：无。服务端按 `UserPreference.timezone` 和当前时钟计算本地自然月，并转换为半开 UTC 窗口 `[monthStart, nextMonthStart)`；提交 `startDate` 或 `endDate` 返回 `400 Bad Request`。
- **响应负载 (200 OK)**：

```json
{
  "baseCurrency": "CNY",
  "netWorth": {
    "baseCurrency": "CNY",
    "totalAssets": "125000.00",
    "totalLiabilities": "-4500.00",
    "netWorth": "120500.00",
    "generatedAt": "2026-06-23T05:45:12Z"
  },
  "monthlyIncome": "18000.00",
  "monthlyExpense": "3900.00",
  "assetDistribution": [
    { "label": "Savings", "amount": "82000.00", "percentage": "68.0%" },
    { "label": "Investment", "amount": "43000.00", "percentage": "35.7%" },
    { "label": "Credit", "amount": "-4500.00", "percentage": "-3.7%" }
  ],
  "topExpenseCategories": [
    {
      "categoryId": 12,
      "categoryName": "餐饮",
      "amount": "1200.00",
      "percentage": "30.8%"
    },
    {
      "categoryId": 18,
      "categoryName": "交通",
      "amount": "650.00",
      "percentage": "16.7%"
    }
  ],
  "reportPeriodStart": "2026-05-31T16:00:00Z",
  "reportPeriodEnd": "2026-06-30T16:00:00Z",
  "generatedAt": "2026-06-23T05:45:12Z"
}
```

### 4.2 获取个人净资产综合看板 (Net Worth Summary)

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/reports/net-worth`
- **响应负载 (200 OK)**：

```json
{
  "baseCurrency": "CNY",
  "totalAssets": "125000.00",
  "totalLiabilities": "-4500.00",
  "netWorth": "120500.00",
  "generatedAt": "2026-03-23T05:45:12Z"
}
```

### 4.3 获取特定时间段的收支趋势 (Cash Flow)

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/reports/cash-flow`
- **请求参数 (Query)**：`?startDate=2026-01&endDate=2026-03&periodType=MONTH`
- 起止月份均包含在结果中，最多 120 个月；超出当前 C++ `system_clock` 可表示范围的月份返回 `400 Bad Request`，不得发生时间点缩窄溢出。
- **响应负载 (200 OK)**：

```json
{
  "baseCurrency": "CNY",
  "trends": [
    { "period": "2026-01", "income": "15000.00", "expense": "4200.00" },
    { "period": "2026-02", "income": "15000.00", "expense": "6100.00" },
    { "period": "2026-03", "income": "18000.00", "expense": "3900.00" }
  ]
}
```

---

### 4.4 净资产趋势与维度分析

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/reports/analysis`
- **请求参数**：`?startDate=2026-01&endDate=2026-07&dimension=root_category`
- `dimension` 可取 `root_category`、`account` 或 `tag`；范围最多 120 个月且不得位于未来。
- 响应包含 `baseCurrency`、`valuationAt`、`rateStatus`、服务端报表窗口、月度 `netWorthTrend` 和所选 `breakdown`。标签维度可重叠，由 `dimensionOverlaps` 明示。
- 任何必要汇率不可用时返回 `422`，不返回部分趋势或 breakdown。

### 4.5 流水 CSV 导出

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/exports/transactions.csv`
- **请求参数**：`from`、`to` 必填；可选 `accountId`、`type`、`categoryId`、`tagId` 与 `keyword`。
- 时间范围为半开窗口且最长 366 天；超过 10,000 行返回 `400`，要求缩小范围。
- 成功响应为 `text/csv; charset=utf-8`，并返回 `Content-Disposition` 与 `X-Export-Row-Count`。时间按用户时区输出，文本字段执行公式注入防护，金额使用服务端 Decimal 字符串。

---

### 4.6 分类、标签、用户偏好与货币接口

### 4.7 获取分类树

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/categories`
- **请求参数**：`?board=expense` 或 `?board=income`
- **响应负载 (200 OK)**：

```json
[
  {
    "id": 12,
    "name": "餐饮",
    "board": "expense",
    "source": "system",
    "parentId": null,
    "children": [
      {
        "id": 13,
        "name": "早餐",
        "board": "expense",
        "source": "system",
        "parentId": 12,
        "children": []
      }
    ]
  }
]
```

### 4.8 新增或启用分类

- **HTTP 方法**：`POST`
- **路径**：`/api/v1/categories`

```json
{
  "board": "expense",
  "name": "咖啡",
  "parentId": 12,
  "templateId": null
}
```

如果 `templateId` 不为空，表示从 `system_category_templates` 启用预设分类。

创建请求必须携带 `Idempotency-Key`，其重放与冲突规则同 1.3 节。

### 4.9 删除分类

- **HTTP 方法**：`DELETE`
- **路径**：`/api/v1/categories/{id}`
- **响应负载**：`204 No Content`

删除分类使用软删除，历史流水保持引用。

### 4.10 标签接口

```text
GET    /api/v1/tags
POST   /api/v1/tags
DELETE /api/v1/tags/{id}
PUT    /api/v1/transactions/{id}/tags
```

`PUT /api/v1/transactions/{id}/tags` 请求：

```json
{
  "tagIds": [1, 2, 3]
}
```

Each transaction accepts at most 64 unique tag IDs.

Tag 不参与余额计算，只用于过滤、搜索和报表维度扩展。

`POST /api/v1/tags` 必须携带 `Idempotency-Key`，其重放与冲突规则同 1.3 节。

### 4.11 用户偏好接口

```text
GET /api/v1/users/me/preferences
PUT /api/v1/users/me/preferences
```

响应/请求 DTO：

```json
{
  "baseCurrency": "CNY",
  "locale": "zh-CN",
  "timezone": "Asia/Shanghai",
  "dateFormat": "YYYY-MM-DD",
  "numberFormat": "1,234.56",
  "theme": "system",
  "defaultHomePage": "dashboard",
  "defaultReportPeriod": "current_month"
}
```

`locale` 与注册接口的 `preferredLocale` 使用同一 `LocaleTag` 约束：最长 16
字符，只接受由单个 `-` 分隔的 ASCII 字母数字段（例如 `zh-CN`、`en-US`）。
Application 层必须重复执行同一校验，不能只依赖 JSON Schema。

### 4.12 货币元数据接口

- **HTTP 方法**：`GET`
- **路径**：`/api/v1/currencies`

```json
[
  {
    "code": "CNY",
    "symbol": "¥",
    "precision": 2,
    "displayName": "Chinese Yuan",
    "isCrypto": false
  },
  {
    "code": "JPY",
    "symbol": "¥",
    "precision": 0,
    "displayName": "Japanese Yen",
    "isCrypto": false
  },
  {
    "code": "BTC",
    "symbol": "₿",
    "precision": 8,
    "displayName": "Bitcoin",
    "isCrypto": true
  }
]
```

响应的弱 ETag 从完整币种目录内容稳定计算；code、symbol、precision、显示名、
加密货币标记或顺序变化时 ETag 必须随之变化，不使用需要人工递增的固定版本号。

---

### 4.13 用户维护接口

```text
GET  /api/v1/maintenance/audit-logs
POST /api/v1/maintenance/accounts/balance-cache/rebuild
POST /api/v1/maintenance/accounts/{accountId}/balance-cache/rebuild
```

- 维护接口要求已认证 USER 或 OPERATOR；查询条件中的用户 ID 固定来自认证上下文，不能由客户端覆盖。
- 审计列表按唯一 audit ID 降序游标分页，可按 action、resource type 和半开时间窗口筛选；只返回当前用户的 user actor 事实、批准字段和可选 TraceId。
- 余额重建读取权威 ledger，单账户越权统一返回 `404`；全部账户只处理当前租户。重建不得改变账务事实或余额，只刷新缓存并写入同事务 Audit。

### 4.14 健康与 Operator 运维接口

```text
GET  /livez
GET  /readyz
GET  /api/v1/operations/summary
GET  /api/v1/operations/metrics
GET  /api/v1/operations/dead-letters
POST /api/v1/operations/dead-letters/{outboxId}/retry
```

- `/livez` 不经过有界应用队列，只证明进程可响应；`/readyz` 脱敏检查 request DB、必要 migration 与 Scheduler 启动状态，未就绪返回 `503`。
- Operations API 必须同时通过 JWT 和服务端当前持久化 `OPERATOR` 角色校验；USER 返回 `403`。Metrics 采用 Prometheus 文本格式并要求同一 Operator 认证。
- summary 固定返回 `httpAdmission` 与 `reportResources`，同时公开当前不可变容量和进程生命周期内的单调拒绝计数。Prometheus 对应使用低基数的 `pfh_http_admission_*` 与 `pfh_report_resource_*`；不得使用路径、用户、TraceId 或财务数据作为 label。
- dead-letter 列表只返回脱敏标识、事件类型、状态、重试计数与时间，不返回 payload、原始错误、claim token 或 owner。
- 重试请求必须携带 `Idempotency-Key`；服务端使用持久化 retry command、行锁和状态条件保证并发重试只发生一次，并在同一事务写 Operator Audit 与 TraceId。

### 4.15 OpenAPI / JSON Schema Contract

当前可执行契约见 [`10_REST_API_OpenAPI.json`](10_REST_API_OpenAPI.json)，采用 OpenAPI 3.1。路由、HTTP 方法、唯一 `operationId`、金额 Schema、Web Cookie 安全方案和 Drogon adapter 注册表由静态门禁逐次比较；前端 DTO 从该文件生成。
以下规则是 JSON Schema 的全局约束：

1. 金额字段必须是 `type: string`，并使用十进制字符串格式
2. 汇率字段必须是 `type: string`
3. ID 字段对外使用 integer 或 string，但 UUID 必须使用 string
4. 错误响应统一使用 `ErrorResponse`
5. DTO 字段名使用 lowerCamelCase
6. Locale 字段统一引用 `LocaleTag`：`^[A-Za-z0-9]+(?:-[A-Za-z0-9]+)*$`
7. `additionalProperties: false` 的对象不得再通过 `allOf` 增加属性；扩展响应必须声明完整属性集，避免 JSON Schema 组合后互相拒绝

核心 Schema：

```yaml
MoneyDTO:
  type: object
  required: [amount, currency]
  properties:
    amount:
      type: string
      pattern: "^-?[0-9]+(\\.[0-9]+)?$"
    currency:
      type: string
      minLength: 3
      maxLength: 8

CreateTransactionRequest:
  type: object
  required: [accountId, type, amount, currencyCode]
  properties:
    accountId: { type: integer, format: int64 }
    type: { type: string, enum: [income, expense, adjustment] }
    amount: { type: string, maxLength: 128, pattern: "^-?[0-9]+(\\.[0-9]+)?$" }
    currencyCode: { type: string, minLength: 3, maxLength: 8 }
    categoryId: { type: integer, format: int64, nullable: true }
    description: { type: string }
    occurredAt: { type: string, format: date-time, nullable: true }

CreateTransferRequest:
  type: object
  required: [sourceAccountId, targetAccountId, mode]
  properties:
    sourceAccountId: { type: integer, format: int64 }
    targetAccountId: { type: integer, format: int64 }
    mode: { type: string, enum: [OutgoingAndRate, BothAmounts, IncomingAndRate] }
    outgoingAmount: { type: string, maxLength: 128, pattern: "^[0-9]+(\\.[0-9]+)?$", nullable: true }
    incomingAmount: { type: string, maxLength: 128, pattern: "^[0-9]+(\\.[0-9]+)?$", nullable: true }
    rate: { type: string, maxLength: 128, pattern: "^[0-9]+(\\.[0-9]+)?$", nullable: true }
    feeAmount: { type: string, maxLength: 128, pattern: "^[0-9]+(\\.[0-9]+)?$", nullable: true }
    feeSource: { type: string, enum: [SourceAccount, TargetAccount, ThirdParty], nullable: true }
    feeAccountId: { type: integer, format: int64, nullable: true }
    description: { type: string }
    occurredAt: { type: string, format: date-time, nullable: true }

ErrorResponse:
  type: object
  required: [error_code, message, trace_id, retryable, field_errors]
  properties:
    error_code:
      type: string
    message:
      type: string
    trace_id:
      type: string
    retryable:
      type: boolean
    field_errors:
      type: array
      items:
        $ref: '#/components/schemas/FieldError'

UserPreferenceDTO:
  type: object
  required:
    - baseCurrency
    - locale
    - timezone
    - dateFormat
    - numberFormat
    - theme
    - defaultHomePage
    - defaultReportPeriod

DashboardSummaryDTO:
  type: object
  required:
    - baseCurrency
    - netWorth
    - monthlyIncome
    - monthlyExpense
    - assetDistribution
    - topExpenseCategories
    - reportPeriodStart
    - reportPeriodEnd
    - generatedAt
```

OpenAPI 文档必须作为前后端共同契约；新增接口时，先补 Schema，再实现 Controller 和前端调用。

---

## 5. Drogon HttpController 落地边界

Controller 只负责 HTTP 协议适配，不直接构造 Domain 对象、访问 Repository 或打开事务。统一执行顺序如下：

```cpp
void TransferController::create_transfer(const HttpRequestPtr& request,
                                         ResponseCallback callback) {
    const auto user_id = request_context.require_user_id();
    auto command = transfer_request_parser.parse_create(request, user_id);
    if (!command) {
        return callback(error_mapper.to_response(command.error()));
    }

    // command 的字段与 pfh::application::CreateTransferCommand 一致：
    // source_account_id, target_account_id, mode, outgoing_amount,
    // incoming_amount, rate, description, occurred_at，以及 P1-S10 接入的 fee 字段。
    auto result = create_transfer_use_case.execute(*command);
    if (!result) {
        return callback(error_mapper.to_response(result.error()));
    }

    callback(transfer_response_mapper.created(*result));
}
```

强制约束：

- `user_id` 只来自通过 JwtFilter 校验的请求上下文，不接受客户端 JSON 或 Query 参数。
- parser 在调用 Use Case 前验证 JSON 类型、枚举、RFC 3339 时间和十进制字符串；RFC 3339 解析器在缩窄为 `system_clock::time_point` 前检查平台可表示范围；Controller 不创建 `Money` 或猜测币种。
- mapper 负责把 Application DTO 的内部带符号金额转换为 1.3 节规定的 REST 业务口径。
- 所有错误统一经过 error mapper；Controller 不返回底层异常的 `what()` 文本。
- Drogon 路由/Controller 由 composition root 注入依赖，不使用隐藏的全局 Repository 或 DbClient。

---

## 6. 身份认证与安全规约 (Authentication & Security)

### 6.1 JWT Access Token 规范

**有效期**

- 推荐：15 分钟
- 短期有效期降低 token 泄漏风险，配合 Refresh Token 机制保证用户体验。

**Payload 结构**

JWT Payload 必须控制精简，**严禁**放入邮箱、手机号、密码哈希等敏感信息。

推荐结构：

```json
{
  "iss": "pfh-api",
  "aud": "pfh-client",
  "sub": "userId",
  "sid": "sessionId",
  "jti": "unique-token-id",
  "roles": ["USER"],
  "iat": 1710000000,
  "nbf": 1710000000,
  "exp": 1710000900
}
```

字段语义：

- `iss` (Issuer): 签发方，固定为 `pfh-api`
- `aud` (Audience): 受众方，固定为 `pfh-client`
- `sub` (Subject): 用户 ID
- `sid` (Session ID): 登录会话标识，用于会话撤销
- `jti` (JWT ID): 单个 token 的唯一 ID，用于黑名单撤销
- `roles`: 服务端持久化角色数组，当前必须且只能包含 `USER` 或 `OPERATOR`
- `iat` (Issued At): 签发时间戳
- `nbf` (Not Before): 生效时间戳
- `exp` (Expiration): 过期时间戳

**签名算法**

- 如果使用对称加密（HMAC），必须采用 HS256 或更高强度算法。
- 密钥必须足够长且随机（至少 256 bit）。
- 更正式的方案推荐使用非对称算法（RS256 / EdDSA），服务端私钥签发，网关或资源服务用公钥验证。

---

### 6.2 Refresh Token 机制

**PFH 必须采用 Refresh Token 机制，不得使用单 Token 方案。**

单 Token 方案在登出和泄漏控制上都比较弱。当前阶段不引入 Redis 强依赖，因此会话撤销、Refresh Token 轮换和 Access Token `jti` 撤销优先落在 PostgreSQL 表中；Redis 仅作为未来可选加速层。

**Token 格式**

- Refresh Token 推荐使用**不透明随机字符串**，不要用 JWT。
- 长度至少 32 字节，使用密码学安全的随机生成器（如 `/dev/urandom`、`std::random_device`）。

**有效期**

- 普通登录：7-30 天
- 可按"记住我"选项调整为更长时间（如 90 天）

**存储规则**

- 服务端**只存 Refresh Token 的哈希值**，不存明文。
- 推荐使用 SHA256 或更强的哈希算法。
- 数据库表结构示例：

```sql
CREATE TABLE refresh_tokens (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    token_hash VARCHAR(64) NOT NULL UNIQUE,
    session_id VARCHAR(64) NOT NULL,
    expires_at TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    revoked_at TIMESTAMPTZ
);

CREATE INDEX idx_refresh_tokens_user_session ON refresh_tokens(user_id, session_id);
CREATE INDEX idx_refresh_tokens_expires ON refresh_tokens(expires_at);
```

**Refresh Token 轮换 (Rotation)**

每次刷新时执行 Refresh Token 轮换，增强安全性：

1. 客户端提交旧 refresh token
2. 服务端验证旧 token 有效且未作废
3. 作废旧 refresh token（设置 `revoked_at`）
4. 签发新 refresh token 和新 access token
5. 返回新 token 对给客户端

**泄漏检测**

如果检测到已作废的 refresh token 被再次使用，说明可能发生了 token 泄漏：

- 立即撤销整个 `sid` 或 token family
- 记录安全事件到 `audit_logs`
- 可选：通知用户异常登录

---

### 6.3 Token 撤销与黑名单机制

**Access Token 黑名单**

登出时：

1. 删除或标记该 `sid` 对应的 refresh token 记录
2. 将当前 access token 的 `jti` 加入 `revoked_access_tokens` 表
3. `expires_at` 设置为 access token 的过期时间，后续由清理任务删除过期记录

当前阶段 PostgreSQL 表结构：

```sql
CREATE TABLE revoked_access_tokens (
    issuer VARCHAR(64) NOT NULL,
    jti VARCHAR(128) NOT NULL,
    session_id VARCHAR(64),
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (issuer, jti)
);

CREATE INDEX idx_revoked_access_tokens_expires ON revoked_access_tokens(expires_at);
```

**注意事项**

- 不建议用原始 JWT 或 JWT hash 当黑名单标识，直接用 `iss + jti` 更清晰。
- 每次验证 JWT 时，必须先检查 `jti` 是否在黑名单中。
- 过期黑名单记录由 `DataCleanupJob` 定期清理。
- 未来引入 Redis 后，可把 `jwt:deny:<iss>:<jti>` 作为加速缓存，但 PostgreSQL 仍保留为可恢复事实来源。

**Session 撤销**

撤销整个 `sid` 时：

```sql
CREATE TABLE revoked_sessions (
    session_id VARCHAR(64) PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    expires_at TIMESTAMPTZ NOT NULL,
    revoked_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    reason VARCHAR(64) NOT NULL
);
```

所有包含该 `sid` 的 access token 均视为无效，即使未单独加入黑名单。

已撤销 refresh token 不物理删除，必须保留到过期以检测旧 token 复用。检测到复用后，同一事务更新该 `sid` 的全部 refresh token、写 `revoked_sessions`、同步安全审计和 outbox；安全处置提交后再向调用方返回统一 401。

---

### 6.4 JwtFilter 规则

**放行路径**

以下接口无需 JWT 验证：

- 登录：`POST /api/v1/auth/login`
- 注册：`POST /api/v1/auth/register`
- 刷新 token：`POST /api/v1/auth/refresh`
- 公开资源（如货币元数据）：`GET /api/v1/currencies`

**验证流程**

其他接口统一要求：

```http
Authorization: Bearer <access_token>
```

验证步骤：

1. 提取 Bearer token
2. 验证签名
3. 检查 `exp` 是否过期
4. 检查 `nbf` 是否生效
5. 验证 `iss` 和 `aud` 是否匹配
6. 检查 `jti` 是否在黑名单表或可选 Redis 缓存中
7. 检查 `sid` 是否被撤销
8. 从用户持久化事实加载当前角色，并与 JWT 唯一 role 比较；不一致时拒绝旧 Token
9. 将 `sub` (userId) 与当前 role 注入请求上下文

**校验失败统一返回**

状态码：`401 Unauthorized`

响应体：

```json
{
  "error_code": "UNAUTHORIZED",
  "message": "Invalid or expired access token",
  "trace_id": "req-20260625-0001"
}
```

**安全原则**

- 不要把 token 解析失败的具体原因暴露得太细，比如"签名错误""密钥不匹配"。
- 统一使用 `Invalid or expired access token` 等模糊描述。
- 避免泄露签名算法、密钥位置、解析器版本等内部信息。
- 公共注册只能创建 USER。角色调整后旧 Access Token 立即因服务端角色不一致失效，下一次登录或 refresh 签发当前角色。

---

### 6.5 密钥配置

**环境变量注入**

JWT Secret / 私钥**绝对不得**写在代码或普通配置文件中。

必须使用环境变量，例如：

```bash
PFH_JWT_SECRET=<强随机密钥>
PFH_PASSWORD_PEPPER=<可选 pepper>
```

**密钥强度要求**

- 如果使用 HMAC（HS256），密钥必须至少 256 bit（32 字节），且由密码学安全的随机生成器生成。
- 更正式的方案可以用 RS256 / EdDSA：
  - 服务端私钥签发
  - 网关或资源服务用公钥验证
  - 私钥妥善保管，定期轮换

**密钥轮换**

- 支持多版本密钥共存，验证时尝试所有活跃密钥。
- 签发时使用最新密钥。
- 逐步淘汰旧密钥，确保所有使用旧密钥签发的 token 过期后再彻底移除。

---

### 6.6 认证接口设计

**注册**

```http
POST /api/v1/auth/register
```

请求：

```json
{
  "username": "user@example.com",
  "password": "plaintext-password"
}
```

响应（201 Created）返回用户标识和初始 token pair。服务端必须完成用户名规范化与唯一性校验、密码哈希、默认 `UserPreference` 创建，并在同一事务写入必要审计/outbox 事实；响应不得返回密码哈希或内部密钥材料。

**登录**

```http
POST /api/v1/auth/login
```

请求：

```json
{
  "username": "user@example.com",
  "password": "plaintext-password"
}
```

响应（200 OK）：

```json
{
  "accessToken": "eyJhbGc...",
  "refreshToken": "random-opaque-string",
  "expiresIn": 900,
  "tokenType": "Bearer"
}
```

**刷新 Token**

```http
POST /api/v1/auth/refresh
```

请求：

```json
{
  "refreshToken": "random-opaque-string"
}
```

响应（200 OK）：

```json
{
  "accessToken": "eyJhbGc...",
  "refreshToken": "new-random-opaque-string",
  "expiresIn": 900,
  "tokenType": "Bearer"
}
```

**登出**

```http
POST /api/v1/auth/logout
```

请求头：

```http
Authorization: Bearer <access_token>
```

请求体：

```json
{
  "refreshToken": "random-opaque-string"
}
```

响应：`204 No Content`

服务端操作：

1. 验证 access token，提取 `jti` 和 `sid`
2. 将 `jti` 加入 `revoked_access_tokens`
3. 作废数据库中对应的 refresh token
4. 写入审计日志

### 6.7 Web Cookie 会话

Web 专用端点位于 `/api/v1/web/auth/*`，与 JSON Token 端点并存：

- `register`、`login` 返回内存使用的 `accessToken`，响应不包含 `refreshToken`。
- Refresh Token 通过 `pfh_refresh` Cookie 返回，固定 `Path=/api/v1/web/auth`，并设置 `HttpOnly`、`Secure`、`SameSite=Strict`、`Cache-Control: no-store`。
- `refresh` 只读取 Cookie，成功后轮换 Cookie；失败时清除 Cookie 并返回统一认证错误。
- `logout` 同时要求当前 Access Token 和 Cookie，撤销会话并清除 Cookie。
- 所有 Web Cookie 写端点要求 `Origin` 与 Web Edge 报告的外部 scheme 和 `Host` 精确同源；生产环境的外部 scheme 必须为 HTTPS。存在 `Sec-Fetch-Site` 时必须为 `same-origin`。
- 浏览器不得把 Access Token 或 Refresh Token 写入 `localStorage`、`sessionStorage`、IndexedDB 或日志。

---

## 7. 安全与防御性边界提示

1. **大数损失防御**：在 JSON 反序列化时，**严禁**允许前端将金额作为数字类型传输（如 `"amount": 45.12` 会因 JsonCpp 内部转为 double 导致二进制精度丢失）。所有金额输入和输出，在 JSON 中**必须映射为纯字符串**（如 `"amount": "45.12"`）。
2. **越权防御**：在 `CreateTransferUseCase` 中，读取账户后，必须在应用层强校验 `account.getUserId() == currentUserId`。严禁仅凭前端传入的 `sourceAccountId` 就盲目扣款。
3. **频率限制预留**：对于创建流水和转账接口，后续可在路由宏上挂载 `RateLimiterFilter`，防止前端异常重试导致的表爆满。
4. **密钥隔离**：所有敏感密钥（JWT Secret、Password Pepper、数据库密码）必须通过环境变量注入，绝不写入代码或配置文件。
5. **Token 泄漏防御**：采用 Refresh Token 轮换机制，检测到旧 token 重用时立即撤销整个会话。
6. **黑名单自动清理**：`revoked_access_tokens` 必须按 `expires_at` 定期清理；未来 Redis 缓存 key 必须设置 TTL，避免内存泄漏。
