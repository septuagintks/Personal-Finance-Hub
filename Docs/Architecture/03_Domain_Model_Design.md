# Personal Finance Hub - Domain Model Design

Version: 2.0  
Backend: C++23  
Architecture: Clean Architecture + Lightweight DDD

---

## 1. File Description

This document defines the core domain models.

Goals:

- Multi-currency support
- Account system
- Transaction system
- Transfer system
- Exchange rate system
- Report system
- Sync system extension points

Domain Layer MUST:

- Not depend on Drogon
- Not depend on PostgreSQL
- Not depend on Redis
- Not depend on HTTP
- Not depend on JSON

Domain layer contains only pure C++23 code.

---

## 2. Clean Architecture

Project structure:

```text
backend/

├── domain/
│
├── application/
│
├── infrastructure/
│
├── presentation/
│
├── scheduler/
│
└── tests/
```

Dependency direction:

```text
presentation
      ↓

application
      ↓

domain

infrastructure
      ↓
domain
```

Domain has no outward dependencies.

---

## 3. Domain Structure

```text
domain/

├── entities/
├── value_objects/
├── aggregates/
├── repositories/
├── services/
└── events/
```

---

## 4. Entities and Value Objects

### 4.1 强类型 ID (Strongly-Typed IDs)

为了防止在函数调用时传错 ID（例如将 `AccountId` 传给 `UserId`），系统全面采用强类型 ID。为了解决 Drogon 路由解析和 JSON 序列化的冗余代码问题，我们在基础设施层（或领域层基础库）提供统一的 `StrongId` 模板：

```cpp
// domain/value_objects/StrongId.hpp
#pragma once
#include <cstdint>
#include <string>
#include <optional>
#include <json/json.h> // 假设使用 jsoncpp

template <typename Tag, typename Underlying = int64_t>
struct StrongId {
    Underlying value;
    
    explicit StrongId(Underlying v) : value(v) {}
    StrongId() : value(0) {}
    
    bool operator==(const StrongId& o) const { return value == o.value; }
    bool operator!=(const StrongId& o) const { return value != o.value; }
    bool operator<(const StrongId& o)  const { return value < o.value;  }
    
    // 统一的字符串转换（供路由层使用）
    static std::optional<StrongId> from_string(std::string_view s) {
        try {
            return StrongId{static_cast<Underlying>(std::stoll(std::string(s)))};
        } catch (...) {
            return std::nullopt;
        }
    }
    
    std::string to_string() const { return std::to_string(value); }
    
    // JSON 互转（供 DTO 层使用）
    Json::Value to_json() const  { return Json::Value(static_cast<Json::Int64>(value)); }
    
    static std::optional<StrongId> from_json(const Json::Value& j) {
        if (j.isInt64()) return StrongId{j.asInt64()};
        if (j.isString()) return from_string(j.asString());
        return std::nullopt;
    }
};

// 具体 ID 类型只需一行声明
struct UserIdTag {};
struct AccountIdTag {};
struct TransactionIdTag {};
struct CategoryIdTag {};
struct TagIdTag {};

using UserId        = StrongId<UserIdTag>;
using AccountId     = StrongId<AccountIdTag>;
using TransactionId = StrongId<TransactionIdTag>;
using CategoryId    = StrongId<CategoryIdTag>;
using TagId         = StrongId<TagIdTag>;
```

### 4.2 Entities

Have unique identity.

Include:

```text
User
UserPreference
Account
Category
Tag
Transaction
AuditLog
```

### Value Objects

No identity.

Include:

```text
Money
Currency
CurrencyMetadata
ExchangeRate
DateRange
BalanceSnapshot
```

`BalanceSnapshot` 虽然包含 `AccountId`、`Money` 和 `Timestamp`，但没有独立身份和生命周期。
它保留为 Value Object，同时也是账户余额查询的 Read Model。

---

## 5. Decimal Type

Foundation for financial calculations.

Never use:

```cpp
float
double
```

Recommended:

```cpp
boost::multiprecision
```

Or fixed-precision implementation.

---

## 6. Currency Type

```cpp
class Currency
{
private:
    std::string code;
};
```

Examples:

```text
USD
CNY
JPY
EUR
HKD
```

Requirements:

- Immutable
- ISO-4217 valid

### CurrencyMetadata

`Currency` 只表达稳定代码，`CurrencyMetadata` 表达显示和格式化信息。

```cpp
class CurrencyMetadata
{
private:
    Currency currency;
    std::string displayName;
    std::string symbol;
    int32_t precision;
    bool isCrypto;
};
```

规则：

1. `CurrencyMetadata` 不参与金额相等、加减或汇率计算
2. `precision` 只决定默认展示小数位，不改变 Decimal 存储精度
3. 前端货币选择器和金额格式化应读取 `CurrencyMetadata`
4. 加密货币必须进入系统受控白名单后才可使用

---

## 7. Money Type

```cpp
class Money
{
private:
    Decimal amount;
    Currency currency;
};
```

Rules:

Allowed:

```text
100 USD + 50 USD
```

Not Allowed:

```text
100 USD + 100 CNY
```

Must convert currency first.

---

## 8. ExchangeRate Object

```cpp
class ExchangeRate
{
private:
    Currency base;
    Currency target;
    Decimal rate;
    Timestamp fetchedAt;
};
```

Example:

```text
USD -> CNY

7.18
```

---

## 9. User Entity

```cpp
class User
{
private:
    UserId id;
    Username username;
};
```

Responsible for identity only.

---

## 9.5 UserPreference Domain Concept

```cpp
class UserPreference
{
private:
    UserId userId;
    Currency baseCurrency;
    std::string locale;
    std::string timezone;
    std::string dateFormat;
    std::string numberFormat;
    ThemeMode theme;
    HomePage defaultHomePage;
    ReportPeriod defaultReportPeriod;
};
```

`UserPreference` 是领域概念，不要求对应独立数据表。
第一阶段可以由 Repository 从 `users.base_currency_code` 和 `user_preferences` 映射得到。
未来即使调整偏好表结构，也不影响 Domain 模型。

`UserPreference` 可作为独立领域对象，也可作为 `User` 聚合的一部分。
全局报表和 Net Worth 计算必须从该领域对象读取 Base Currency。

扩展字段含义：

- `baseCurrency`: 默认基准货币，用于 Dashboard、报表和净值折算
- `locale`: 前端本地化语言，例如 `zh-CN`、`en-US`
- `timezone`: 用户时区，例如 `Asia/Shanghai`
- `dateFormat`: 日期展示格式，例如 `YYYY-MM-DD`
- `numberFormat`: 数字展示格式，例如 `1,234.56`
- `theme`: 主题偏好，例如 `System`、`Light`、`Dark`
- `defaultHomePage`: 登录后的默认首页
- `defaultReportPeriod`: 报表默认时间范围，例如月、季度、年

```cpp
enum class ThemeMode
{
    System,
    Light,
    Dark
};

enum class HomePage
{
    Dashboard,
    Transactions,
    Reports,
    Accounts
};

enum class ReportPeriod
{
    CurrentMonth,
    LastMonth,
    Last3Months,
    CurrentYear,
    Custom
};
```

---

## 10. Account Entity

Asset container.

```cpp
class Account
{
private:
    AccountId id;
    UserId owner;
    std::string name;
    AccountType type;
    std::string subtype;
    AccountCategory category;
    Currency currency;
};
```

Account name is mandatory. `银行卡`、`信用卡`、`支付宝`、`微信支付` 这类词是账户子分类，不是账户名称。
用户创建账户时必须输入可识别的名称，例如：

```text
招商银行储蓄卡
招行信用卡 2026
支付宝余额
微信零钱
Binance BTC
```

账户余额允许为负数。负数用于表达透支、信用账户欠款、账户调整或同步纠偏。
总资产/净值汇总按所有账户余额折算到基准货币后直接求和，负数余额会自然抵减总额。

---

## 11. Account Types

```cpp
enum class AccountType
{
    Cash,
    Savings,
    Credit,
    DigitalWallet,
    Investment,
    Crypto,
    Other
};

enum class AccountCategory
{
    Asset,
    Liability
};
```

