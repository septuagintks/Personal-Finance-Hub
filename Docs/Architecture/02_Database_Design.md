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
categories_initialized BOOLEAN NOT NULL DEFAULT FALSE,
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

`base_currency_code` 用于保存用户全局报表和净值统计的默认基准货币。
`categories_initialized` 用于标记用户注册后是否已成功初始化默认分类，作为防止重复初始化的第一道防线。
当前阶段保留该字段作为强依赖默认值；扩展偏好落在 `user_preferences` 表。
该字段表示用户偏好，不等同于 `exchange_rates.base_currency_code` 的汇率方向基准货币。

Indexes:

```sql
UNIQUE(username)
```

### 3.1 密码存储规范

`users.password_hash` 必须只存储强哈希结果，绝不允许存储明文、中间结果或弱哈希。

#### 推荐算法

**首选：Argon2id**

- 哈希格式使用 PHC 字符串标准，例如：
  ```
  $argon2id$v=19$m=19456,t=2,p=1$<salt>$<hash>
  ```
- Argon2id 是当前密码哈希的最佳实践，兼顾侧信道防御与内存硬度。
- PHC 格式自带算法版本、参数、salt，便于未来升级和迁移验证。

**备选：bcrypt**

- 如果暂时无法集成 Argon2id 库，可退到 bcrypt。
- 成本因子（cost factor）至少设为 10，更推荐按服务器压测设为 12+。
- bcrypt 已被广泛验证，但抗 ASIC 攻击能力弱于 Argon2id。

#### 严格禁止

- 明文存储
- MD5
- SHA1
- 单次 SHA256（无 salt、无迭代）
- 自研哈希算法

#### Pepper 可选策略

可选择性增加一层全局 pepper（即应用层密钥），用于进一步防御数据库泄漏攻击：

- pepper 从环境变量（如 `PFH_PASSWORD_PEPPER`）或密钥管理服务读取。
- pepper **不入库**、**不进 Git**、**不写入配置文件**。
- pepper 在哈希前与密码拼接，例如：`hash(password + pepper + salt)`。
- 如果未来需轮换 pepper，必须保留旧 pepper 以验证历史密码，或强制用户重置密码。

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

| type           | 默认 subtype 示例                        | 默认 category |
| -------------- | ---------------------------------------- | ------------- |
| cash           | 现金、零钱包、备用金                     | asset         |
| savings        | 银行卡、借记卡、活期账户、定期账户       | asset         |
| credit         | 信用卡、花呗、京东白条、借贷账户         | liability     |
| digital_wallet | 支付宝、微信支付、PayPal、Apple Pay      | asset         |
| investment     | 股票账户、基金账户、券商账户、养老金账户 | asset         |
| crypto         | 比特币、以太坊、交易所账户、硬件钱包     | asset         |
| other          | 自定义                                   | asset         |

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

| board   | 一级分类 | 二级分类示例                         |
| ------- | -------- | ------------------------------------ |
| expense | 餐饮     | 早餐、午餐、晚餐、咖啡、外卖、聚餐   |
| expense | 日常     | 水费、电费、燃气费、物业费、生活用品 |
| expense | 交通     | 地铁、公交、打车、加油、停车         |
| expense | 财务     | 手续费、利息支出、汇兑损耗           |
| income  | 工资     | 基本工资、绩效、补贴                 |
| income  | 投资     | 股息、基金收益、利息、卖出收益       |
| income  | 红包     | 亲友红包、平台红包                   |

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

```sql
CREATE TABLE account_balance_cache (
account_id BIGINT PRIMARY KEY REFERENCES accounts(id),
balance NUMERIC(20,8) NOT NULL,
last_transaction_id BIGINT REFERENCES transactions(id),
source_version BIGINT NOT NULL DEFAULT 0,
cache_version BIGINT NOT NULL DEFAULT 1,
updated_at TIMESTAMPTZ NOT NULL
);
```

### 9.1 并发安全与死锁预防策略

当高并发写入事务（或多笔流水同时并发导入）时，若不加控制地更新 `account_balance_cache`，极易发生**死锁（Deadlock）**或**数据不一致**。

