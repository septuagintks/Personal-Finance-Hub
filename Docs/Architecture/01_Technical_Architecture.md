# Personal Finance Hub - Technical Architecture Design

Version: 1.0
Backend: C++23
Architecture: Clean Architecture

---

## 1. Project Overview

### 1.1 Goal

Build a personal finance aggregation platform focused on learning modern C++ backend development.

### 1.2 System Requirements:

- Multi-account asset management
- Transaction records
- Balance calculation
- Multi-currency support
- Exchange rate management
- Financial reports
- Future external platform sync
- Future automated data collection

### 1.3 Architecture Priorities:

- Maintainability
- Extensibility
- Domain-Driven Design (DDD) concepts
- Separation of concerns
- Backend-centric business logic

---

## 2. High-Level Architecture

```text
┌─────────────────────────────┐
│   Frontend (Vue3)           │
└─────────────┬───────────────┘
              │ REST API
┌─────────────▼───────────────┐
│  Presentation Layer         │
├─────────────────────────────┤
│  Auth Module                │
│  Account Module             │
│  Transaction Module         │
│  Currency Module            │
│  Report Module              │
└─────────────┬───────────────┘
              │
┌─────────────▼───────────────┐
│  Application Layer          │
├─────────────────────────────┤
│  CreateTransactionUseCase   │
│  CreateTransferUseCase      │
│  DeleteTransactionUseCase   │
│  FinanceApplicationService  │
│  ReportQueryService         │
│  RefreshExchangeRatesUseCase │
└─────────────┬───────────────┘
              │
┌─────────────▼───────────────┐
│  Domain Layer               │
├─────────────────────────────┤
│  TransferDomainService      │
│  CurrencyConversionService  │
│  BalanceCalculationService  │
└─────────────┬───────────────┘
              │
┌─────────────▼───────────────┐
│  Infrastructure Layer       │
├─────────────────────────────┤
│  AccountRepository          │
│  TransactionRepository      │
│  ExchangeRateRepository     │
└─────────────┬───────────────┘
              │
┌─────────────▼───────────────┐
│      PostgreSQL             │
└─────────────────────────────┘

```

---

## 3. Tech Stack

### 3.1 Backend

| Component     | Technology       |
| ------------- | ---------------- |
| Language      | C++23            |
| Presentation  | Drogon           |
| Build System  | CMake            |
| Database      | PostgreSQL       |
| Cache         | PostgreSQL table |
| Serialization | JSON             |
| Scheduler     | Internal Worker  |
| Logging       | spdlog           |
| Testing       | GoogleTest       |

### 3.2 Frontend

| Component    | Technology   |
| ------------ | ------------ |
| Framework    | Vue 3        |
| Build Tool   | Vite         |
| UI Framework | Element Plus |
| Charts       | ECharts      |
| HTTP Client  | Axios        |

---

## 4. Layered Design

### 4.1 Presentation Layer

**Responsibilities:**

- REST API endpoint handling
- Request validation
- Authentication
- DTO conversion
- Response formatting

### 4.2 Application Layer

**Responsibilities:**

- Use case orchestration
- Repository calls
- Database transaction boundaries
- Domain Service coordination
- Domain Event publication

Application Layer uses explicit Use Case classes:

```text
application/use_cases/
├── CreateTransactionUseCase
├── CreateTransferUseCase
├── DeleteTransactionUseCase
├── ResourceUseCases
├── FinanceApplicationService
├── ReportQueryService
└── RefreshExchangeRatesUseCase
```

Application Layer must not define generic services named `AccountingService`, `ExchangeRateService`, or `ReportService`.

Presentation 不直接持有或编排 Repository。资源 Controller 统一调用 `FinanceApplicationService`；该门面通过 `IRequestScopeFactory` 为每次认证操作创建与 JWT `UserId` 一致的 request scope，再从 scope 取得 tenant-bound Repository 与 Unit of Work。`IRequestScope` 是 Application 端口，PostgreSQL/In-Memory 构造细节只存在于 Infrastructure。

### 4.3 Domain Layer

**Responsibilities:**

- Pure business rules
- Transfer consistency validation
- Currency conversion math
- Balance calculation rules

Domain Layer services:

```text
domain/services/
├── TransferDomainService
├── CurrencyConversionService
└── BalanceCalculationService
```

Domain Services do not call Repository, open database transactions, or publish events.

### 4.4 Infrastructure Layer

**Responsibilities:**

- SQL data access
- Persistence operations
- Query abstraction

### 4.5 Database Layer

**Responsibilities:**

- Data storage
- Indexing
- Constraints
- Historical record keeping

---

### 4.6 Dependency Direction