`AccountType` 是账户大类，`subtype` 是用户可自定义的具体账户类型。

默认子分类示例：

| AccountType   | 默认子分类                               |
| ------------- | ---------------------------------------- |
| Savings       | 银行卡、借记卡、活期账户、定期账户       |
| Credit        | 信用卡、花呗、京东白条、借贷账户         |
| DigitalWallet | 支付宝、微信支付、PayPal、Apple Pay      |
| Investment    | 股票账户、基金账户、券商账户、养老金账户 |
| Cash          | 现金、零钱包、备用金                     |
| Crypto        | 比特币、以太坊、交易所账户、硬件钱包     |
| Other         | 自定义                                   |

行为规则：

1. 所有账户都必须有 `name`、`type`、`subtype`、`currency`
2. 所有账户余额都支持负数
3. `Credit` 默认映射为 `AccountCategory::Liability`
4. 其他大类默认映射为 `AccountCategory::Asset`
5. 用户可以覆盖 `AccountCategory`，用于处理特殊账户
6. 统计总资产/净值时，系统不按大类排除账户，而是将所有账户余额折算后求和
7. 报表可以按 `AccountType`、`subtype`、`AccountCategory` 分组展示

---

## 12. Account Lifecycle

Two approaches supported.

### Archive

Recommended.

```text
Hide account
Preserve all transactions
Preserve statistics
Preserve reports
```

### Dangerous Delete

Completely remove:

```text
Account
All transactions
All statistics
```

需要至少三次确认

未来可扩展输入 DELETE 文本确认

Dangerous Delete requires:

1. Explicit user action
2. Multiple confirmations
3. Application layer execution
4. Audit logging

Dangerous Delete is an Application Use Case, not a database cascade.
The application layer opens a DB transaction and executes:

1. Soft delete or hard delete all Transactions under the Account
2. Delete the Account's `account_balance_cache` row
3. Soft delete or hard delete the Account
4. Record AuditLog
5. Commit the DB transaction

---

## 13. Category Entity

```cpp
class Category
{
private:
    CategoryId id;
    UserId owner;
    std::string name;
    std::optional<CategoryId> parentId;
    CategoryBoard board;
    CategorySource source;
    std::optional<SystemCategoryTemplateId> templateId;
};
```

Support unlimited hierarchy.

```cpp
enum class CategorySource
{
    System,
    User
};

enum class CategoryBoard
{
    Income,
    Expense
};
```

分类系统分为两层：

1. System Category Template: 系统预设分类池，只用于检索和初始化，不直接记账
2. User Category: 用户实际启用到收入/支出板块的分类树，可编辑、可删除、可新增

系统预设分类被用户加入收入或支出板块时，会复制为用户自己的 `Category` 记录，并保留 `source = System` 与 `templateId`。
用户可以删除这些预设分类；删除只影响该用户自己的分类树，不删除系统模板。

可选分类池的大类仅用于检索和归组，不强制成为用户的一级分类。
用户可以把分类池中的大类、一级分类或二级分类名称加入收入/支出板块作为一级分类，也可以继续添加子分类。

可选分类池大类：

```text
餐饮、日常、财务、居家、服饰、美妆、数码、办公、社交、医疗、交通、运动、娱乐、教育、旅行、宠物、家庭、汽车、校园、人情、其他
```

支出板块默认一级分类：

```text
餐饮、日常、财务、居家、服饰、美妆、数码、办公、社交、医疗、交通、运动、娱乐、教育、旅行、宠物、家庭、汽车、校园、人情、其他
```

支出板块默认二级分类示例：

| 一级分类 | 默认二级分类示例                           |
| -------- | ------------------------------------------ |
| 餐饮     | 早餐、午餐、晚餐、咖啡、外卖、聚餐         |
| 日常     | 水费、电费、燃气费、物业费、生活用品、维修 |
| 交通     | 地铁、公交、打车、加油、停车、高铁、机票   |
| 医疗     | 挂号、药品、体检、保险、牙科               |
| 娱乐     | 电影、游戏、会员订阅、演出                 |
| 财务     | 手续费、利息支出、汇兑损耗                 |

收入板块默认一级分类：