1. **悲观锁防死锁策略**：在涉及余额变动的事务中（如记账、转账、导入），必须在事务开始时对**聚合根（Account）**加锁。
   - **规则 1**：始终通过 `SELECT ... FOR UPDATE` 锁住 `accounts` 表，而不是直接锁缓存表。
   - **规则 2**：跨账户转账时，必须按 `account_id` 升序加锁。例如，账户 `3` 向账户 `1` 转账，加锁顺序必须是：先锁 `1`，再锁 `3`。这能从根本上消除死锁环路。
2. **缓存更新排他锁**：在 Repository 层通过数据库排他性语句更新，确保并发写入下的账目准确。

### 9.2 缓存自愈机制 (Self-Healing)

由于缓存可能因非正常渠道修改而失效，设计一个**双向校验与自愈服务**：

1. **触发时机**：
   - 每日凌晨由 `Scheduler` 触发定时对账任务。
   - 用户在前端点击“重建账户余额”按钮。
   - 系统检测到 `source_version` 异常不连续时。
2. **自愈逻辑**：

   ```sql
   -- 1. 计算真实的流水聚合余额
   SELECT COALESCE(SUM(CASE WHEN type = 'income' THEN amount
                            WHEN type = 'expense' THEN -amount
                            ELSE 0 END), 0) AS real_balance,
          MAX(id) AS max_tx_id,
          COALESCE(MAX(version), 0) AS max_version
   FROM transactions
   WHERE account_id = $1 AND deleted_at IS NULL;

   -- 2. 强制覆盖更新缓存表
   INSERT INTO account_balance_cache (account_id, balance, last_transaction_id, source_version, cache_version, updated_at)
   VALUES ($1, $real_balance, $max_tx_id, $max_version, 1, NOW())
   ON CONFLICT (account_id) DO UPDATE SET
   balance = EXCLUDED.balance,
   last_transaction_id = EXCLUDED.last_transaction_id,
   source_version = EXCLUDED.source_version,
   cache_version = account_balance_cache.cache_version + 1,
   updated_at = NOW();
   ```

### 9.3 Repository 缓存策略：

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
| ---- | ------ | --------- | ------------ | --------- |
| USD  | $      | 2         | US Dollar    | false     |
| CNY  | ¥      | 2         | Chinese Yuan | false     |
| EUR  | €      | 2         | Euro         | false     |
| JPY  | ¥      | 0         | Japanese Yen | false     |
| BTC  | ₿      | 8         | Bitcoin      | true      |

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

# 23. 数据库版本演进与迁移机制 (Database Schema Migration)

随着系统升级，数据库结构会发生变化（新增表、修改字段、添加索引等）。必须采用结构化的迁移机制，确保开发、测试、生产环境的数据库架构保持一致且可追溯。

## 23.1 迁移工具选型

**推荐方案：Flyway**

- **语言无关**：基于 SQL 文件，不绑定特定编程语言
- **版本化**：每个迁移脚本有唯一版本号（如 `V1__initial_schema.sql`）
- **状态追踪**：通过 `flyway_schema_history` 表记录已执行的迁移
- **幂等性**：已执行的迁移不会重复执行
- **回滚支持**：通过 Undo 脚本（商业版）或手动编写回滚 SQL
- **跨平台**：支持 PostgreSQL、MySQL、Oracle 等

**备选方案：Liquibase**

- 支持 XML/YAML/JSON 格式
- 更强的回滚和条件迁移能力
- 学习曲线略陡

**不推荐：手动 SQL 脚本**

- 难以追踪执行状态
- 容易重复执行
- 无法自动化集成

## 23.2 Flyway 集成方式

### 方案 A：独立 CLI 工具（推荐）

在应用启动前，通过 CI/CD 或启动脚本独立运行 Flyway：

```bash
# flyway.conf 配置文件
flyway.url=jdbc:postgresql://localhost:5432/pfh
flyway.user=pfh_user
flyway.password=${DB_PASSWORD}
flyway.locations=filesystem:./migrations
flyway.baselineOnMigrate=true
flyway.validateOnMigrate=true

# 执行迁移
flyway migrate
```

**优势**：

- 与应用代码解耦
- 支持独立运维
- 适合容器化部署（在应用容器启动前运行 init 容器）

### 方案 B：嵌入式集成

在 C++ 应用启动时，通过 `system()` 调用 Flyway CLI：