```text
Presentation
      ↓

Application
      ↓

Domain

Infrastructure
      ↓
Domain
```

**强调：**

- `Domain` 不依赖任何框架。

---

## 5. Core Domain Models

### 5.1 Account

Represents an asset container.

**Examples:**

- Cash
- Savings Account
- Credit Account
- Digital Wallet
- Investment Account
- Crypto Account
- Other Account

**Fields:**

```cpp
AccountId
Name
Type
Subtype
AccountCategory
CurrencyCode
CreatedAt
UpdatedAt
```

`Type` 是账户大类，`Subtype` 是用户可自定义的具体账户类型。
例如 `银行卡`、`支付宝`、`信用卡` 是 subtype，不是账户名称。
账户余额允许为负数，净值/总资产汇总按所有账户余额折算后求和。

---

### 5.2 Transaction

Represents a financial event.

**Fields:**

```cpp
TransactionId
AccountId
CategoryId
Amount
CurrencyCode
TransactionType
Description
CreatedAt
```

**Supported Types:**

```text
Income
Expense
Transfer
Adjustment

```

---

### 5.3 TransferAggregate

**Structure:**

```text
TransferAggregate

├── OutgoingTransaction
├── IncomingTransaction
├── ExchangeRate(optional)
└── Adjustments[]
```

---

### 5.4 Transfer Architecture

Transfer is implemented as a dedicated aggregate.

```text
TransferAggregate
├── OutgoingTransaction
├── IncomingTransaction
├── ExchangeRate(optional)
└── Adjustments[]
```

Transfer metadata is stored separately from transaction records.
Transactions remain the single source of truth.

转账产生的出账流水和入账流水，其 `TransactionType` 必须严格标记为 `Transfer`。
不得复用 `Income` 或 `Expense` 表示转账两端，否则报表会把资产内部移动误计为收入或支出。

手续费、汇兑损耗等转账调整项必须作为独立 `Adjustment` 流水，或归入明确的 `Expense` 类别（例如金融手续费），不得隐藏在转账主金额中。

---

### 5.5 Category

用于交易分类。系统提供预设分类池，用户实际使用的是自己的收入/支出分类树。
预设分类加入用户分类树后可编辑、可删除；删除不影响系统模板。
**示例：**

```text
- 支出: 餐饮、日常、交通、医疗、娱乐
- 收入: 工资、奖金、投资、兼职、副业、红包

```

分类最多支持 64 层（根节点与当前节点均计入），并通过 `CategoryBoard` 区分收入板块和支出板块。Repository 的创建、更新、root 回溯和列表建树必须使用同一上限，禁止写入随后无法读取的树。

### 5.6 Tag

用于给交易添加跨分类辅助标记。

```text
travel
business
reimbursable
tax
```

Tag 不参与余额计算，只用于检索、过滤和报表扩展。

---

### 5.7 Currency

表示支持的货币。
**示例：**

```text
- USD
- CNY
- EUR
- JPY
- HKD
```

---

### 5.8 Exchange Rate

表示历史汇率。
**字段：**

```cpp
BaseCurrency
TargetCurrency
Rate
Timestamp
Source
```

---

## 6. Application Use Case 设计

Application Layer 只放具体 Use Case，负责调用 Repository、开启事务、编排 Domain Service、发布 Domain Event。

不要在 Application 和 Domain 同时存在 `AccountingService`、`ExchangeRateService`、`ReportService` 这类同名服务。

### 6.1 CreateTransactionUseCase

**职责：**

- 校验输入 DTO
- 创建单笔收入、支出或调整流水
- 调用 TransactionRepository 保存
- 发布 TransactionCreated 事件

### 6.2 CreateTransferUseCase

**职责：**

- 开启数据库事务
- 编排 `TransferDomainService`
- 调用 TransactionRepository 保存 TransferAggregate
- 更新或失效余额缓存
- 发布 TransferCompleted 事件

### 6.3 DeleteTransactionUseCase

**职责：**

- 开启数据库事务
- 执行软删除或危险删除流程
- 清理相关缓存记录
- 写入 AuditLog
- 发布 TransactionDeleted 事件

### 6.4 ReportQueryService

**职责：**

- 查询 Repository
- 显式排除 `TransactionType::Transfer`
- 调用 `CurrencyConversionService`
- 生成 net worth、cash flow trend 与 dashboard DTO
- 以用户时区计算半开月窗，并由调用方注入 `IClock` 的当前时间

统计月度总收入或月度总支出时，查询条件必须显式排除 `TransactionType::Transfer`。
转账只表示资产在账户之间移动，不参与收入/支出聚合。

### 6.5 RefreshExchangeRatesUseCase