```text
工资、奖金、投资、兼职、副业、红包
```

收入板块默认二级分类示例：

| 一级分类 | 默认二级分类示例               |
| -------- | ------------------------------ |
| 工资     | 基本工资、绩效、补贴           |
| 奖金     | 年终奖、项目奖金               |
| 投资     | 股息、基金收益、利息、卖出收益 |
| 兼职     | 劳务收入、咨询收入             |
| 副业     | 店铺收入、内容收入             |
| 红包     | 亲友红包、平台红包             |

`红包` 在可选分类池中归入 `财务` 大类，但收入板块可以直接把它作为一级分类。

分类约束：

1. 分类名称在同一用户、同一父分类、同一板块下唯一
2. `parentId` 所指向分类必须属于同一用户和同一 `CategoryBoard`
3. 交易的 `TransactionType::Income` 只能选择收入板块分类
4. 交易的 `TransactionType::Expense` 或费用类 `Adjustment` 只能选择支出板块分类
5. `TransactionType::Transfer` 默认不要求分类
6. 删除分类使用软删除，历史流水保持引用

---

## 14. Category Lifecycle

Use soft delete.

After deletion:

```text
Historical transactions preserved
Historical statistics preserved
```

Display as:

```text
Deleted Category
```

Or:

```text
Uncategorized
```

---

## 14.5 Tag Entity

```cpp
class Tag
{
private:
    TagId id;
    UserId owner;
    std::string name;
    Timestamp createdAt;
    std::optional<Timestamp> deletedAt;
};
```

Tag 用于跨分类、跨账户给流水打辅助标记，例如：

```text
travel
business
reimbursable
vacation
tax
```

规则：

1. Tag 属于用户
2. 同一用户下 Tag 名称唯一
3. 一笔 Transaction 可以有多个 Tag
4. Tag 删除使用软删除，历史流水关系保留或在展示时标记为已删除
5. Tag 不参与余额计算，只用于检索、过滤和报表维度扩展

---

## 14.6 AuditLog Entity

```cpp
class AuditLog
{
private:
    AuditLogId id;
    UserId operatorUserId;
    AuditAction action;
    std::string resourceType;
    std::string resourceId;
    JsonSnapshot before;
    JsonSnapshot after;
    Timestamp occurredAt;
};
```

AuditLog 是审计记录，不参与领域规则计算，但必须由危险或关键 Use Case 写入。

必须记录的操作：

- 危险删除账户
- 归档账户
- 修改账户类型、名称、币种
- 同步导入与重复导入跳过
- 汇率刷新
- 分类批量初始化或删除
- 用户偏好修改

字段语义：

- `operatorUserId`: 执行人
- `action`: 操作类型，例如 `Create`、`Update`、`Archive`、`Delete`、`DangerousDelete`、`SyncImport`、`Refresh`
- `resourceType`: 资源类型，例如 `Account`、`Transaction`、`Category`、`ExchangeRate`
- `resourceId`: 资源 ID，允许字符串，兼容 UUID、外部 ID 和复合 ID
- `before`: 修改前快照
- `after`: 修改后快照
- `occurredAt`: 发生时间

---

## 15. Transaction Entity

Core entity.

```cpp
class Transaction
{
private:
    TransactionId id;
    AccountId accountId;
    CategoryId categoryId;
    Money amount;
    TransactionType type;
    Timestamp occurredAt;
    std::string description;
};
```

---

## 16. Transaction Types

```cpp
enum class TransactionType
{
    Income,
    Expense,
    Transfer,
    Adjustment
};
```

转账产生的出账流水和入账流水必须严格标记为 `TransactionType::Transfer`。
不得复用 `Income` 或 `Expense` 表示转账两端。

报表服务计算月度总收入或月度总支出时，必须显式排除 `TransactionType::Transfer`。

---

## 17. 交易生命周期

用户层：

```text
允许删除
```

实现层：

```text
软删除
```

优势：

```text
审计
恢复
同步纠错
```

---

## 18. 调整交易

Adjustment 是特殊流水。

用途：

```text
手续费
返现
优惠
汇兑损耗
平台补贴
```

本质仍是 Transaction。

