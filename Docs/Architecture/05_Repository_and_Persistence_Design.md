# Personal Finance Hub - Repository & Persistence Design

Version: 1.0
Backend: C++23
Database: PostgreSQL 16+
Framework: Drogon ORM
Architecture: Clean Architecture + Lightweight DDD

---

## 1. 设计原则与边界

仓储（Repository）和持久化（Persistence）层是连接纯粹的领域逻辑与底层 PostgreSQL 数据库的桥梁。必须严格遵守以下原则：

1. **接口与实现彻底分离**：仓储接口（Interfaces）属于 `Domain` 层（纯 C++23，不包含任何 SQL、Drogon 或网络依赖）；仓储实现（Implementations）属于 `Infrastructure` 层。
2. **面向聚合根（Aggregate Root）**：原则上只有聚合根才拥有独立的仓储。在本项目中，`User`（含偏好）、`Account`、`TransferAggregate` 拥有独立仓储。由于 `Transaction` 和 `Adjustment` 的生命周期与 `Account` / `Transfer` 强绑定，它们可以通过专门的仓储加速查询，但写入必须符合聚合的约束。
3. **隐式缓存策略**：业务层（Domain/Application）不感知缓存的存在。Repository 内部负责维护 `account_balance_cache` 的命中断接和重建，对外只返回标准的领域对象。
4. **强类型、事件与错误处理**：使用 C++23 的强类型 ID（如 `AccountId`）防止参数传错。对于仓储查找失败或数据库异常，使用 `std::expected` 传递中性错误，不滥用异常（Exceptions），保持控制流清晰。Repository Error 只表达“数据访问结果”，不携带 Drogon、SQL 或连接池等实现细节；同一个 UnitOfWork 事务还负责把领域事件写入 `domain_events_outbox`，不在提交前直接对外派发。
5. **Use Case 与 Domain Service 分层**：Application 层只使用 `application/use_cases/` 下的具体 Use Case 进行事务和仓储编排；Domain 层只使用 `domain/services/` 下的纯业务规则服务。不要定义跨层同名的 `AccountingService`、`RefreshExchangeRatesUseCase` 或 `ReportService`。
6. **领域概念不强制等同表结构**：`UserPreference` 是 Domain Concept，拥有独立的 `user_preferences` 表，由 `IUserRepository` 联合查询或通过独立的 `IUserPreferenceRepository` 进行读写。`TransferGroup` 则相反，只是 `transfer_groups` 持久化元数据载体，不是 Domain Entity。
7. **Read Model 边界**：`BalanceSnapshot` 是 Value Object，也是账户余额查询的 Read Model。它没有独立身份或生命周期，Repository 可以从 `account_balance_cache` 或流水聚合映射得到。
8. **辅助实体仓储边界**：`Category`、`Tag`、`AuditLog`、`UserPreference` 虽不是资金事实来源，但它们是前端工作流和审计闭环的必要数据，必须有明确 Repository 接口。

---

## 2. 领域层仓储接口定义 (Domain/Repositories)

仓储接口文件必须是纯净的 C++23 头文件，仅依赖领域对象和标准库。

### 2.1 仓储错误代码枚举

```cpp
// domain/repositories/RepositoryError.hpp
#pragma once
#include <string>

enum class RepositoryStatus {
    NotFound,
    ValidationError,
    Conflict,
    DatabaseError
};

struct RepositoryError {
    RepositoryStatus status;
    std::string message;
};

```

### 2.2 账户仓储接口

