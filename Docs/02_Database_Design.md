# Personal Finance Hub - Database Design

Version: 1.0  
Backend: C++23  
Presentation: Drogon  
Database: PostgreSQL 16+

---

## 1. Database Design Philosophy

Core Principles:

1. Transaction is the Source of Truth
2. Balance is reconstructible derived data
3. Native multi-currency support
4. Historical exchange rates permanently preserved
5. All statistics can be recalculated
6. Reserve extensibility for future platform sync
7. Never use float/double for amounts

All amounts use:

NUMERIC(20,8)

Example:

999999999999.12345678

---

## 2. ER Model

User
├── Account
│ ├── Transaction
│ └── BalanceCache
│
├── Category
│
├── Currency
│
├── ExchangeRate
│
└── AuditLog

Reserved for Future:

ExternalAccount
ExternalTransaction
SyncJob

---

## 3. Users Table

System users

```sql
CREATE TABLE users (
id BIGSERIAL PRIMARY KEY,
username VARCHAR(64) NOT NULL UNIQUE,
password_hash VARCHAR(255) NOT NULL,
base_currency_code CHAR(3) NOT NULL REFERENCES currencies(code),
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

`base_currency_code` 用于保存用户全局报表和净值统计的默认基准货币。
当前阶段保留该字段作为强依赖默认值；扩展偏好落在 `user_preferences` 表。
该字段表示用户偏好，不等同于 `exchange_rates.base_currency_code` 的汇率方向基准货币。

Indexes:

```sql
UNIQUE(username)
```

### User Preferences Table

扩展偏好单独存储，避免继续膨胀 `users` 表。
Repository 对外仍映射为 Domain 的 `UserPreference`。

```sql
CREATE TYPE theme_mode AS ENUM (
    'system',
    'light',
    'dark'
);

CREATE TYPE default_home_page AS ENUM (
    'dashboard',
    'transactions',
    'reports',
    'accounts'
);

CREATE TYPE report_period AS ENUM (
    'current_month',
    'last_month',
    'last_3_months',
    'current_year',
    'custom'
);