```cpp
// infrastructure/database/MigrationRunner.cpp
#include <cstdlib>
#include <filesystem>

class MigrationRunner {
public:
    static bool runMigrations(const std::string& dbUrl,
                              const std::string& dbUser,
                              const std::string& dbPassword) {
        // 设置环境变量
        setenv("FLYWAY_URL", dbUrl.c_str(), 1);
        setenv("FLYWAY_USER", dbUser.c_str(), 1);
        setenv("FLYWAY_PASSWORD", dbPassword.c_str(), 1);

        // 执行 Flyway
        int result = system("flyway migrate");

        if (result != 0) {
            LOG_ERROR << "Database migration failed with code " << result;
            return false;
        }

        LOG_INFO << "Database migration completed successfully";
        return true;
    }
};

// main.cpp
int main() {
    // 1. 执行数据库迁移
    if (!MigrationRunner::runMigrations(
        "jdbc:postgresql://localhost:5432/pfh",
        "pfh_user",
        std::getenv("DB_PASSWORD")
    )) {
        LOG_FATAL << "Failed to migrate database, aborting startup";
        return 1;
    }

    // 2. 启动 Drogon 应用
    drogon::app().addListener("0.0.0.0", 8080);
    drogon::app().run();
    return 0;
}
```

**优势**：

- 应用启动自动执行迁移
- 适合开发环境

**劣势**：

- 需要在运行环境安装 Flyway CLI
- 应用无法控制迁移细节

## 23.3 迁移脚本组织结构

```
project/
├── migrations/
│   ├── V1__initial_schema.sql
│   ├── V2__add_user_preferences.sql
│   ├── V3__add_refresh_tokens.sql
│   ├── V4__add_locale_to_category_templates.sql
│   ├── V5__add_exchange_rate_indices.sql
│   └── R__seed_default_categories.sql  (可重复执行)
├── src/
├── CMakeLists.txt
└── flyway.conf
```

**命名规范**

- **版本化迁移**：`V<版本号>__<描述>.sql`
  - `V1__initial_schema.sql`
  - `V2.1__add_user_email.sql`（支持小数版本）
- **可重复迁移**：`R__<描述>.sql`
  - 每次 checksum 变化时重新执行
  - 适合视图、存储过程、种子数据
- **Undo 迁移**（可选）：`U<版本号>__<描述>.sql`
  - `U2__remove_user_email.sql`

## 23.4 迁移脚本示例

### V1\_\_initial_schema.sql

```sql
-- Version 1: Initial Schema
-- Date: 2026-06-25
-- Description: Create core tables (users, accounts, transactions, currencies)

-- Users
CREATE TABLE users (
    id BIGSERIAL PRIMARY KEY,
    username VARCHAR(64) NOT NULL UNIQUE,
    password_hash VARCHAR(255) NOT NULL,
    base_currency_code CHAR(3) NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Accounts
CREATE TYPE account_type AS ENUM ('cash', 'savings', 'credit', 'digital_wallet', 'investment', 'crypto', 'other');
CREATE TYPE account_category AS ENUM ('asset', 'liability');

CREATE TABLE accounts (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    name VARCHAR(128) NOT NULL,
    type account_type NOT NULL,
    subtype VARCHAR(64) NOT NULL,
    category account_category NOT NULL,
    currency_code CHAR(3) NOT NULL,
    description TEXT,
    is_archived BOOLEAN NOT NULL DEFAULT FALSE,
    archived_at TIMESTAMPTZ,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

CREATE INDEX idx_accounts_user_id ON accounts(user_id);
CREATE INDEX idx_accounts_user_type ON accounts(user_id, type);

-- Transactions
CREATE TYPE transaction_type AS ENUM ('income', 'expense', 'transfer', 'adjustment');

CREATE TABLE transactions (
    id BIGSERIAL PRIMARY KEY,
    user_id BIGINT NOT NULL REFERENCES users(id),
    account_id BIGINT NOT NULL REFERENCES accounts(id),
    category_id BIGINT,
    type transaction_type NOT NULL,
    amount NUMERIC(20,8) NOT NULL,
    currency_code CHAR(3) NOT NULL,
    description TEXT,
    transfer_group_id UUID,
    deleted_at TIMESTAMPTZ,
    transaction_time TIMESTAMPTZ NOT NULL,
    created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    version BIGINT NOT NULL DEFAULT 1
);

CREATE INDEX idx_transactions_account_id ON transactions(account_id);
CREATE INDEX idx_transactions_user_time ON transactions(user_id, transaction_time);
CREATE INDEX idx_transactions_time ON transactions(transaction_time);

-- ... 其他表 ...
```

