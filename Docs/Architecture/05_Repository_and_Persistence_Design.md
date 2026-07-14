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
3. **隐式缓存策略**：业务层（Domain/Application）不感知缓存的存在。Repository 内部负责维护 `account_balance_cache` 的命中检查和重建，对外只返回标准的领域对象。
4. **强类型、事件与错误处理**：使用 C++23 的强类型 ID（如 `AccountId`）防止参数传错。对于仓储查找失败或数据库异常，使用 `std::expected` 传递中性错误，不滥用异常（Exceptions），保持控制流清晰。Repository Error 只表达“数据访问结果”，不携带 Drogon、SQL 或连接池等实现细节；同一个 UnitOfWork 事务还负责把领域事件写入 `domain_events_outbox`，不在提交前直接对外派发。
5. **Use Case 与 Domain Service 分层**：Application 层只使用 `application/use_cases/` 下的具体 Use Case 进行事务和仓储编排；Domain 层只使用 `domain/services/` 下的纯业务规则服务。不要定义跨层同名的 `AccountingService`、`RefreshExchangeRatesUseCase` 或 `ReportService`。
6. **领域概念不强制等同表结构**：`UserPreference` 是 Domain Concept，拥有独立的 `user_preferences` 表，由 `IUserRepository` 联合查询或通过独立的 `IUserPreferenceRepository` 进行读写。`TransferGroup` 则相反，只是 `transfer_groups` 持久化元数据载体，不是 Domain Entity。
7. **Read Model 边界**：`BalanceSnapshot` 是 Value Object，也是账户余额查询的 Read Model。它没有独立身份或生命周期，Repository 可以从 `account_balance_cache` 或流水聚合映射得到。
8. **辅助实体仓储边界**：`Category`、`Tag`、`AuditLog`、`UserPreference` 虽不是资金事实来源，但它们是前端工作流和审计闭环的必要数据，必须有明确 Repository 接口。
9. **RLS 必须绑定固定事务**：租户 Repository 是 request-scoped，并持有认证后的 `UserId`。每次读取 RLS 表也必须创建并固定一个 Drogon `Transaction`，先执行事务级 `SET LOCAL app.current_user_id`，再在同一 Transaction 上执行全部查询。禁止先对池化 `DbClient` 设置 GUC、再假设下一条语句仍使用同一物理连接。
10. **以提交回调确认成功**：Drogon `Transaction` 没有供业务代码直接调用的 `commit()`。成功路径应释放最后一个 Transaction owner 触发提交，并以 `newTransaction` / `setCommitCallback` 的布尔结果作为提交是否成功的依据；禁止通过 `execSqlSync("COMMIT")` 绕过 Drogon 生命周期。
11. **事务内读取必须显式传递上下文**：需要 read-your-writes 的 User/UserPreference 等流程必须调用接收 `ITransactionContext&` 的 Repository 重载；无上下文读取会创建独立短事务，不能用于观察当前 UoW 尚未提交的写入。
12. **注册 tenant 只能绑定一次**：注册使用未绑定 tenant 的 bootstrap UoW，只允许先访问 `users` 和系统模板。User INSERT 返回新 ID 后，必须在同一 Transaction 上调用 `bind_tenant_once(newUserId)`，由它执行 `SET LOCAL`；重复绑定、切换 ID 或在绑定前访问租户表均失败。不得拆成“创建用户一次提交 + 默认数据另一次提交”。
13. **请求作用域由 Application 定义**：`IRequestScopeFactory::create(UserId)` 每次返回独立 scope；scope 暴露 Account、Transaction、Category、Tag、Preference、ExchangeRate、AuditLog Repository 与同租户 Unit of Work。Presentation 只能调用 `FinanceApplicationService`，不得包含 Infrastructure 配置或自行 new Repository。

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

class ITransactionContext;

class IAccountRepository {
public:
    virtual ~IAccountRepository() = default;