CREATE TABLE user_preferences (
user_id BIGINT PRIMARY KEY REFERENCES users(id),
base_currency_code CHAR(3) NOT NULL REFERENCES currencies(code),
locale VARCHAR(16) NOT NULL DEFAULT 'zh-CN',
timezone VARCHAR(64) NOT NULL DEFAULT 'Asia/Shanghai',
date_format VARCHAR(32) NOT NULL DEFAULT 'YYYY-MM-DD',
number_format VARCHAR(32) NOT NULL DEFAULT '1,234.56',
theme theme_mode NOT NULL DEFAULT 'system',
default_home_page default_home_page NOT NULL DEFAULT 'dashboard',
default_report_period report_period NOT NULL DEFAULT 'current_month',
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

规则：

1. `users.base_currency_code` 是基础默认值，`user_preferences.base_currency_code` 是扩展偏好值
2. 如果 `user_preferences` 缺失，Repository 必须回退到 `users.base_currency_code`
3. 报表、Dashboard、净值统计优先读取 `UserPreference.baseCurrency`

---

## 4. Accounts Table

Asset containers

### PostgreSQL Types

固定集合使用 PostgreSQL ENUM。
如果迁移阶段暂不使用 ENUM，必须至少使用等价的 `CHECK` 约束。

```sql
CREATE TYPE account_type AS ENUM (
    'cash',
    'savings',
    'credit',
    'digital_wallet',
    'investment',
    'crypto',
    'other'
);

CREATE TYPE account_category AS ENUM (
    'asset',
    'liability'
);

CREATE TYPE transaction_type AS ENUM (
    'income',
    'expense',
    'transfer',
    'adjustment'
);
```

Supported Types:

- Cash
- Savings
- Credit
- DigitalWallet
- Investment
- Crypto
- Other

```sql
CREATE TABLE accounts (
id BIGSERIAL PRIMARY KEY,
user_id BIGINT NOT NULL REFERENCES users(id),
name VARCHAR(128) NOT NULL,
type account_type NOT NULL,
subtype VARCHAR(64) NOT NULL,
category account_category NOT NULL,
currency_code CHAR(3) NOT NULL REFERENCES currencies(code),
description TEXT,
is_archived BOOLEAN NOT NULL DEFAULT FALSE,
archived_at TIMESTAMPTZ,
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

Indexes:

```sql
(user_id)
(user_id, type)
(user_id, subtype)
(user_id, category)
```

### Account Type Behavior

`type` 是账户大类，`subtype` 是用户可自定义的具体账户类型。
`银行卡`、`支付宝`、`微信支付`、`信用卡` 这类词应写入 `subtype`，不能替代账户名称。
`name` 必须是用户可识别的账户实例名，例如 `招商银行储蓄卡`、`招行信用卡 2026`。

默认子分类：

| type | 默认 subtype 示例 | 默认 category |
| --- | --- | --- |
| cash | 现金、零钱包、备用金 | asset |
| savings | 银行卡、借记卡、活期账户、定期账户 | asset |
| credit | 信用卡、花呗、京东白条、借贷账户 | liability |
| digital_wallet | 支付宝、微信支付、PayPal、Apple Pay | asset |
| investment | 股票账户、基金账户、券商账户、养老金账户 | asset |
| crypto | 比特币、以太坊、交易所账户、硬件钱包 | asset |
| other | 自定义 | asset |

余额允许为负数。
总资产/净值统计按所有账户余额折算到用户基准货币后直接求和，负数余额自然抵减总额。
`category` 主要用于分组展示和负债分析，不用于决定账户是否参与总额计算。

### Account Deletion Strategy

默认归档

支持危险删除 (Dangerous Delete)

危险删除会同时删除：

- Account
- Related Transactions
- Balance Snapshots
- Statistics Cache

需要三次用户确认

---

## Foreign Key Delete Strategy

### Users

- 禁止删除

### Categories

- 软删除 `deleted_at`

### Transactions

- 软删除 `deleted_at`

### Accounts

- 默认归档 `is_archived`

### Dangerous Delete

- 允许但必须通过业务层执行
- 数据库不允许自动级联删除（不要对 `transactions` 使用 `ON DELETE CASCADE`）

应用层必须把危险删除实现为完整 Use Case，并在一个数据库事务中按顺序执行：

1. 软删除/硬删除该账户下的所有 `transactions`
2. 清除该账户在 `account_balance_cache` 中的记录
3. 软删除/硬删除 `accounts` 记录
4. 写入 `audit_logs`
5. 提交事务

---

## 5. Categories Table

Support unlimited hierarchical categories.

分类系统由两类数据组成：

1. `system_category_templates`: 系统可选分类池，只用于初始化和检索
2. `categories`: 用户实际启用在收入/支出板块的分类树，可编辑、可删除

系统模板不会直接被交易引用。
用户选择预设分类时，系统复制模板为该用户自己的 `categories` 记录，并保留 `source = 'system'` 和 `template_id`。

Example Structure:

```text
Food
├─ Coffee
├─ Restaurant

Transport
├─ Taxi
├─ Fuel
```

```sql
CREATE TYPE category_source AS ENUM (
    'system',
    'user'
);

CREATE TYPE category_board AS ENUM (
    'income',
    'expense'
);

CREATE TABLE system_category_templates (
id BIGSERIAL PRIMARY KEY,
name VARCHAR(128) NOT NULL,
group_name VARCHAR(64) NOT NULL,
parent_id BIGINT REFERENCES system_category_templates(id),
default_board category_board,
sort_order INT NOT NULL DEFAULT 0,
is_selectable BOOLEAN NOT NULL DEFAULT TRUE,
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
UNIQUE NULLS NOT DISTINCT (group_name, parent_id, name)
);

CREATE TABLE categories (
id BIGSERIAL PRIMARY KEY,
user_id BIGINT NOT NULL REFERENCES users(id),
name VARCHAR(128) NOT NULL,
parent_id BIGINT REFERENCES categories(id),
board category_board NOT NULL,
source category_source NOT NULL DEFAULT 'user',
template_id BIGINT REFERENCES system_category_templates(id),
sort_order INT NOT NULL DEFAULT 0,
deleted_at TIMESTAMPTZ,
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
UNIQUE NULLS NOT DISTINCT (user_id, board, parent_id, name)
);
```

Indexes:

```sql
(user_id)
(parent_id)
(user_id, board)
(template_id)
```

### Default Category Rules

可选分类池大类：

```text
餐饮、日常、财务、居家、服饰、美妆、数码、办公、社交、医疗、交通、运动、娱乐、教育、旅行、宠物、家庭、汽车、校园、人情、其他
```

大类可仅作为检索维度，`is_selectable = false` 时不直接加入用户分类树。
支出板块默认一级分类：

```text
餐饮、日常、财务、居家、服饰、美妆、数码、办公、社交、医疗、交通、运动、娱乐、教育、旅行、宠物、家庭、汽车、校园、人情、其他
```

收入板块默认一级分类：

```text
工资、奖金、投资、兼职、副业、红包
```

预设二级分类只在必要时创建，例如：

| board | 一级分类 | 二级分类示例 |
| --- | --- | --- |
| expense | 餐饮 | 早餐、午餐、晚餐、咖啡、外卖、聚餐 |
| expense | 日常 | 水费、电费、燃气费、物业费、生活用品 |
| expense | 交通 | 地铁、公交、打车、加油、停车 |
| expense | 财务 | 手续费、利息支出、汇兑损耗 |
| income | 工资 | 基本工资、绩效、补贴 |
| income | 投资 | 股息、基金收益、利息、卖出收益 |
| income | 红包 | 亲友红包、平台红包 |

规则：

1. 用户注册后，系统应初始化收入和支出板块的默认一级分类
2. 用户可以从可选分类池继续添加分类到收入或支出板块
3. 用户可以把可选分类里的大类、一级分类或二级分类名称作为板块一级分类
4. 用户可在任意一级分类下继续创建二级或更深层级分类
5. 用户可以删除预设分类，删除仅影响自己的 `categories`
6. `TransactionType::Income` 只能使用 `board = 'income'` 分类
7. `TransactionType::Expense` 和费用类 `adjustment` 使用 `board = 'expense'` 分类
8. `transfer` 默认不要求分类

---

## 6. Transactions Table

Core table

```sql
CREATE TABLE transactions (
id BIGSERIAL PRIMARY KEY,

    user_id BIGINT NOT NULL REFERENCES users(id),

    account_id BIGINT NOT NULL REFERENCES accounts(id),

    category_id BIGINT REFERENCES categories(id),

    type transaction_type NOT NULL,

    amount NUMERIC(20,8) NOT NULL,

    currency_code CHAR(3) NOT NULL REFERENCES currencies(code),

    description TEXT,

    transfer_group_id UUID REFERENCES transfer_groups(id),

    deleted_at TIMESTAMPTZ,

    transaction_time TIMESTAMPTZ NOT NULL,

    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),

    version BIGINT NOT NULL DEFAULT 1
);
```

Supported Types:

- income
- expense
- transfer
- adjustment

转账产生的出账流水和入账流水必须使用 `transfer`。
不得复用 `income` 或 `expense` 表示转账两端。

手续费、汇兑损耗等转账调整项必须作为独立 `adjustment` 流水，或归入明确的 `expense` 类别，例如“金融手续费”。

Indexes:

```sql
(account_id)
(category_id)
(transaction_time)
(user_id, transaction_time)
```

---

## 7. Transfer Model

Dual-flow model implementation

Example:

Bank A -> Bank B generates two transactions:

Outgoing Transaction:

- type: transfer
- account: Bank A
- amount: 100

Incoming Transaction:

- type: transfer
- account: Bank B
- amount: 100

Bind using:

transfer_group_id (UUID) REFERENCES transfer_groups(id)

转账两端都必须使用 `transfer`。
不得用 `expense` 表示出账端，也不得用 `income` 表示入账端。

转账金额变化、汇率差、手续费差异必须通过：

- 独立 `adjustment` 流水
- 或明确的费用流水，例如 `expense` + “金融手续费”类别

表达。

关联

优势：

- 不需要特殊转账表
- 余额计算统一
- 容易审计
- 容易对账

---

## 8. 交易标签表

支持标签系统

示例：

travel
business
vacation

```sql
CREATE TABLE transaction_tags (
id BIGSERIAL PRIMARY KEY,
user_id BIGINT NOT NULL REFERENCES users(id),
name VARCHAR(64) NOT NULL,
deleted_at TIMESTAMPTZ,
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
UNIQUE(user_id, name)
);