**职责：**

- 拉取外部汇率
- 保存新的历史汇率快照
- 发布 ExchangeRateRefreshed 事件

---

### 6.6 Domain Service 设计

Domain Service 只负责纯业务规则，不调用 Repository，不管理事务。

### 6.7 TransferDomainService

**职责：**

- 校验转账两端必须为 `TransactionType::Transfer`
- 校验同币种转账金额一致
- 校验跨币种转账与汇率匹配
- 校验手续费、汇兑损耗必须作为 Adjustment

### 6.8 CurrencyConversionService

**职责：**

- 执行 Money 与 ExchangeRate 的转换
- 处理舍入策略
- 计算转换损耗

### 6.9 BalanceCalculationService

**职责：**

- 根据 Transaction 集合计算 BalanceSnapshot
- 排除已删除流水
- 应用收入、支出、转账、调整的金额方向规则

---

### 6.10 Sync Use Cases (预留)

未来的扩展点。

可能包含：

- ImportTransactionsUseCase
- ReconcileAccountUseCase

---

## 7. 数据库设计

### 7.1 accounts

```sql
id
name
type account_type
currency\\\_code
created\\\_at
updated\\\_at

```

### 7.2 categories

```sql
id
name
parent\\\_id

```

### 7.3 transactions

```sql
id
account\\\_id
category\\\_id
amount
currency\\\_code
type transaction_type
description
version
created\\\_at

```

### 7.4 account_balance_cache

```sql
account\\\_id
balance
last_transaction_id
source_version
cache_version
updated\\\_at

```

缓存边界：

1. Domain 只面向 `IAccountRepository` 请求 `BalanceSnapshot` Read Model
2. Domain 不关心余额来自缓存表还是流水聚合
3. Infrastructure 先查询 PostgreSQL `account_balance_cache`
4. 命中且有效则映射为 `BalanceSnapshot` 返回
5. 未命中或失效则执行 `SELECT SUM(...)` 聚合流水，更新缓存表，再返回

### 7.5 currencies

```sql
code
name
symbol

```

### 7.6 exchange_rates

```sql
id
base\\\_currency
target\\\_currency
rate
timestamp
source

```

---

## 8. 调度器架构

**目的：**

- 汇率更新
- 后台任务
- 未来的同步任务

**结构：**

```text
Scheduler (调度器)
    │
    ├── ExchangeRateJob (汇率作业)
    ├── CleanupJob (清理作业)
    └── SyncJob (同步作业 - 预留)

```

---

## 9. API 设计原则

**指导方针：**

- RESTful 设计
- JSON 数据交互
- 无状态请求
- 版本化 API

**示例：**

```text
/api/v1/accounts
/api/v1/transactions
/api/v1/currencies
/api/v1/reports

```

---

## 10. 目录结构

```text
backend/

├── domain/
│   ├── entities/
│   ├── value_objects/
│   ├── aggregates/
│   ├── repositories/
│   ├── services/
│   └── events/
│
├── application/
│   ├── use_cases/
│   ├── dto/
│   └── commands/
│
├── infrastructure/
│   ├── persistence/
│   ├── exchange_rate/
│   ├── sync/
│   └── external/
│
├── presentation/
│   ├── controllers/
│   ├── middleware/
│   ├── dto/
│   └── routes/
│
├── scheduler/
│
└── tests/
```

---

## 11. 未来的扩展点

### 11.1 Redis

当前阶段不引入 Redis 强依赖，技术栈保持为 C++23 + PostgreSQL。
Redis 仅作为未来基础设施级扩展，用于 Dashboard Summary、Monthly Report 或 Latest Exchange Rates 等可重建数据。

### 11.2 数据提供方 (Data Providers)

**预留给：**

```text
CSV
Bank (银行)
Broker (券商)
Crypto Exchange (加密货币交易所)
Open Banking API (开放银行 API)

```

### 11.3 同步框架 (Synchronization Framework)

**接口：**

```cpp
class IDataProvider
{
public:
    virtual SyncResult sync() = 0;
};

```

**潜在实现：**

```cpp
CsvProvider
BankProvider
CryptoProvider
ManualImportProvider

```

---

## 12. 非功能性需求

### 12.1 可靠性 (Reliability)

- 事务安全
- 支持回滚
- 数据完整性

### 12.2 性能 (Performance)

- 余额缓存
- 索引查询
- 高效聚合

### 12.3 可维护性 (Maintainability)

- 分层隔离
- 依赖隔离
- 测试覆盖率

### 12.4 可扩展性 (Extensibility)

- 类插件式的提供方模型
- 模块化服务
- 独立的领域组件
