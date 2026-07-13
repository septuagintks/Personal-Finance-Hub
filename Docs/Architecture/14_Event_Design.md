# Personal Finance Hub - Event System Design

Version: 1.0

Backend: C++23

Architecture: Clean Architecture (Event-Driven)

---

## 1. 架构定位：为什么要引入领域事件？

领域事件用于捕获我们在领域模型中发生的具有业务意义的事情。

**核心设计原则：**

1. **解耦非关键副作用（Decoupling Side Effects）**：核心 Use Case 负责主业务，以及必须与业务事实原子提交的同步 AuditLog；通知、报表缓存失效和补充型运维记录等非关键副作用由事件处理器接管。
2. **事务外发箱（Transactional Outbox）**：业务事务内只写业务事实和 outbox 记录，绝不在提交前直接对外派发事件。只有当同一数据库事务中的 outbox 记录与业务数据一起成功提交后，事件才具备可投递资格。
3. **进程内总线（In-Process Bus）**：目前阶段不引入 Kafka 或 RabbitMQ 等沉重的外部消息中间件，仍然使用 C++23 在进程内实现轻量级 Pub/Sub；但它只负责消费 outbox 已提交事件，不参与业务事务提交路径。

---

## 2. 领域层事件定义 (Domain Events)

领域事件属于 `Domain` 层。它们是纯粹的值对象（Value Objects），过去时态命名，一旦产生就不可变。

### 2.1 基础事件接口

```cpp
// domain/events/IDomainEvent.hpp
#pragma once
#include <string>
#include <chrono>

struct IDomainEvent {
    virtual ~IDomainEvent() = default;

    virtual std::string getEventName() const = 0;
    virtual std::chrono::system_clock::time_point getOccurredAt() const = 0;
};

```

### 2.2 具体事件案例

事件中只携带必要的 ID 和最小化上下文，不要把整个 Entity 塞进去。

### 2.3 必备事件清单

| Event                     | 必备字段                                                              | 触发来源                                                   | 典型订阅者                                                 |
| ------------------------- | --------------------------------------------------------------------- | ---------------------------------------------------------- | ---------------------------------------------------------- |
| TransactionCreated        | userId, transactionId, accountId, occurredAt                          | CreateTransactionUseCase                                   | BalanceCacheInvalidator, ReportCacheInvalidator            |
| TransactionDeleted        | userId, transactionId, accountId, occurredAt                          | DeleteTransactionUseCase                                   | BalanceCacheInvalidator, ReportCacheInvalidator            |
| TransferCompleted         | userId, transferGroupId, sourceAccountId, targetAccountId, occurredAt | CreateTransferUseCase                                      | BalanceCacheInvalidator, ReportCacheInvalidator            |
| AccountArchived           | userId, accountId, occurredAt                                         | ArchiveAccountUseCase                                      | DashboardCacheInvalidator                                  |
| AccountDangerouslyDeleted | userId, accountId, occurredAt                                         | DangerousDeleteAccountUseCase                              | SecurityNotificationHandler                                |
| CategoryCreated           | userId, categoryId, board, occurredAt                                 | CreateCategoryUseCase / InitializeDefaultCategoriesUseCase | CategoryTreeCacheInvalidator                               |
| CategoryDeleted           | userId, categoryId, board, occurredAt                                 | DeleteCategoryUseCase                                      | CategoryTreeCacheInvalidator, ReportCacheInvalidator       |
| ExchangeRateRefreshed     | provider, baseCurrency, targetCurrency, fetchedAt                     | RefreshExchangeRatesUseCase                                | LatestRateCacheInvalidator, AuditLogHandler                |
| ExchangeRateRefreshFailed | provider, baseCurrency, historicalAvailable, reason, occurredAt       | RefreshExchangeRatesUseCase                                | AuditLogHandler, AlertHandler                              |
| SyncCompleted             | userId, provider, syncJobId, importedCount, skippedCount, occurredAt  | RunSyncJobUseCase                                          | ReconciliationJob, AuditLogHandler                         |
| UserPreferenceUpdated     | userId, occurredAt                                                    | UpdateUserPreferenceUseCase                                | FrontendPreferenceCacheInvalidator, ReportCacheInvalidator |
| AuditLogRecorded          | auditLogId, operatorUserId, action, resourceType, occurredAt          | AuditLogHandler                                            | SecurityNotificationHandler                                |