CREATE TABLE transaction_tag_relations (
transaction_id BIGINT NOT NULL REFERENCES transactions(id),
tag_id BIGINT NOT NULL REFERENCES transaction_tags(id),
PRIMARY KEY(transaction_id, tag_id)
);
```

规则：

1. Tag 属于用户，不能跨用户复用
2. Tag 不参与余额计算
3. Tag 删除使用软删除，历史流水关系保留
4. Tag 用于检索、过滤、报表扩展和同步导入标记

---

## 9. 账户余额缓存表

余额缓存

不是事实来源

CREATE TABLE account_balance_cache (
account_id BIGINT PRIMARY KEY REFERENCES accounts(id),
balance NUMERIC(20,8) NOT NULL,
last_transaction_id BIGINT REFERENCES transactions(id),
source_version BIGINT NOT NULL DEFAULT 0,
cache_version BIGINT NOT NULL DEFAULT 1,
updated_at TIMESTAMPTZ NOT NULL
);

重建逻辑：

transactions
-> aggregate
-> balance_cache

Repository 缓存策略：

1. 先查询 `account_balance_cache`
2. 对比 `source_version` 和该账户流水最新版本/最新流水
3. 命中且有效则直接返回
4. 未命中或失效则执行 `SELECT SUM(...)` 聚合该账户流水
5. 更新 `account_balance_cache`
6. 返回 `BalanceSnapshot` Read Model

Domain 只通过 Repository 获取 `BalanceSnapshot`，不直接感知缓存命中、失效或重建细节。

---

## 10. 货币表

```sql
CREATE TABLE currencies (
code CHAR(3) PRIMARY KEY,
name VARCHAR(64) NOT NULL,
display_name VARCHAR(64) NOT NULL,
symbol VARCHAR(16) NOT NULL,
precision SMALLINT NOT NULL DEFAULT 2,
is_crypto BOOLEAN NOT NULL DEFAULT FALSE,
is_enabled BOOLEAN NOT NULL DEFAULT TRUE,
updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
CHECK (precision >= 0 AND precision <= 12)
);
```

示例：

| code | symbol | precision | display_name | is_crypto |
| --- | --- | --- | --- | --- |
| USD | $ | 2 | US Dollar | false |
| CNY | ¥ | 2 | Chinese Yuan | false |
| EUR | € | 2 | Euro | false |
| JPY | ¥ | 0 | Japanese Yen | false |
| BTC | ₿ | 8 | Bitcoin | true |

规则：

1. `code` 是计算和外键引用使用的稳定标识
2. `display_name`、`symbol`、`precision` 属于 CurrencyMetadata，用于展示和格式化
3. `precision` 是默认展示精度，不改变 `NUMERIC(20,8)` 或汇率存储精度
4. 加密货币允许进入受控白名单，但仍必须在 `currencies` 中登记
5. `is_enabled = false` 表示不再允许新建账户或交易，但历史数据仍可展示

---

## 10.5 transfer_groups

```sql
CREATE TABLE transfer_groups (
    id UUID PRIMARY KEY,
    created_at TIMESTAMPTZ NOT NULL,
    note TEXT,
    transfer_mode SMALLINT NOT NULL,
    exchange_rate DECIMAL(30,10),
    exchange_rate_provider VARCHAR(64),
    exchange_rate_snapshot_time TIMESTAMPTZ
);
```

transfer_mode:

- 1 = Outgoing + Rate
- 2 = Outgoing + Incoming
- 3 = Incoming + Rate

TransferAggregate 是领域层的转账聚合，TransferGroup 是持久化/元数据承载体，用来存模式、汇率、快照时间等。
TransferGroup 不是业务实体，也不是 Domain Entity；不要在领域层建 `TransferGroup Entity`。

---

## 11. 汇率表

历史汇率

```sql
CREATE TABLE exchange_rates (
id BIGSERIAL PRIMARY KEY,

    base_currency_code CHAR(3) NOT NULL REFERENCES currencies(code),

    target_currency_code CHAR(3) NOT NULL REFERENCES currencies(code),

    rate NUMERIC(20,10) NOT NULL,

    source VARCHAR(64) NOT NULL,

    fetched_at TIMESTAMPTZ NOT NULL

);
```

**汇率历史策略：**

- Append Only
- 禁止 UPDATE
- 仅允许 INSERT

## Exchange Rate Storage Policy

- Append Only
- INSERT only
- Never UPDATE historical rates
- Never DELETE historical rates

索引：

(base_currency_code, target_currency_code)

(base_currency_code, target_currency_code, fetched_at DESC)

历史查询规则：

给定目标时间 `target_time` 时，使用小于等于目标时间的最新一条：

```sql
SELECT *
FROM exchange_rates
WHERE base_currency_code = $1
  AND target_currency_code = $2
  AND fetched_at <= $3