```cpp
// domain/repositories/IAccountRepository.hpp
#pragma once
#include <vector>
#include <expected>
#include "domain/entities/Account.hpp"
#include "domain/value_objects/BalanceSnapshot.hpp"
#include "domain/repositories/RepositoryError.hpp"

class IAccountRepository {
public:
    virtual ~IAccountRepository() = default;

    // 获取账户基础信息（不含余额）
    virtual std::expected<Account, RepositoryError> findById(AccountId id) = 0;

    // 获取用户下的所有未归档账户
    virtual std::expected<std::vector<Account>, RepositoryError> findActiveByUser(UserId userId) = 0;

    // 获取系统中所有未归档账户正在使用的币种（用于汇率刷新任务）
    virtual std::expected<std::vector<Currency>, RepositoryError> findActiveCurrencies() = 0;

    // 核心：获取账户的实时余额快照（Repository 内部处理流水聚合或缓存读取）
    virtual std::expected<BalanceSnapshot, RepositoryError> balanceOf(AccountId id) = 0;

    // 保存或更新账户基础状态（如重命名、归档）
    virtual std::expected<void, RepositoryError> save(const Account& account) = 0;

    // 危险删除：物理删除/彻底清除账户基础记录（流水清理由 UnitOfWork 编排）
    virtual std::expected<void, RepositoryError> physicalDelete(AccountId id) = 0;
};

```

### 2.3 转账与流水仓储接口

```cpp
// domain/repositories/ITransactionRepository.hpp
#pragma once
#include <vector>
#include <expected>
#include "domain/aggregates/TransferAggregate.hpp"
#include "domain/entities/Transaction.hpp"
#include "domain/value_objects/StrongId.hpp"
#include "domain/value_objects/DateRange.hpp"

class ITransactionRepository {
public:
    virtual ~ITransactionRepository() = default;

    // 保存单笔常规收支流水（Income/Expense/Adjustment），返回持久化后的 TransactionId
    virtual std::expected<TransactionId, RepositoryError> saveSingle(const Transaction& transaction) = 0;

    // 保存转账聚合（自动拆分为多笔底层流水和一条 transfer_groups 记录），返回持久化后的 TransferGroupId
    virtual std::expected<TransferGroupId, RepositoryError> saveTransfer(const TransferAggregate& transfer) = 0;

    // 按条件查询流水，支持分页。内部自动过滤/包含软删除状态
    virtual std::expected<std::vector<Transaction>, RepositoryError> findByAccount(
        AccountId accountId,
        const DateRange& range,
        bool includeDeleted = false
    ) = 0;

    // 危险删除辅助：物理删除某个账户下的所有流水
    virtual std::expected<void, RepositoryError> physicalDeleteByAccount(AccountId accountId) = 0;
};

```

### 2.4 分类仓储接口

```cpp
// domain/repositories/ICategoryRepository.hpp
#pragma once
#include <expected>
#include <vector>
#include "domain/entities/Category.hpp"
#include "domain/repositories/RepositoryError.hpp"

class ICategoryRepository {
public:
    virtual ~ICategoryRepository() = default;

    virtual std::expected<std::vector<Category>, RepositoryError> findTreeByUser(
        UserId userId,
        CategoryBoard board,
        bool includeDeleted = false
    ) = 0;

    virtual std::expected<Category, RepositoryError> findById(CategoryId id) = 0;

    virtual std::expected<void, RepositoryError> save(const Category& category) = 0;

    virtual std::expected<void, RepositoryError> softDelete(CategoryId id, UserId userId) = 0;

    virtual std::expected<void, RepositoryError> initializeDefaultsForUser(UserId userId) = 0;
};
```

映射规则：

1. `system_category_templates` 只映射为初始化模板，不直接作为 Domain `Category`
2. `categories.source = 'system'` 表示用户启用的系统预设副本，仍属于用户
3. `softDelete` 只设置 `deleted_at`，历史流水引用不变
4. Repository 必须校验父分类和子分类属于同一用户、同一 `CategoryBoard`

### 2.5 标签仓储接口