命名规则：

1. 事件名使用过去时态
2. 事件只描述已经成功发生的事实
3. 事件不得承担命令语义，例如不要命名为 `RefreshDashboard`
4. 事件 payload 只携带 ID、时间和少量摘要字段
5. 事件必须在数据库事务成功提交后派发

`ExchangeRateRefreshed` 按成功返回的币种对逐条登记，不使用批次计数替代
`targetCurrency`。`ExchangeRateRefreshFailed.historicalAvailable` 只有在本次请求的
全部目标币种对均存在历史汇率时才为 `true`。

### 2.4 AuditLog 与事件处理器边界

以下审计属于业务事实的一部分，必须由 Use Case 通过 `IAuditLogRepository` 在同一
事务内同步写入：认证生命周期，以及账户、流水、分类、标签、偏好等用户发起的
关键资源变更。业务写入、AuditLog 和 outbox 任一失败时，整个事务回滚。

`AuditLogHandler` 只允许记录没有同步业务审计的系统事件、投递失败/dead-letter、
安全告警处置等补充事实。它不得根据 `TransactionDeleted`、`AccountArchived`、
`AccountDangerouslyDeleted` 等事件重复写入同一业务动作的 AuditLog。补充审计必须
使用 `outbox_id + handler_name`（或等价键）保证幂等。

```cpp
// domain/events/TransferCompletedEvent.hpp
#pragma once
#include "domain/events/IDomainEvent.hpp"
#include "domain/entities/Account.hpp" // 仅需 AccountId
#include "domain/value_objects/StrongId.hpp" // 需 TransferGroupId

struct TransferCompletedEvent : public IDomainEvent {
    AccountId sourceAccountId;
    AccountId targetAccountId;
    TransferGroupId transferGroupId;
    std::chrono::system_clock::time_point occurredAt;

    TransferCompletedEvent(AccountId src, AccountId tgt, TransferGroupId groupId)
        : sourceAccountId(src), targetAccountId(tgt), transferGroupId(groupId),
          occurredAt(std::chrono::system_clock::now()) {}

    std::string getEventName() const override { return "TransferCompleted"; }
    std::chrono::system_clock::time_point getOccurredAt() const override { return occurredAt; }
};

```

```cpp
// domain/events/TransactionCreatedEvent.hpp
#pragma once
#include "domain/events/IDomainEvent.hpp"
#include "domain/entities/Account.hpp" // 仅需 AccountId
#include "domain/entities/Transaction.hpp" // 仅需 TransactionId
#include "domain/value_objects/StrongId.hpp" // 仅需 UserId

struct TransactionCreatedEvent : public IDomainEvent {
    UserId userId;
    TransactionId transactionId;
    AccountId accountId;
    std::chrono::system_clock::time_point occurredAt;

    TransactionCreatedEvent(UserId uid, TransactionId txId, AccountId accId)
        : userId(uid), transactionId(txId), accountId(accId),
          occurredAt(std::chrono::system_clock::now()) {}

    std::string getEventName() const override { return "TransactionCreated"; }
    std::chrono::system_clock::time_point getOccurredAt() const override { return occurredAt; }
};

```

```cpp
// domain/events/AccountDangerouslyDeletedEvent.hpp
#pragma once
#include "domain/events/IDomainEvent.hpp"
#include "domain/value_objects/StrongId.hpp" // 仅需 AccountId / UserId

struct AccountDangerouslyDeletedEvent : public IDomainEvent {
    AccountId accountId;
    UserId userId;
    std::chrono::system_clock::time_point occurredAt;

    // 构造函数和接口实现...
};

```