    // 获取账户基础信息（不含余额）
    virtual std::expected<Account, RepositoryError> findById(AccountId id) = 0;

    // 获取用户下的所有未归档账户
    virtual std::expected<std::vector<Account>, RepositoryError> findActiveByUser(UserId userId) = 0;

    // 核心：获取账户的实时余额快照（Repository 内部处理流水聚合或缓存读取）
    virtual std::expected<BalanceSnapshot, RepositoryError> balanceOf(AccountId id) = 0;

    // 保存或更新账户基础状态（如重命名、归档）
    virtual std::expected<void, RepositoryError> save(const Account& account) = 0;

    // 危险删除辅助：显式清理账户余额缓存
    virtual std::expected<void, RepositoryError> deleteBalanceCache(ITransactionContext& tx, AccountId id) = 0;

    // 危险删除：物理删除/彻底清除账户基础记录（流水清理由 UnitOfWork 编排）
    virtual std::expected<void, RepositoryError> physicalDelete(ITransactionContext& tx, AccountId id) = 0;
};

```

系统级“活跃币种”查询不属于租户账户仓储。它必须合并所有未归档账户币种与 `users.base_currency_code`；否则用户把报表基准币种设为一个没有对应账户的币种时，USD 枢纽刷新会缺少该侧汇率。后台任务不携带 `UserId`，而 `accounts` 已启用 FORCE RLS；若把该方法留在 request-scoped `IAccountRepository`，它只能看到单个租户或在未设置 GUC 时返回空集合。因此 Application 单独定义 `IActiveCurrencyQuery::list_active_currencies()` 端口：

- In-Memory adapter 可直接遍历全部未归档账户。
- PostgreSQL adapter 必须使用独立的后台只读 DbClient/数据库角色，该角色具备跨租户读取所需权限。
- request-serving DbClient 不得复用该特权角色，`PostgresActiveCurrencyQuery` 也不得注入 Controller 或普通用户 Use Case。
- 角色、连接与最小权限的实际装配在 P1-S10-04 完成，并在 S12 验证 request 连接无法绕过 RLS。

### 2.3 转账与流水仓储接口

Phase 1 的 `transactions` 采用追加 + 软删除模型：创建后不提供普通字段更新，因此不增加行级 `version`，也不对流水本身执行乐观锁更新。并发一致性由应用层事务、账户行锁、`Account.version` 和转账聚合原子写入保证；若未来增加流水编辑能力，必须先引入独立修订模型或为 Transaction 增加版本字段，不得静默覆盖。

```cpp
// domain/repositories/ITransactionRepository.hpp
#pragma once
#include <vector>
#include <expected>
#include "domain/aggregates/TransferAggregate.hpp"
#include "domain/entities/Transaction.hpp"
#include "domain/value_objects/StrongId.hpp"
#include "domain/value_objects/DateRange.hpp"

class ITransactionContext;

class ITransactionRepository {
public:
    virtual ~ITransactionRepository() = default;

    virtual RepositoryResult<Transaction> save_single(
        ITransactionContext& tx,
        const Transaction& transaction) = 0;

    virtual RepositoryResult<TransferPersistResult> save_transfer(
        ITransactionContext& tx,
        const TransferAggregate& transfer) = 0;

    virtual RepositoryResult<std::vector<Transaction>> find_by_account(
        AccountId account_id,
        std::optional<TimePoint> from,
        std::optional<TimePoint> to,
        bool include_deleted = false) = 0;

    virtual RepositoryResult<std::vector<Transaction>> find_by_user(
        UserId user_id,
        bool include_deleted = false) = 0;

    virtual RepositoryResult<TransferSnapshot> find_transfer_by_group(
        TransferGroupId group_id,
        UserId user_id) = 0;

    virtual RepositoryVoidResult soft_delete(
        ITransactionContext& tx,
        TransactionId id,
        UserId user_id,
        TimePoint deleted_at) = 0;
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