ORDER BY fetched_at DESC
LIMIT 1;
```

不能简单读取最新插入行；历史回放必须按目标时间选择汇率。

---

## 12. 审计日志表

关键操作审计

```sql
CREATE TYPE audit_action AS ENUM (
    'create',
    'update',
    'archive',
    'delete',
    'dangerous_delete',
    'sync_import',
    'refresh'
);

CREATE TABLE audit_logs (
id BIGSERIAL PRIMARY KEY,
operator_user_id BIGINT NOT NULL REFERENCES users(id),
action audit_action NOT NULL,
resource_type VARCHAR(64) NOT NULL,
resource_id VARCHAR(128) NOT NULL,
before_value JSONB,
after_value JSONB,
metadata JSONB,
occurred_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

Indexes:

```sql
(operator_user_id, occurred_at DESC)
(resource_type, resource_id)
(action, occurred_at DESC)
```

必须记录：

- 危险删除账户
- 账户归档和修改
- 同步导入、重复导入跳过、同步失败摘要
- 汇率刷新
- 用户偏好修改
- 分类批量初始化、删除或重命名

字段语义：

- `operator_user_id`: 执行人
- `action`: 操作类型
- `resource_type`: 资源类型，例如 `Account`、`Transaction`、`Category`、`ExchangeRate`
- `resource_id`: 资源 ID，使用字符串以兼容 BIGINT、UUID、外部 ID 和复合 ID
- `before_value`: 修改前快照
- `after_value`: 修改后快照
- `metadata`: 请求来源、Provider 名称、IP、User-Agent、失败原因等附加信息

---

# 13. 汇率快照策略

禁止覆盖旧汇率

错误：

USD-CNY
7.18

更新后：

USD-CNY
7.19

覆盖旧记录

正确：

新增一条记录

这样可以查询：

2025-01-01汇率

2026-01-01汇率

用于历史报表

---

# 14. 报表查询优化

主要查询：

月支出统计

```sql
SELECT
SUM(amount)
FROM transactions
WHERE type = 'expense'
  AND type <> 'transfer'
```

月收入统计

```sql
SELECT
SUM(amount)
FROM transactions
WHERE type = 'income'
  AND type <> 'transfer'
```

报表统计必须显式排除 `type = 'transfer'`，避免账户间转账造成收入或支出虚增。

分类统计

```sql
SELECT
category_id,
SUM(amount)
GROUP BY category_id
```

资产趋势

```sql
SELECT
DATE(transaction_time)
GROUP BY date
```

---

# 15. 推荐索引

transactions:

(account_id)

(user_id)

(category_id)

(transaction_time)

(user_id, transaction_time)

(user_id, category_id)

exchange_rates:

(base_currency_code,target_currency_code)

(base_currency_code,target_currency_code,fetched_at DESC)

accounts:

(user_id)

(user_id,type)

---

# 16. 缓存边界

当前阶段不引入 Redis 强依赖。

账户余额缓存使用 PostgreSQL `account_balance_cache` 表。

Redis 仅作为未来基础设施级扩展，可用于：

- Dashboard Summary
- Monthly Report
- Latest Exchange Rates

Redis 缓存不得成为事实来源，所有缓存内容必须能由 PostgreSQL 重新计算。

未来 Key 示例：

balance:{account_id}

summary:{user_id}

rate:USD:CNY

---

# 17. 分区表预留

未来数据量增长：

transactions

可按月分区

示例：

transactions_2026_01

transactions_2026_02

transactions_2026_03

优点：

- 查询快
- 清理方便
- 历史归档方便

---

# 18. 同步模块预留

external_accounts

CREATE TABLE external_accounts (
id BIGSERIAL PRIMARY KEY,
user_id BIGINT NOT NULL REFERENCES users(id),
provider VARCHAR(64) NOT NULL,
external_id VARCHAR(255) NOT NULL,
account_id BIGINT REFERENCES accounts(id),
UNIQUE(provider, external_id)
);

支持：

Bank
Alipay
WeChat
Broker
Crypto

---

# 19. external_transactions

CREATE TABLE external_transactions (
id BIGSERIAL PRIMARY KEY,
provider VARCHAR(64) NOT NULL,
external_transaction_id VARCHAR(255) NOT NULL,
transaction_id BIGINT REFERENCES transactions(id),
imported_at TIMESTAMPTZ NOT NULL
);

唯一索引：

(provider, external_transaction_id)

目的：

防止重复导入

实现幂等

---

# 20. sync_jobs

CREATE TABLE sync_jobs (
id BIGSERIAL PRIMARY KEY,
provider VARCHAR(64) NOT NULL,
status VARCHAR(20) NOT NULL CHECK(status IN ('pending', 'running', 'success', 'failed')),
started_at TIMESTAMPTZ,
finished_at TIMESTAMPTZ,
message TEXT
);

状态：

pending
running
success
failed

---

## 21. C++23 实体对应

User
Account
Category
Transaction
Tag
Currency
ExchangeRate
AuditLog

Repository:

UserRepository
AccountRepository
TransactionRepository
ExchangeRateRepository

Application Use Cases:

CreateTransactionUseCase
CreateTransferUseCase
DeleteTransactionUseCase
GenerateMonthlyReportUseCase
RefreshExchangeRateUseCase

Domain Services:

TransferDomainService
CurrencyConversionService
BalanceCalculationService

---

## 22. 最终原则

1. Transaction 永远是真实数据

2. Balance Cache 随时可重建

3. 汇率必须保留历史

4. 报表不存储结果

5. 外部同步必须幂等

6. 数据库结构无需为了第三阶段重构

7. 所有统计都能从流水重新计算

```

```
