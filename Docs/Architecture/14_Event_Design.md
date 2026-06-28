# Personal Finance Hub - Event System Design

Version: 1.0

Backend: C++23

Architecture: Clean Architecture (Event-Driven)

---

## 1. 架构定位：为什么要引入领域事件？

领域事件用于捕获我们在领域模型中发生的具有业务意义的事情。

**核心设计原则：**

1. **解耦副作用（Decoupling Side Effects）**：核心 Use Case 只负责主业务（如扣款），后续的附属操作（如刷新统计缓存、写日志）由事件处理器（Event Handlers）异步或同步接管。
2. **纯内存总线（In-Process Bus）**：目前阶段不引入 Kafka 或 RabbitMQ 等沉重的外部消息中间件，直接利用 C++23 在进程内实现一个轻量级的 Pub/Sub 机制。
3. **事务后触发（Post-Commit Dispatch）**：**极其重要！** 只有当数据库事务（Unit of Work）真正 `Commit` 成功后，才能对外发送事件。否则一旦数据库回滚，副作用（如邮件已经发出去）将无法撤回。

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

### 2.2.1 必备事件清单

| Event                     | 必备字段                                                              | 触发来源                                                   | 典型订阅者                                                 |
| ------------------------- | --------------------------------------------------------------------- | ---------------------------------------------------------- | ---------------------------------------------------------- |
| TransactionCreated        | userId, transactionId, accountId, occurredAt                          | CreateTransactionUseCase                                   | BalanceCacheInvalidator, ReportCacheInvalidator            |
| TransactionDeleted        | userId, transactionId, accountId, occurredAt                          | DeleteTransactionUseCase                                   | BalanceCacheInvalidator, AuditLogHandler                   |
| TransferCompleted         | userId, transferGroupId, sourceAccountId, targetAccountId, occurredAt | CreateTransferUseCase                                      | BalanceCacheInvalidator, ReportCacheInvalidator            |
| AccountArchived           | userId, accountId, occurredAt                                         | ArchiveAccountUseCase                                      | DashboardCacheInvalidator, AuditLogHandler                 |
| AccountDangerouslyDeleted | userId, accountId, occurredAt                                         | DangerousDeleteAccountUseCase                              | AuditLogHandler, SecurityNotificationHandler               |
| CategoryCreated           | userId, categoryId, board, occurredAt                                 | CreateCategoryUseCase / InitializeDefaultCategoriesUseCase | CategoryTreeCacheInvalidator                               |
| CategoryDeleted           | userId, categoryId, board, occurredAt                                 | DeleteCategoryUseCase                                      | CategoryTreeCacheInvalidator, ReportCacheInvalidator       |
| ExchangeRateRefreshed     | provider, baseCurrency, targetCurrency, fetchedAt                     | RefreshExchangeRateUseCase                                 | LatestRateCacheInvalidator, AuditLogHandler                |
| SyncCompleted             | userId, provider, syncJobId, importedCount, skippedCount, occurredAt  | RunSyncJobUseCase                                          | ReconciliationJob, AuditLogHandler                         |
| UserPreferenceUpdated     | userId, occurredAt                                                    | UpdateUserPreferenceUseCase                                | FrontendPreferenceCacheInvalidator, ReportCacheInvalidator |
| AuditLogRecorded          | auditLogId, operatorUserId, action, resourceType, occurredAt          | AuditLogHandler                                            | SecurityNotificationHandler                                |

命名规则：

1. 事件名使用过去时态
2. 事件只描述已经成功发生的事实
3. 事件不得承担命令语义，例如不要命名为 `RefreshDashboard`
4. 事件 payload 只携带 ID、时间和少量摘要字段
5. 事件必须在数据库事务成功提交后派发

```cpp
// domain/events/TransferCompletedEvent.hpp
#pragma once
#include "domain/events/IDomainEvent.hpp"
#include "domain/entities/Account.hpp" // 仅需 AccountId
#include "domain/entities/Transaction.hpp" // 仅需 TransactionId

struct TransferCompletedEvent : public IDomainEvent {
    AccountId sourceAccountId;
    AccountId targetAccountId;
    TransactionId transferGroupId;
    std::chrono::system_clock::time_point occurredAt;

    TransferCompletedEvent(AccountId src, AccountId tgt, TransactionId groupId)
        : sourceAccountId(src), targetAccountId(tgt), transferGroupId(groupId),
          occurredAt(std::chrono::system_clock::now()) {}

    std::string getEventName() const override { return "TransferCompleted"; }
    std::chrono::system_clock::time_point getOccurredAt() const override { return occurredAt; }
};

```