### V2\_\_add_user_preferences.sql

```sql
-- Version 2: User Preferences
-- Date: 2026-06-26
-- Description: Add user_preferences table for extended settings

CREATE TYPE theme_mode AS ENUM ('system', 'light', 'dark');
CREATE TYPE default_home_page AS ENUM ('dashboard', 'transactions', 'reports', 'accounts');
CREATE TYPE report_period AS ENUM ('current_month', 'last_month', 'last_3_months', 'current_year', 'custom');

CREATE TABLE user_preferences (
    user_id BIGINT PRIMARY KEY REFERENCES users(id),
    base_currency_code CHAR(3) NOT NULL,
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

-- 为现有用户初始化默认偏好
INSERT INTO user_preferences (user_id, base_currency_code, locale, timezone)
SELECT id, base_currency_code, 'zh-CN', 'Asia/Shanghai'
FROM users
ON CONFLICT (user_id) DO NOTHING;
```

### V3\_\_add_refresh_tokens.sql

```sql
-- Version 3: Refresh Tokens
-- Date: 2026-06-27
-- Description: Add refresh_tokens table for JWT authentication

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

## 23.5 迁移执行流程

### 开发环境

```bash
# 1. 本地启动 PostgreSQL
docker-compose up -d postgres

# 2. 执行迁移
flyway migrate

# 3. 启动应用
./build/pfh-server
```

### CI/CD 流程

```yaml
# .github/workflows/deploy.yml
jobs:
  deploy:
    steps:
      - name: Checkout code
        uses: actions/checkout@v3

      - name: Run database migrations
        env:
          FLYWAY_URL: ${{ secrets.DB_URL }}
          FLYWAY_USER: ${{ secrets.DB_USER }}
          FLYWAY_PASSWORD: ${{ secrets.DB_PASSWORD }}
        run: |
          flyway migrate

      - name: Deploy application
        run: |
          docker-compose up -d app
```

### 生产环境

```bash
# 方案 1: 蓝绿部署
# 1. 先迁移数据库（向后兼容）
flyway migrate

# 2. 更新应用代码
kubectl rollout restart deployment/pfh-api

# 方案 2: Kubernetes Init Container
# 在应用容器启动前运行迁移
apiVersion: v1
kind: Pod
spec:
  initContainers:
  - name: db-migration
    image: flyway/flyway:latest
    command: ["flyway", "migrate"]
    env:
    - name: FLYWAY_URL
      value: "jdbc:postgresql://db:5432/pfh"
  containers:
  - name: app
    image: pfh-api:latest
```

## 23.6 迁移最佳实践

1. **向后兼容**：
   - 新增字段必须有默认值或允许 NULL
   - 删除字段分两步：先停用（应用不再使用），再删除（下个版本）
   - 重命名字段：先新增 → 双写 → 迁移数据 → 删除旧字段

2. **幂等性**：
   - 使用 `IF NOT EXISTS` / `IF EXISTS`
   - 避免硬编码 ID 或数据

3. **事务性**：
   - 每个迁移脚本在单独的事务中执行
   - 避免在迁移中执行长时间锁表操作

4. **测试**：
   - 在测试数据库先验证
   - 准备回滚脚本
   - 大规模数据迁移先在副本上测试性能

5. **文档化**：
   - 每个迁移脚本顶部注释说明目的
   - 记录破坏性变更

## 23.7 回滚策略

**场景 1：迁移失败**

Flyway 自动回滚事务，数据库保持迁移前状态。

**场景 2：应用部署失败需回滚**

```bash
# 手动回滚到指定版本（需要 Undo 脚本）
flyway undo

# 或手动执行回滚 SQL
psql -U pfh_user -d pfh -f rollback_v3.sql
```

**场景 3：数据迁移错误**

准备补偿脚本：

```sql
-- V4.1__fix_data_migration.sql
-- 修复 V4 中的数据错误
UPDATE accounts SET type = 'savings' WHERE type = 'saving'; -- 修正拼写错误
```

---

## 24. 最终原则

1. Transaction 永远是真实数据

2. Balance Cache 随时可重建

3. 汇率必须保留历史

4. 报表不存储结果

5. 外部同步必须幂等

6. **数据库迁移必须版本化、可追溯、可回滚**

7. 所有统计都能从流水重新计算

```

```