---

## 3. 基础设施层：轻量级事件总线 (Event Bus)

在 `infrastructure/events/` 目录下，利用现代 C++ 的 `std::type_index` 和 `std::function` 实现一个强类型的本地事件总线。它是 outbox 消费侧的最终分发器，而不是业务写路径的一部分。

### 3.1 事件总线接口与实现

```cpp
// application/events/IEventBus.hpp
#pragma once
#include <memory>
#include "domain/events/IDomainEvent.hpp"

class IEventBus {
public:
    virtual ~IEventBus() = default;
    virtual void publish(const std::shared_ptr<IDomainEvent>& event) = 0;
};

```

```cpp
// infrastructure/events/LocalEventBus.hpp
#pragma once
#include "application/events/IEventBus.hpp"
#include <unordered_map>
#include <vector>
#include <functional>
#include <typeindex>
#include <mutex>
#include <drogon/drogon.h>

class LocalEventBus : public IEventBus {
private:
    // 映射表：Event 类型 -> 处理器回调函数列表
    std::unordered_map<std::type_index, std::vector<std::function<void(const std::shared_ptr<IDomainEvent>&)>>> handlers_;
    std::mutex mutex_;
    std::shared_ptr<IBackgroundExecutor> backgroundExecutor_;

public:
    explicit LocalEventBus(std::shared_ptr<IBackgroundExecutor> backgroundExecutor)
        : backgroundExecutor_(std::move(backgroundExecutor)) {}

    // 注册订阅者
    template<typename EventType, typename HandlerFunc>
    void subscribe(HandlerFunc&& handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_[typeid(EventType)].push_back([handler](const std::shared_ptr<IDomainEvent>& ev) {
            // 安全的向下转型
            handler(std::static_pointer_cast<EventType>(ev));
        });
    }

    // 派发已经从 outbox 取出的事件
    void publish(const std::shared_ptr<IDomainEvent>& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(typeid(*event));
        if (it != handlers_.end()) {
            for (const auto& handler : it->second) {
                // 将处理器扔进后台执行器，避免阻塞 outbox publisher 或 Event Loop
                backgroundExecutor_->submit([handler, event]() {
                    try {
                        handler(event);
                    } catch (const std::exception& e) {
                        LOG_ERROR << "Event handler crashed: " << e.what();
                    }
                });
            }
        }
    }
};

```

---

## 4. 事务整合：保证一致性 (Transactional Outbox)

正如之前强调的，事件不能在 Use Case 执行一半的时候发出去。我们需要改造《05_Repository_and_Persistence_Design.md》中设计的 `IUnitOfWork`，让它在同一个数据库事务里同时写业务表和 outbox 表。

### 4.1 工作单元聚合事件

我们在 `IUnitOfWork` 中增加事件收集的功能。在实体/聚合根处理时，把产生的事件“暂存”在 UoW 中。这个 UoW 必须是 request-scoped / 单次事务作用域，不能跨请求共享。

```cpp
// application/persistence/IUnitOfWork.hpp (改造后)
#pragma once
#include <functional>
#include <expected>
#include <vector>
#include <memory>
#include "domain/repositories/RepositoryError.hpp"
#include "domain/events/IDomainEvent.hpp"

class ITransactionContext {
public:
    virtual ~ITransactionContext() = default;
};

class IUnitOfWork {
public:
    virtual ~IUnitOfWork() = default;

    // 暂存要写入 outbox 的事件
    virtual void registerEvent(std::shared_ptr<IDomainEvent> event) = 0;

    // 执行事务；闭包内 Repository 必须使用同一个事务上下文
    virtual std::expected<void, RepositoryError> executeInTransaction(
        std::function<std::expected<void, RepositoryError>(ITransactionContext& tx)> action
    ) = 0;
};

```