```cpp
// domain/events/AccountDangerouslyDeletedEvent.hpp
#pragma once
#include "domain/events/IDomainEvent.hpp"

struct AccountDangerouslyDeletedEvent : public IDomainEvent {
    int64_t accountId;
    int64_t userId;
    std::chrono::system_clock::time_point occurredAt;

    // 构造函数和接口实现...
};

```

---

## 3. 基础设施层：轻量级事件总线 (Event Bus)

在 `infrastructure/events/` 目录下，利用现代 C++ 的 `std::type_index` 和 `std::function` 实现一个强类型的本地事件总线。

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

public:
    // 注册订阅者
    template<typename EventType, typename HandlerFunc>
    void subscribe(HandlerFunc&& handler) {
        std::lock_guard<std::mutex> lock(mutex_);
        handlers_[typeid(EventType)].push_back([handler](const std::shared_ptr<IDomainEvent>& ev) {
            // 安全的向下转型
            handler(std::static_pointer_cast<EventType>(ev));
        });
    }

    // 异步派发事件
    void publish(const std::shared_ptr<IDomainEvent>& event) override {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = handlers_.find(typeid(*event));
        if (it != handlers_.end()) {
            for (const auto& handler : it->second) {
                // 将处理器扔进 Drogon 的线程池执行，避免阻塞当前主业务
                drogon::app().getLoop()->queueInLoop([handler, event]() {
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

## 4. 事务整合：保证一致性 (Post-Commit Dispatch)

正如之前强调的，事件不能在 Use Case 执行一半的时候发出去。我们需要稍微改造我们在《05_Repository_and_Persistence_Design.md》中设计的 `IUnitOfWork`。

### 4.1 工作单元聚合事件

我们在 `IUnitOfWork` 中增加事件收集的功能。在实体/聚合根处理时，把产生的事件“暂存”在 UoW 中。

```cpp
// application/persistence/IUnitOfWork.hpp (改造后)
#pragma once
#include <functional>
#include <expected>
#include <vector>
#include <memory>
#include "domain/repositories/RepositoryError.hpp"
#include "domain/events/IDomainEvent.hpp"

class IUnitOfWork {
public:
    virtual ~IUnitOfWork() = default;

    // 暂存要在事务成功后派发的事件
    virtual void registerEvent(std::shared_ptr<IDomainEvent> event) = 0;

    // 执行事务
    virtual std::expected<void, RepositoryError> executeInTransaction(
        std::function<std::expected<void, RepositoryError>()> action
    ) = 0;
};

```

### 4.2 事务完成后的自动派发与异步解耦

在基础设施层（Drogon UoW 实现）拦截 Commit 状态。必须确保事件派发逻辑紧跟在**数据库连接真正 Commit 成功**的物理动作之后，而不是仅仅在 `action()` 返回 `has_value()` 时。

同时，为了防止事件处理器（Event Handlers）执行缓慢（如发送邮件、调用外部 Webhook）或抛出异常拖慢主业务线程，系统引入了**异步解耦派发**与**本地消息表（Outbox Pattern）**设计：

1. **线程池异步派发**：
   * `IEventBus` 内部集成 Drogon 的线程池或自定义的轻量级工作线程池。
   * 区分**同步订阅者**（如 `BalanceCacheInvalidator`，需要强一致性更新缓存）和**异步订阅者**（如 `AuditLogHandler`、`SecurityNotificationHandler`）。
   * 异步订阅者的执行被封装为任务投递到线程池中，实现主业务线程的即时释放。
2. **本地消息表（Outbox Pattern）预留**：
   * 为了保证在进程崩溃或网络抖动时事件“至少一次投递（At-Least-Once Delivery）”，系统设计了本地消息表：
     ```sql
     CREATE TABLE outbox_events (
         id BIGSERIAL PRIMARY KEY,
         event_id UUID NOT NULL UNIQUE,
         event_name VARCHAR(128) NOT NULL,
         payload JSONB NOT NULL,
         status VARCHAR(32) NOT NULL DEFAULT 'PENDING', -- PENDING, PROCESSED, FAILED
         retry_count INT NOT NULL DEFAULT 0,
         created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
         updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
     );
     ```
   * 在 `DrogonUnitOfWork` 事务闭包中，将事件序列化为 JSON 并与业务数据在**同一个数据库事务**中写入 `outbox_events` 表。
   * 事务 Commit 成功后，立即触发一次异步扫描；同时，调度器中的 `OutboxPublisherJob` 每隔 10 秒扫描一次 `PENDING` 状态的事件进行重试派发，确保事件的绝对可靠性。

```cpp
// infrastructure/persistence/DrogonUnitOfWork.cpp (改造片段)
#include "application/events/IEventBus.hpp"

class DrogonUnitOfWork : public IUnitOfWork {
private:
    drogon::orm::DbClientPtr dbClient_;
    std::shared_ptr<IEventBus> eventBus_;
    std::vector<std::shared_ptr<IDomainEvent>> pendingEvents_;

public:
    DrogonUnitOfWork(drogon::orm::DbClientPtr dbClient, std::shared_ptr<IEventBus> eventBus)
        : dbClient_(dbClient), eventBus_(eventBus) {}

    void registerEvent(std::shared_ptr<IDomainEvent> event) override {
        pendingEvents_.push_back(std::move(event));
    }

    std::expected<void, RepositoryError> executeInTransaction(
        std::function<std::expected<void, RepositoryError>()> action) override
    {
        pendingEvents_.clear(); // 清理旧事件
        auto trans = dbClient_->newTransaction();

        auto result = action();

        if (result.has_value()) {
            // 显式提交事务，并捕获物理提交结果
            try {
                // 假设 Drogon 事务对象支持显式 commit()
                // 只有在底层连接真正 Commit 成功后，才派发事件
                trans->commit(); 
                
                for (const auto& ev : pendingEvents_) {
                    // 内部根据订阅者类型，决定是同步调用还是投递到线程池异步调用
                    eventBus_->publish(ev);
                }
                pendingEvents_.clear();
                return {};
            } catch (const std::exception& e) {
                LOG_ERROR << "Transaction commit failed physically: " << e.what();
                pendingEvents_.clear();
                return std::unexpected(RepositoryError{RepositoryStatus::DatabaseError, "Physical commit failed"});
            }
        } else {
            // 显式回滚，直接丢弃暂存的事件
            trans->rollback();
            pendingEvents_.clear();
            return std::unexpected(result.error());
        }
    }
};
```

---

## 5. 业务用例中的应用：危险删除

让我们回顾《07_Workflow_and_Lifecycle_Design.md》中的危险删除流程。加入事件机制后，Use Case 变得非常干净。

```cpp
// application/use_cases/DangerousDeleteAccountUseCase.cpp
// ... 之前的权限与校验逻辑 ...

return uow_->executeInTransaction([&]() -> std::expected<void, RepositoryError> {

    auto delTxRes = txRepo_->physicalDeleteByAccount(accountId);
    if (!delTxRes) return delTxRes;

    auto delAccRes = accountRepo_->physicalDelete(accountId);
    if (!delAccRes) return delAccRes;

    // 原来的审计日志写入和缓存清理去掉了！
    // 替换为：向 UoW 注册一个事件
    uow_->registerEvent(std::make_shared<AccountDangerouslyDeletedEvent>(
        accountId.value(), userId.value()
    ));

    return {};
});

```

### 5.2 独立的事件处理器 (Event Handlers)

在系统启动时（例如在 `main.cpp` 中），我们将处理器注册到总线上。这种设计完全符合开闭原则（Open/Closed Principle）。

```cpp
// 处理器 1：负责写高危审计日志
eventBus->subscribe<AccountDangerouslyDeletedEvent>([auditRepo](const auto& ev) {
    auditRepo->log("DANGEROUS_DELETE", "Account", ev->accountId, "User: " + std::to_string(ev->userId));
});

// 处理器 2：负责发预警邮件给用户
eventBus->subscribe<AccountDangerouslyDeletedEvent>([emailService](const auto& ev) {
    emailService->sendWarningEmail(ev->userId, "您刚刚彻底删除了一个账户及其所有流水。");
});

```

---

## 6. 未来扩容预留：发件箱模式 (Outbox Pattern)

当前的 `LocalEventBus` 是基于内存的。如果 C++ 进程在 `Commit` 成功但还没来得及 `publish` 事件时崩溃，事件就会丢失。
在单机环境下这勉强可以接受（因为我们的核心记账数据已经安全落盘了），但如果未来要保证“邮件必达”或者接入微服务，可以预留**发件箱模式 (Outbox Pattern)**：

1. 在 `transactions` 等核心表所在同一个 PostgreSQL 库里建一张表 `domain_events_outbox`。
2. 在 `executeInTransaction` 的同一个事务里，把要把派发的事件作为 JSON 插入到 `outbox` 表中。
3. 让我们在《12_Scheduler_Design.md》中设计的 Scheduler  定时轮询这张表，把还没发送的事件真正发到外部队列或执行。

由于这会引入不小的复杂度，当前版本可以先依靠 C++ 内存派发，但在系统架构层面必须预留切换到发件箱模式的底座。

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
3. 事务提交后，由 Scheduler 或专用 Worker 派发事件
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