```cpp
// domain/repositories/ITagRepository.hpp
#pragma once
#include <expected>
#include <vector>
#include "domain/entities/Tag.hpp"
#include "domain/entities/Transaction.hpp"
#include "domain/repositories/RepositoryError.hpp"

class ITagRepository {
public:
    virtual ~ITagRepository() = default;

    virtual std::expected<std::vector<Tag>, RepositoryError> findByUser(
        UserId userId,
        bool includeDeleted = false
    ) = 0;

    virtual std::expected<Tag, RepositoryError> create(UserId userId, const std::string& name) = 0;

    virtual std::expected<void, RepositoryError> softDelete(TagId tagId, UserId userId) = 0;

    virtual std::expected<void, RepositoryError> attachToTransaction(
        TransactionId transactionId,
        const std::vector<TagId>& tagIds
    ) = 0;

    virtual std::expected<std::vector<Tag>, RepositoryError> findByTransaction(TransactionId transactionId) = 0;
};
```

映射规则：

1. 同一用户下 Tag 名称唯一
2. Tag 软删除后，历史流水关系保留
3. `attachToTransaction` 必须校验流水和 Tag 属于同一用户

### 2.6 用户偏好仓储接口

```cpp
// domain/repositories/IUserPreferenceRepository.hpp
#pragma once
#include <expected>
#include "domain/entities/UserPreference.hpp"
#include "domain/repositories/RepositoryError.hpp"

class IUserPreferenceRepository {
public:
    virtual ~IUserPreferenceRepository() = default;

    virtual std::expected<UserPreference, RepositoryError> findByUser(UserId userId) = 0;

    virtual std::expected<void, RepositoryError> save(const UserPreference& preference) = 0;
};
```

映射规则：

1. 优先读取 `user_preferences`
2. 如果缺失，回退到 `users.base_currency_code` 并使用默认 locale、timezone、theme
3. 保存时同时更新 `user_preferences.base_currency_code`
4. 如需兼容旧查询，可同步更新 `users.base_currency_code`

### 2.7 审计仓储接口

```cpp
// domain/repositories/IAuditLogRepository.hpp
#pragma once
#include <expected>
#include <optional>
#include <string>
#include "domain/entities/AuditLog.hpp"
#include "domain/repositories/RepositoryError.hpp"

class IAuditLogRepository {
public:
    virtual ~IAuditLogRepository() = default;

    virtual std::expected<void, RepositoryError> record(const AuditLog& log) = 0;

    virtual std::expected<std::vector<AuditLog>, RepositoryError> findByResource(
        const std::string& resourceType,
        const std::string& resourceId
    ) = 0;
};
```

映射规则：

1. `resource_id` 使用字符串，兼容 BIGINT、UUID、外部 ID
2. `before_value`、`after_value`、`metadata` 从领域快照序列化为 JSONB
3. 审计写入失败时，高危操作必须整体失败并回滚
4. 普通事件处理中的审计失败必须进入日志和告警，不应静默吞掉

---

## 3. 基础设施层持久化实现 (Infrastructure/Persistence)

实现层位于 `infrastructure/persistence/`。为了充分利用 Drogon 框架的高性能异步特性，我们采用 Drogon 提供的 `drogon::orm::DbClient` 进行底层 SQL 操作，并在 Repository 实现中完成 **DTO（数据库行数据） $\leftrightarrow$ Domain Object（领域对象）** 的双向映射。

### 3.1 数据映射器示例 (Data Mapper)

映射器负责将非纯粹的数据库类型（如字符串、高精度字符串）转换为领域强类型。

```cpp
// infrastructure/persistence/mappers/AccountMapper.hpp
#pragma once
#include <drogon/orm/Result.h>
#include "domain/entities/Account.hpp"

class AccountMapper {
public:
    static Account toDomain(const drogon::orm::Row& row) {
        return Account(
            AccountId(row["id"].as<int64_t>()),
            UserId(row["user_id"].as<int64_t>()),
            row["name"].as<std::string>(),
            row["type"].as<std::string>(), // 内部映射为枚举
            Currency(row["currency_code"].as<std::string>()),
            row["is_archived"].as<bool>()
        );
    }
}

```

### 3.2 账户仓储的具体实现

这里展示如何利用 Drogon 的同步/协程客户端，在内部透明地处理 `account_balance_cache`。

