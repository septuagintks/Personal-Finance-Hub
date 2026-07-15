# Personal Finance Hub 汇率系统设计

Version: 2.0
Backend: C++23
Status: Approved

---

## 1. 目标与原则

汇率系统为多币种余额、报表和转账提供可追溯的精确折算。核心原则：

1. 汇率快照 append-only，不覆盖历史。
2. 数据库存储 `USD -> target`，交叉汇率在 Domain 内推导。
3. 汇率不经 `float`、`double` 或 JSON number。
4. Domain Service 只计算，不访问 Repository 或外部 API。
5. 外部批次必须完整成功，不写部分结果。
6. 缺失汇率明确返回不可用，不使用默认值。

---

## 2. 分层职责

### 2.1 Domain

`ExchangeRate` 包含：

- base currency。
- target currency。
- 正数 `Decimal` rate。
- `fetched_at`。
- 实际 source。

`CurrencyConversionService` 只执行：

- 直接汇率。
- 反向汇率。
- USD 枢纽三角折算。

它不注入 `IExchangeRateRepository`，不读取系统时间，不负责降级。

### 2.2 Application

`RefreshExchangeRatesUseCase` 负责：

1. 通过 `IActiveCurrencyQuery` 获取活动账户币种和用户基准币种。
2. 加入 USD、排序、去重并构造完整目标批次。
3. 调用 `IExchangeRateProvider`。
4. 在 Unit of Work 中 append 全部快照和 Outbox 事件。
5. 失败时检查每个目标币种对的历史快照。
6. 发布 `ExchangeRateRefreshFailed`，记录 Provider identity 与 `historicalAvailable`。

只有全部目标币种对均有历史快照时，`historicalAvailable` 才为 `true`。Repository 异常与无历史记录必须区分。

### 2.3 Infrastructure

Infrastructure 实现：

- `FreeCurrencyApiProvider`。
- `ExchangeRateFunProvider`。
- `FailoverExchangeRateProvider`。
- `CurlHttpTransport`。
- PostgreSQL `ExchangeRateRepositoryImpl`。
- 活跃币种查询与 Scheduler 接线。

---

## 3. 外部 Provider

### 3.1 主备顺序

- 主源：FreeCurrencyAPI。
- 备用源：exchangerate.fun。
- 组合 Provider identity：`FreeCurrencyAPI/exchangerate.fun`。

主源出现 transport、timeout、HTTP 非 200、JSON 非法、字段非法、集合不完整或数值越界时，原请求批次整体切换备用源。一个成功批次不允许混用来源。

### 3.2 FreeCurrencyAPI

- Endpoint：`/v1/latest`。
- Key：`PFH_FREECURRENCYAPI_API_KEY`。
- Base：USD。
- 响应 `data` 必须与请求目标集合精确一致。
- API 不返回抓取时间，使用注入的 `IClock::now()`。

### 3.3 exchangerate.fun

- Endpoint：`/latest`。
- 不需要 key。
- `base` 必须是 USD。
- `timestamp` 必须是正整数且不晚于当前时钟五分钟。
- Provider 可能返回全量 superset；适配器只提取请求目标，但所有目标都必须存在。

### 3.4 HTTP 安全

`CurlHttpTransport` 强制：

- HTTPS-only。
- peer 与 host 证书验证。
- 禁止 redirect。
- connect/total hard timeout。
- 1 MiB 响应上限和 8 KiB URL 上限。
- Query 参数由 libcurl 转义。
- 错误不包含 key、完整 URL、响应正文或底层异常。

两个 Provider 串行执行，因此：

```text
2 * request_timeout_seconds <= job_execution_timeout_seconds
```

---

## 4. 数值与响应校验

### 4.1 Numeric Token

Provider 响应使用 nlohmann JSON SAX 解析并保留原始 numeric token。解析回调不使用已转换的 `double` 值。

外部 rate 可包含超过 10 位小数；防腐层显式按 Half-Even 归一到 scale 10，再验证：

- rate > 0。
- `NUMERIC(20,10)` 范围。
- base/target 合法且不同。

用户输入汇率继续执行严格 scale 校验，不复用 Provider 的归一路径。

### 4.2 批次原子性

Provider 返回必须满足：

- 无重复 key。
- 所有请求币种存在。
- 必需字段类型正确。
- timestamp 合法。
- 所有 rate 都可构造 Domain `ExchangeRate`。

任一元素失败，整批拒绝且不写新快照。

---

## 5. 持久化与查询

### 5.1 存储

```text
base_currency_code   USD
target_currency_code requested target
rate                 NUMERIC(20,10)
source               actual provider
fetched_at           provider/clock timestamp
```

成功批次的 source 是 `FreeCurrencyAPI` 或 `exchangerate.fun`，不得写组合 identity。数据库 trigger/Repository 契约阻止历史快照 UPDATE 或 DELETE。

### 5.2 查询

- Latest：按 `fetched_at DESC, id DESC` 选择最新快照。
- Historical：选择 `fetched_at <= target_time` 的最新快照。
- Non-USD pair：读取两个 USD pivot rate 后由 Domain 推导。

报表按流水发生时间查询历史汇率；净资产按注入的当前时刻查询。

---

## 6. Scheduler 与事件

汇率刷新由 PostgreSQL scheduled lease 防止多实例重复执行。Drogon timer 只提交任务到有界 worker pool，HTTPS 和数据库 I/O 不阻塞 Event Loop。

成功时按目标币种产生 `ExchangeRateRefreshed` Outbox 事件；失败时产生一条 `ExchangeRateRefreshFailed`，包含：

- provider identity。
- base currency。
- historical availability。
- 脱敏 reason。
- occurredAt。

Outbox Publisher 在业务提交后领取和投递事件。

---

## 7. 能力边界

PFH Domain 白名单包含 20 种法币和 13 种加密货币。当前 Provider 组合：

- 可为 20 种法币提供实时路径。
- exchangerate.fun 额外覆盖 BTC。
- ETH、USDT、USDC、BNB、XRP、ADA、DOGE、SOL、TRX、MATIC、DOT 和 WBTC 没有实时保证。

请求含未覆盖币种时，系统保持整批语义：不会拆批或混源；完整历史快照存在时可降级，否则返回不可用。完整加密货币实时定价不在当前计划内。

---

## 8. 验收规则

自动化测试必须覆盖：

- 主源完整成功。
- 主源失败后整批备用成功。
- 两个 Provider 均失败。
- 完整与不完整历史降级。
- 重复、缺失、非法类型和非法 timestamp。
- 高精度 token 的 Half-Even 归一。
- source 与失败 identity。
- Scheduler lease、Outbox 发布和无部分写入。
- Error/log 不泄露 key、URL query 或 response body。

最终 Linux production ON 与 Docker runtime 已对真实主源、整批备用和双源失败场景完成验证。