    virtual RepositoryResult<Category> find_by_id_for_user(
        CategoryId id, UserId user_id) = 0;
    virtual RepositoryResult<Category> find_by_id_for_user_including_deleted(
        CategoryId id, UserId user_id) = 0;
    virtual RepositoryResult<Category> find_by_id_for_user_for_update(
        ITransactionContext& tx, CategoryId id, UserId user_id) = 0;
    virtual RepositoryResult<std::vector<Category>> find_by_board(
        UserId user_id, CategoryBoard board) = 0;
    virtual RepositoryResult<std::vector<Category>> find_all_for_user(
        UserId user_id) = 0;
    virtual RepositoryResult<CategoryId> resolve_root_id_for_user(
        CategoryId id, UserId user_id) = 0;
    virtual RepositoryResult<CategoryId> save(
        ITransactionContext& tx, const Category& category) = 0;
    virtual RepositoryVoidResult soft_delete(
        ITransactionContext& tx, CategoryId id, UserId user_id,
        TimePoint deleted_at) = 0;
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

    virtual RepositoryResult<std::vector<Tag>> find_by_user(
        UserId user_id, bool include_deleted = false) = 0;
    virtual RepositoryResult<Tag> find_by_id_for_user_for_update(
        ITransactionContext& tx, TagId tag_id, UserId user_id) = 0;
    virtual RepositoryResult<TagId> save(
        ITransactionContext& tx, const Tag& tag) = 0;
    virtual RepositoryVoidResult soft_delete(
        ITransactionContext& tx, TagId tag_id, UserId user_id,
        TimePoint deleted_at) = 0;
    virtual RepositoryResult<std::vector<Tag>> replace_transaction_tags(
        ITransactionContext& tx, TransactionId transaction_id,
        UserId user_id, const std::vector<TagId>& tag_ids) = 0;
};
```

映射规则：

1. 同一用户下 Tag 名称唯一
2. Tag 软删除后，历史流水关系保留
3. `replace_transaction_tags` 必须锁定流水、校验流水和全部 Tag 属于同一用户，并在同一事务替换完整关系集合
4. 删除 Tag 前必须使用 `find_by_id_for_user_for_update` 在当前 UoW 中取得审计快照，禁止提交前跨事务重读

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

账户余额读取是“带缓存写回的读路径”，必须把账户、流水版本、缓存命中判断、余额重建和缓存 UPSERT 放在同一个租户事务中。当前实现遵守以下顺序：

1. 使用 request-scoped `tenant_user_id` 创建短事务，并先执行 `SET LOCAL`。
2. 先锁定账户聚合根，再在同一事务中读取 `MAX(transactions.version)`、最新流水 ID 和缓存行；流水新增、软删除与物理删除也必须遵循账户先行锁定约束。
3. 仅当 `source_version` 与最新流水 ID 同时匹配时命中缓存。
4. miss/stale 时加载未删除流水，交给纯领域 `BalanceCalculationService` 重建。
5. 同事务 UPSERT `account_balance_cache`；流水新增、软删除和聚合物理删除也必须在各自写事务内删除受影响账户的缓存。
6. `source_version` 使用 schema 的流水 `version` 语义，不得使用 In-Memory 的“未删除流水数量”替代。

```cpp
return postgres::execute_transaction<BalanceSnapshot>(
    dbClient_, tenantUserId_, "read account balance",
    [&](const auto& transaction) -> RepositoryResult<BalanceSnapshot> {
        // account/cache/version/transactions all use this transaction.
        // NUMERIC values are read as strings and mapped to Decimal.
        // Rebuild delegates only financial arithmetic to the Domain Service.
        // Cache UPSERT includes account_id, user_id, source_version and last tx id.
        return rebuiltOrCachedSnapshot;
    });

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

class ITransactionContext {
public:
    virtual ~ITransactionContext() = default;
};

class IUnitOfWork {
public:
    virtual ~IUnitOfWork() = default;