```cpp
// infrastructure/persistence/AccountRepositoryImpl.cpp
#include "domain/repositories/IAccountRepository.hpp"
#include "domain/services/BalanceCalculationService.hpp"
#include "infrastructure/persistence/mappers/AccountMapper.hpp"
#include "infrastructure/persistence/mappers/TransactionMapper.hpp"
#include <drogon/drogon.h>

class AccountRepositoryImpl : public IAccountRepository {
private:
    drogon::orm::DbClientPtr dbClient_;
    BalanceCalculationService balanceCalculator_;

public:
    AccountRepositoryImpl(drogon::orm::DbClientPtr dbClient) : dbClient_(dbClient) {}

    std::expected<Account, RepositoryError> findById(AccountId id) override {
        try {
            auto result = dbClient_->execSqlSync("SELECT * FROM accounts WHERE id = $1", id.value());
            if (result.empty()) {
                return std::unexpected(RepositoryError{RepositoryStatus::NotFound, "Account not found"});
            }
            return AccountMapper::toDomain(result[0]);
        } catch (const std::exception& e) {
            return std::unexpected(RepositoryError{RepositoryStatus::DatabaseError, e.what()});
        }
    }

    // 核心落地：余额快照的缓存断接策略。
    // Repository 只负责缓存读取、流水加载、映射和写回；
    // Income/Expense/Transfer/Adjustment 的余额方向规则属于 BalanceCalculationService。
    std::expected<BalanceSnapshot, RepositoryError> balanceOf(AccountId id) override {
        try {
            // 1. 尝试从缓存表中读取
            auto cacheRes = dbClient_->execSqlSync(
                "SELECT balance, last_transaction_id, source_version, updated_at "
                "FROM account_balance_cache WHERE account_id = $1",
                id.value()
            );

            // 2. 查询该账户流水当前版本，用于判断缓存是否失效
            auto versionRes = dbClient_->execSqlSync(
                "SELECT COALESCE(MAX(version), 0) AS source_version, MAX(id) AS max_tx_id "
                "FROM transactions WHERE account_id = $1 AND deleted_at IS NULL",
                id.value()
            );

            int64_t sourceVersion = versionRes[0]["source_version"].as<int64_t>();
            int64_t maxTxId = versionRes[0]["max_tx_id"].isNull() ? 0 : versionRes[0]["max_tx_id"].as<int64_t>();

            if (!cacheRes.empty() && cacheRes[0]["source_version"].as<int64_t>() == sourceVersion) {
                auto row = cacheRes[0];
                return BalanceSnapshot(
                    Decimal(row["balance"].as<std::string>()), // 字符串转高精度定点数
                    TransactionId(row["last_transaction_id"].as<int64_t>()),
                    row["updated_at"].as<std::string>()
                );
            }

            // 3. 缓存未命中或失效：加载原始流水并交给 Domain Service 计算
            auto txRes = dbClient_->execSqlSync(
                "SELECT * FROM transactions "
                "WHERE account_id = $1 AND deleted_at IS NULL "
                "ORDER BY transaction_time ASC, id ASC",
                id.value()
            );

            std::vector<Transaction> transactions;
            transactions.reserve(txRes.size());
            for (const auto& row : txRes) {
                transactions.push_back(TransactionMapper::toDomain(row));
            }

            auto snapshot = balanceCalculator_.calculate(id, transactions);

            // 4. 将计算结果异步写入/更新缓存表，保证下次快速读取
            dbClient_->execSqlAsync(
                "INSERT INTO account_balance_cache (account_id, balance, last_transaction_id, source_version, cache_version, updated_at) "
                "VALUES ($1, $2, $3, $4, 1, NOW()) "
                "ON CONFLICT (account_id) DO UPDATE SET "
                "balance = $2, last_transaction_id = $3, source_version = $4, "
                "cache_version = account_balance_cache.cache_version + 1, updated_at = NOW()",
                [id](const drogon::orm::Result&) {}, // 异步回调消防后即忘
                [](const drogon::orm::DrogonDbException&) {},
                id.value(), snapshot.balance().to_string(), maxTxId, sourceVersion
            );

            return snapshot;

        } catch (const std::exception& e) {
            return std::unexpected(RepositoryError{RepositoryStatus::DatabaseError, e.what()});
        }
    }

    // 满足多态和接口的其他实现...
    std::expected<std::vector<Account>, RepositoryError> findActiveByUser(UserId userId) override { /* ... */ return {}; }
    std::expected<void, RepositoryError> save(const Account& account) override { /* ... */ return {}; }
    std::expected<void, RepositoryError> physicalDelete(AccountId id) override {
        dbClient_->execSqlSync("DELETE FROM accounts WHERE id = $1", id.value());
        return {};
    }
};

```