转账中的手续费、汇兑损耗等调整项必须作为独立 `TransactionType::Adjustment` 流水，或归入明确的 `Expense` 类别（例如金融手续费）。
调整项不得隐藏在转账主金额中。

---

## 19. 转账聚合

转账是聚合。

```cpp
class TransferAggregate
{
public:

    Transaction outgoing;

    Transaction incoming;

    std::optional<ExchangeRate> rate;

    std::vector<Transaction> adjustments;
};
```

### Aggregate Consistency Rules

TransferAggregate 必须满足：

1. 同币种：
   Outgoing == Incoming

2. 跨币种：
   Outgoing × Rate == Incoming

3. 差额：
   必须通过 Adjustment 表示

4. Adjustment 不允许隐藏在金额字段中

### Adjustment Rules

Adjustments may contain: 0..N transactions.

Examples:

- Fee
- Rebate
- Cashback

Fee or FX loss must be modeled as a standalone Adjustment transaction, or an explicit Expense category.
It must not be folded into outgoing or incoming transfer amount.

- FX Loss
- FX Gain

---

## 20. 同币种转账

示例：

```text
Bank A
  ↓
1000 CNY
  ↓
Bank B
```

生成：

```text
Outgoing
1000 CNY

Incoming
1000 CNY
```

无需汇率。

---

## 21. 跨币种转账

示例：

```text
1000 USD

↓

7180 CNY
```

生成：

```text
Outgoing
1000 USD

Incoming
7180 CNY

ExchangeRate
7.18
```

---

## 22. 转账计算模式

支持三种模式。

### 模式 1

已知：

```text
Outgoing Amount
Rate
```

自动计算：

```text
Incoming Amount
```

---

### 模式 2

已知：

```text
Outgoing Amount
Incoming Amount
```

自动计算：

```text
Rate
```

---

### 模式 3

已知：

```text
Incoming Amount
Rate
```

自动计算：

```text
Outgoing Amount
```

---

## 22.5 Transfer Metadata

TransferAggregate 是领域层的转账聚合，TransferGroup 是持久化/元数据承载体，用来存模式、汇率、快照时间等。

TransferAggregate references TransferGroup metadata.

TransferGroup 不是业务实体，不进入 Domain Entity 列表，也不拥有独立业务生命周期。
它只是 `TransferAggregate` 的持久化元数据载体，避免把转账聚合拆成双模型。

TransferGroup stores:

- Transfer Mode
- Exchange Rate
- Provider
- Snapshot Time

---

## 23. 调整计算

例如：

```text
1000 USD

理论到账

7180 CNY

实际到账

7170 CNY
```

生成：

```text
Adjustment
10 CNY
```

保证数据一致。

---

## 24. 余额快照

缓存对象，也是 Read Model。

```cpp
class BalanceSnapshot
{
private:

    AccountId accountId;

    Money balance;

    Timestamp calculatedAt;
};
```

不是事实来源。
没有独立身份和生命周期，因此仍属于 Value Object。

Domain 只通过 Repository 请求 `BalanceSnapshot`。
Domain 不关心余额是从 PostgreSQL 缓存表读取，还是由流水重新聚合。

---

## 25. 仓储接口

Repository 属于 Domain。

实现属于 Infrastructure。

---

### IUserRepository

```cpp
class IUserRepository
{
public:

    virtual User findById() = 0;

    virtual UserPreference preferenceOf(UserId userId) = 0;
};
```

---

### IAccountRepository

```cpp
class IAccountRepository
{
public:

    virtual Account findById() = 0;

    virtual std::vector<Account> findByUser() = 0;

    virtual BalanceSnapshot balanceOf(AccountId accountId) = 0;
};
```

`IAccountRepository::balanceOf` 的实现属于 Infrastructure：

1. 查询 PostgreSQL `account_balance_cache`
2. 命中且有效则映射并返回
3. 未命中或失效则执行 `SELECT SUM(...)` 聚合流水
4. 更新缓存表
5. 返回 `BalanceSnapshot`

---

### ITransactionRepository

```cpp
class ITransactionRepository
{
public:

    virtual void save() = 0;

    virtual Transaction findById() = 0;
};
```