### 4.2 事务完成后的自动派发与异步解耦

在基础设施层（Drogon UoW 实现）拦截 Commit 状态。必须确保业务写入和 outbox 写入共处一个数据库事务，而不是仅仅在 `action()` 返回 `has_value()` 时就对外发布。

提交成功后的事件投递由后台 `OutboxPublisherJob` 负责，它从 outbox 扫描待投递事件，再通过 `IEventBus` 分发给本地处理器。这样既避免了主业务线程阻塞，也保证了事务一致性。

1. **本地消息表（Outbox Pattern）**：
   - 为了保证在进程崩溃、网络抖动或发布器重启时事件“至少一次投递（At-Least-Once Delivery）”，系统使用本地消息表 `domain_events_outbox` 作为事实来源。
   - 在 `DrogonUnitOfWork` 事务闭包中，将事件序列化为 JSON，并与业务数据在**同一个数据库事务**中写入 `domain_events_outbox` 表。
   - 业务事务 Commit 成功后，不直接调用 handler；由 Scheduler 中的 `OutboxPublisherJob` 扫描 `pending/failed` 事件并投递到 `IEventBus`。
2. **处理器分层**：
   - `IEventBus` 内部仍可使用 Drogon 的线程池或自定义工作线程池，以避免单个 handler 阻塞 outbox publisher。
   - 处理器可按优先级区分快速本地处理（如缓存失效）和后台处理（如安全通知），但二者都发生在业务事务提交之后。
   - 业务 AuditLog 不由 post-commit handler 补写；只有 2.4 节定义的补充审计可以进入 `AuditLogHandler`。
   - 所有 handler 都必须幂等，因为 outbox 允许重试。

```cpp
// infrastructure/persistence/DrogonUnitOfWork.cpp (改造片段)
#include "domain/events/IDomainEvent.hpp"

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
        pendingEvents_.clear();
        return std::unexpected(result.error());
    }

    auto outbox = writeOutbox(context, pendingEvents_);
    if (!outbox) {
        transaction->rollback();
        pendingEvents_.clear();
        return std::unexpected(outbox.error());
    }
}

transaction.reset(); // Drogon commits when the last owner is released
pendingEvents_.clear();
if (!completion.get()) {
    return std::unexpected(RepositoryError::database("commit failed"));
}
return {};
```

## 说明：`serializeDomainEvent`、`generateOutboxId`、`getAggregateType`、`getAggregateId` 由基础设施层实现。它们只负责把领域事件转换为 outbox 行，不负责真正的 handler 执行。

## 5. 业务用例中的应用：危险删除

让我们回顾《07_Workflow_and_Lifecycle_Design.md》中的危险删除流程。加入事件机制后，Use Case 变得非常干净。

```cpp
// application/use_cases/DangerousDeleteAccountUseCase.cpp
// ... 之前的权限与校验逻辑 ...

return uow_->executeInTransaction([&](ITransactionContext& tx) -> std::expected<void, RepositoryError> {

    auto auditRes = auditRepo_->append(tx, makeDangerousDeleteAudit(
        userId, accountId, confirmations
    ));
    if (!auditRes) return auditRes;

    auto delTxRes = txRepo_->physicalDeleteByAccount(tx, accountId);
    if (!delTxRes) return delTxRes;

    auto delCacheRes = accountRepo_->deleteBalanceCache(tx, accountId);
    if (!delCacheRes) return delCacheRes;

    auto delAccRes = accountRepo_->physicalDelete(tx, accountId);
    if (!delAccRes) return delAccRes;

    // AuditLog 已与删除事实处于同一事务；事件只承载提交后的副作用。
    uow_->registerEvent(std::make_shared<AccountDangerouslyDeletedEvent>(
        accountId, userId
    ));

    return {};
});

```

### 5.1 独立的事件处理器 (Event Handlers)