---

## 4. 工作单元与事务控制 (Unit of Work / Transaction Management)

由于持久化操作通常涉及多张表或多个聚合根的协同（例如：**跨币种转账**需要插入 `transfer_groups`、两笔 `transactions` 并扣减/增加两个账户的 `account_balance_cache` ），我们必须引入工作单元（Unit of Work）确保底层数据库事务的 ACID 特性，同时不让底层事务对象泄露到 Domain 层。

### 4.1 并发安全与死锁预防规约

在高并发写入事务（或多笔流水同时并发导入）时，若不加控制地更新 `account_balance_cache`，极易发生**死锁（Deadlock）**或**数据不一致**。

1. **悲观锁防死锁策略**：在涉及余额变动的事务中（如记账、转账、导入），必须在事务开始时对**聚合根（Account）**加锁。
   - **规则 1**：始终通过 `SELECT ... FOR UPDATE` 锁住 `accounts` 表，而不是直接锁缓存表。
   - **规则 2**：跨账户转账时，必须按 `account_id` 升序加锁。例如，账户 `3` 向账户 `1` 转账，加锁顺序必须是：先锁 `1`，再锁 `3`。这能从根本上消除死锁环路。
2. **缓存更新排他锁**：在 Repository 层通过数据库排他性语句更新，确保并发写入下的账目准确。

### 4.2 应用层事务接口

```cpp
// application/persistence/IUnitOfWork.hpp
#pragma once
#include <memory>
#include <functional>
#include <expected>
#include <vector>
#include "domain/events/IDomainEvent.hpp"
#include "domain/repositories/RepositoryError.hpp"

class IUnitOfWork {
public:
    virtual ~IUnitOfWork() = default;

    // 暂存要写入 outbox 的领域事件；实现必须是 request-scoped，不得跨请求共享
    virtual void registerEvent(std::shared_ptr<IDomainEvent> event) = 0;

    // 在一个独立的闭包中执行事务，闭包返回失败时自动回滚
    virtual std::expected<void, RepositoryError> executeInTransaction(
        std::function<std::expected<void, RepositoryError>()> action
    ) = 0;
};

```

### 4.3 基础设施层事务实现

利用 Drogon 的 `Transaction` 对象保持范围原子性。实现层必须是 request-scoped / 单次用例作用域，`pendingEvents_` 只允许存在于单个 `DrogonUnitOfWork` 实例内，不能被并发请求共享。