---

### IExchangeRateRepository

```cpp
class IExchangeRateRepository
{
public:

    virtual ExchangeRate latest() = 0;
};
```

---

## 26. 领域服务

Domain Service 只负责纯业务规则。

Domain Service 不允许：

```text
调用 Repository
开启数据库事务
编排 Use Case
发布 Domain Event
```

这些职责属于 Application Use Case。

### TransferDomainService

负责：

```text
校验转账两端 TransactionType 必须为 Transfer
校验同币种转账金额一致
校验跨币种转账金额、汇率和舍入结果一致
校验手续费、汇兑损耗必须作为 Adjustment
构造 TransferAggregate 的领域一致性结果
```

---

### CurrencyConversionService

负责：

```text
执行 Money 与 ExchangeRate 的纯转换
处理舍入规则
计算汇兑损耗
校验跨货币运算必须显式提供汇率
```

---

### BalanceCalculationService

负责：

```text
根据 Transaction 集合计算 BalanceSnapshot
排除 deleted_at 已设置的流水
应用 Income、Expense、Transfer、Adjustment 的金额方向规则
返回可重建的余额计算结果
```

---

## 26.5 Application Use Case 边界

Application Layer 使用 `application/use_cases/` 下的具体 Use Case：

```text
CreateTransactionUseCase
CreateTransferUseCase
DeleteTransactionUseCase
GenerateMonthlyReportUseCase
RefreshExchangeRateUseCase
```

Application Use Case 负责：

```text
调用 Repository
开启事务
编排 Domain Service
发布 Domain Event
返回 DTO
```

不要在 Domain 和 Application 同时定义 `AccountingService`、`ExchangeRateService`、`ReportService` 这类同名服务。

---

## 27. 领域事件

领域事件使用过去时态命名，只携带最小必要 ID，不携带完整 Entity。
事件由 Application Use Case 在事务成功后发布。

必备事件清单：

| Event                     | 触发时机                         | 主要用途                           |
| ------------------------- | -------------------------------- | ---------------------------------- |
| TransactionCreated        | 单笔收入、支出、调整流水创建成功 | 失效余额缓存、刷新报表缓存         |
| TransactionDeleted        | 流水软删除或危险删除成功         | 失效余额缓存、写审计               |
| TransferCompleted         | TransferAggregate 完整落盘成功   | 失效两个账户余额缓存、刷新报表缓存 |
| AccountArchived           | 账户归档成功                     | 刷新 Dashboard 和账户列表          |
| AccountDangerouslyDeleted | 账户危险删除成功                 | 写高危审计、预警                   |
| CategoryCreated           | 用户新增或启用分类               | 刷新分类树缓存                     |
| CategoryDeleted           | 用户删除分类                     | 保持历史流水展示兼容               |
| ExchangeRateRefreshed     | 新汇率快照插入成功               | 失效最新汇率缓存                   |
| SyncCompleted             | 同步任务完成                     | 写同步结果、触发对账               |
| UserPreferenceUpdated     | 用户偏好修改成功                 | 刷新前端配置和报表默认参数         |
| AuditLogRecorded          | 审计日志写入成功                 | 可选的安全通知扩展                 |

---

## 28. 同步扩展点

```cpp
class IDataProvider
{
public:

    virtual SyncResult sync() = 0;
};
```

未来支持：

```text
CSV
Bank
Broker
Crypto
Alipay
WeChat
```

---

## 29. 强类型 ID

禁止：

```cpp
long long
```

推荐：

```cpp
UserId
AccountId
TransactionId
CategoryId
TagId
AuditLogId
SystemCategoryTemplateId
```

示例：

```cpp
struct AccountId
{
    int64_t value;
};
```

提高编译期安全性。

---

## 30. 设计规则

1. Domain 不依赖任何框架
2. Money 必须包含 Currency
3. Transaction 是唯一事实来源
4. Balance 可重建
5. Transfer 必须通过 Aggregate
6. Adjustment 也是 Transaction
7. 汇率历史必须保留
8. Repository 只定义接口
9. Infrastructure 实现 Repository
10. 所有统计都可以从流水重新计算