在系统启动时（例如在 `main.cpp` 中），我们将处理器注册到总线上。这些处理器由 `OutboxPublisherJob` 在成功 claim outbox 事件后触发。

```cpp
// 业务审计已同步提交；事件处理器只负责安全通知，不重复写 AuditLog。
eventBus->subscribe<AccountDangerouslyDeletedEvent>([emailService](const auto& ev) {
    emailService->sendWarningEmail(ev->userId, "您刚刚彻底删除了一个账户及其所有流水。");
});

```

---

## 6. Outbox 投递工作流

当前的标准实现就是 Outbox Pattern。`LocalEventBus` 只是最终的本地分发器；真正的可靠性由 `domain_events_outbox` 和 `OutboxPublisherJob` 保证。

1. 在 `transactions` 等核心表所在同一个 PostgreSQL 库里建一张表 `domain_events_outbox`。
2. 在 `executeInTransaction` 的同一个事务里，把要派发的事件作为 JSON 插入到 `domain_events_outbox`。
3. 在《12_Scheduler_Design.md》中定义的 `OutboxPublisherJob` 定时轮询这张表，逐条 claim、投递、更新状态。
4. `OutboxPublisherJob` 成功投递后，再由 `IEventBus` 将事件分发给本地 handler。

由于 outbox 允许重试，同一事件可能被投递多次，因此所有 handler 必须幂等。

### 6.1 Outbox 表结构

```sql
CREATE TYPE outbox_status AS ENUM (
    'pending',
    'processing',
    'published',
    'failed',
    'dead_letter'
);

CREATE TABLE domain_events_outbox (
id UUID PRIMARY KEY,
event_name VARCHAR(128) NOT NULL,
aggregate_type VARCHAR(64),
aggregate_id VARCHAR(128),
payload JSONB NOT NULL,
status outbox_status NOT NULL DEFAULT 'pending',
retry_count INT NOT NULL DEFAULT 0,
max_retry_count INT NOT NULL DEFAULT 5,
next_retry_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
last_error TEXT,
occurred_at TIMESTAMPTZ NOT NULL,
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
published_at TIMESTAMPTZ
);

CREATE INDEX idx_outbox_pending
ON domain_events_outbox(status, next_retry_at)
WHERE status IN ('pending', 'failed');
```

### 6.2 写入规则

1. Use Case 在同一个业务事务内写入业务表和 `domain_events_outbox`
2. 事务回滚时，业务数据和事件都回滚
3. 事务提交后，由 `OutboxPublisherJob` 派发事件
4. Event payload 必须只包含 ID、时间和最小摘要
5. Event handler 必须幂等，允许同一事件被重试处理

### 6.3 重试策略

Worker 查询：

```sql
SELECT *
FROM domain_events_outbox
WHERE status IN ('pending', 'failed')
  AND next_retry_at <= NOW()
ORDER BY created_at ASC
LIMIT 100
FOR UPDATE SKIP LOCKED;
```

处理规则：

1. 取到事件后设置 `status = 'processing'`
2. 处理成功后设置 `status = 'published'` 和 `published_at`
3. 处理失败后递增 `retry_count`
4. `next_retry_at` 使用指数退避，例如 1m、5m、15m、1h、6h
5. 超过 `max_retry_count` 后设置 `dead_letter`

### 6.4 失败审计

事件处理失败必须记录：

- `event_name`
- `outbox_id`
- `handler_name`
- `retry_count`
- `last_error`
- `failed_at`

高危事件进入 `dead_letter` 时必须写入 AuditLog，例如：

```text
action = update
resource_type = DomainEventOutbox
resource_id = outbox_id
metadata = { "status": "dead_letter", "eventName": "AccountDangerouslyDeleted" }
```

规则：

1. Outbox 失败不得修改已经提交的业务事实
2. 缓存失效类事件失败可以重试
3. 安全通知、审计补充类事件失败必须进入告警
4. Worker 重启后必须能继续处理 pending/failed 事件