```cpp
// infrastructure/persistence/DrogonUnitOfWork.cpp
#include "application/persistence/IUnitOfWork.hpp"
#include <drogon/orm/DbClient.h>

class DrogonUnitOfWork : public IUnitOfWork {
private:
    drogon::orm::DbClientPtr dbClient_;
    std::vector<std::shared_ptr<IDomainEvent>> pendingEvents_;

public:
    DrogonUnitOfWork(drogon::orm::DbClientPtr dbClient) : dbClient_(dbClient) {}

    void registerEvent(std::shared_ptr<IDomainEvent> event) override {
        pendingEvents_.push_back(std::move(event));
    }

    std::expected<void, RepositoryError> executeInTransaction(
        std::function<std::expected<void, RepositoryError>()> action
    ) override {
        pendingEvents_.clear();
        // 创建 Drogon 底层的 Transaction 智能指针
        auto trans = dbClient_->newTransaction();

        auto result = action();

        if (result.has_value()) {
            try {
                // 同一数据库事务内先写 outbox，再提交业务事实
                for (const auto& event : pendingEvents_) {
                    auto payload = serializeDomainEvent(*event); // 序列化为 JSON
                    trans->execSqlSync(
                        "INSERT INTO domain_events_outbox (id, event_name, aggregate_type, aggregate_id, payload, status, retry_count, max_retry_count, next_retry_at, created_at) "
                        "VALUES ($1, $2, $3, $4, $5, 'pending', 0, 5, NOW(), NOW())",
                        generateOutboxId(), event->getEventName(), getAggregateType(*event), getAggregateId(*event), payload
                    );
                }

                trans->commit();
                pendingEvents_.clear();
                return {};
            } catch (const std::exception& e) {
                LOG_ERROR << "Transaction commit or outbox write failed: " << e.what();
                trans->rollback();
                pendingEvents_.clear();
                return std::unexpected(RepositoryError{RepositoryStatus::DatabaseError, "Outbox write or commit failed"});
            }
        } else {
            // 执行失败，回滚事务并丢弃暂存事件
            trans->rollback();
            pendingEvents_.clear();
            return std::unexpected(result.error());
        }
    }
};

```

说明：`serializeDomainEvent`、`getAggregateType`、`getAggregateId`、`generateOutboxId` 由基础设施层实现。它们的职责是把领域事件转换为 outbox 行，而不是把事件直接发布到订阅者。

---

## 5. 典型用例下的仓储及事务编排 (Use Case Assembly)

这里以 **“危险删除账户（Dangerous Delete）”** 的应用层 Use Case 为例，展示持久化层、工作单元与业务逻辑如何组装：

```cpp
// application/use_cases/DangerousDeleteAccountUseCase.cpp
#include "domain/repositories/IAccountRepository.hpp"
#include "domain/repositories/ITransactionRepository.hpp"
#include "application/persistence/IUnitOfWork.hpp"

class DangerousDeleteAccountUseCase {
private:
    std::shared_ptr<IAccountRepository> accountRepo_;
    std::shared_ptr<ITransactionRepository> txRepo_;
    std::shared_ptr<IUnitOfWork> uow_;

public:
    DangerousDeleteAccountUseCase(
        std::shared_ptr<IAccountRepository> accountRepo,
        std::shared_ptr<ITransactionRepository> txRepo,
        std::shared_ptr<IUnitOfWork> uow)
        : accountRepo_(accountRepo), txRepo_(txRepo), uow_(uow) {}

    std::expected<void, RepositoryError> execute(UserId userId, AccountId accountId, int confirmationCount) {
        // 1. 三次确认校验
        if (confirmationCount < 3) {
            return std::unexpected(RepositoryError{RepositoryStatus::ValidationError, "Dangerous action requires 3 confirmations"});
        }

        // 2. 权限校验：确认账户确实属于该用户
        auto accountOpt = accountRepo_->findById(accountId);
        if (!accountOpt || accountOpt->getUserId() != userId) {
            return std::unexpected(RepositoryError{RepositoryStatus::NotFound, "Account verification failed"});
        }

        // 3. 编排事务，按严格顺序执行硬删除
        return uow_->executeInTransaction([&]() -> std::expected<void, RepositoryError> {

            // 步骤 A: 删除该账户下的所有关联流水 (物理删除)
            auto delTxRes = txRepo_->physicalDeleteByAccount(accountId);
            if (!delTxRes) return delTxRes;

            // 步骤 B: 物理删除账户本体（由于没有 ON DELETE CASCADE，必须后删）
            auto delAccRes = accountRepo_->physicalDelete(accountId);
            if (!delAccRes) return delAccRes;

            // 提示：account_balance_cache 表是以 account_id 作为外键的
            // 可以在 DB 设计中对 cache 表设为 ON DELETE CASCADE，
            // 或者在这里显式调用 db 清理。推荐在底层物理删除 Account 时由 DB 触发器或本事务内一并清除。

            return {};
        });
    }
};

```