    // 暂存要写入 outbox 的领域事件；实现必须是 request-scoped，不得跨请求共享
    virtual void registerEvent(std::shared_ptr<IDomainEvent> event) = 0;

    // 在一个独立的闭包中执行事务，闭包内 Repository 必须使用同一个事务上下文
    virtual std::expected<void, RepositoryError> executeInTransaction(
        std::function<std::expected<void, RepositoryError>(ITransactionContext& tx)> action
    ) = 0;
};

```

### 4.3 基础设施层事务实现

利用 Drogon 的 `Transaction` 对象保持范围原子性。实现层必须是 request-scoped / 单次用例作用域，`pendingEvents_` 只允许存在于单个 `DrogonUnitOfWork` 实例内，不能被并发请求共享。

事务闭包必须接收一个事务上下文（例如 `ITransactionContext& tx`），并把它传给参与写入的 Repository。这样业务表写入、缓存更新和 outbox 写入才能共享同一个底层数据库事务；不得在 UoW 中创建 `trans` 后，让 Repository 继续使用普通 `DbClient` 在事务外写库。

Drogon 的提交由 `Transaction` 生命周期驱动。实现必须在释放最后一个 owner 前完成业务写入与 outbox 写入，并等待 commit callback 返回 `true` 后才向 Application 报告成功。回滚或提交失败时清空本次 `pendingEvents_`；`SET LOCAL` 会随事务结束自动清除，不允许在已提交/已回滚的 Transaction 上再执行 RESET。

注册是唯一允许 UoW 从未绑定状态开始的用户写流程。`IBootstrapUnitOfWork::execute_bootstrap_transaction` 提供 `ITenantBootstrapTransaction`；User 创建成功后只允许绑定一次 tenant，再写 Preference、Category、refresh token、同步 AuditLog 和 outbox。普通 request UoW 仍必须在进入闭包前由 JWT `sub` 预绑定 tenant；后台 UoW 不得调用任何租户 Repository。

无租户不等于使用特权数据库角色。汇率 append、Outbox 状态、系统 AuditLog、认证清理和 scheduled lease 均通过普通 request-role DbClient 的无租户事务访问非 RLS 表；BYPASSRLS/default-read-only client 只允许注入显式批准的跨租户读取端口。

```cpp
auto committed = std::make_shared<std::promise<bool>>();
auto completion = committed->get_future();
auto transaction = dbClient_->newTransaction(
    [committed](bool ok) { committed->set_value(ok); });

RlsSession::setAppUserId(transaction, tenantUserId);
{
    DrogonTransactionContext context(transaction, tenantUserId);
    auto result = action(context);
    if (!result) {
        transaction->rollback();
        return std::unexpected(result.error());
    }
    auto outbox = writeOutbox(context, pendingEvents_); // same Transaction
    if (!outbox) {
        transaction->rollback();
        return std::unexpected(outbox.error());
    }
}

transaction.reset(); // release last owner and let Drogon commit
if (!completion.get()) {
    return std::unexpected(RepositoryError::database("commit failed"));
}
return {};

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
        return uow_->executeInTransaction([&](ITransactionContext& tx) -> std::expected<void, RepositoryError> {

            // 步骤 A: 删除该账户下的所有关联流水 (物理删除)
            auto delTxRes = txRepo_->physicalDeleteByAccount(tx, accountId);
            if (!delTxRes) return delTxRes;

            // 步骤 B: 显式清理余额缓存。数据库层不使用 ON DELETE CASCADE。
            auto delCacheRes = accountRepo_->deleteBalanceCache(tx, accountId);
            if (!delCacheRes) return delCacheRes;

            // 步骤 C: 物理删除账户本体（由于没有 ON DELETE CASCADE，必须后删）
            auto delAccRes = accountRepo_->physicalDelete(tx, accountId);
            if (!delAccRes) return delAccRes;

            return {};
        });
    }
};

```
