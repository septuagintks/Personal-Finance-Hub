# Personal Finance Hub - Event System Design

Version: 1.1

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
// pfh/domain/events/i_domain_event.h
#pragma once
#include <string>
#include <chrono>

class IDomainEvent {
public:
    virtual ~IDomainEvent() = default;

    virtual std::string event_name() const = 0;
    virtual std::chrono::system_clock::time_point occurred_at() const = 0;
    virtual std::string aggregate_type() const = 0;
    virtual std::string aggregate_id() const = 0;
    virtual std::string payload_json() const = 0;
};

```

### 2.2 具体事件案例

事件中只携带必要的 ID 和最小化上下文，不要把整个 Entity 塞进去。

### 2.3 必备事件清单

| Event                     | 必备字段                                                              | 触发来源                                                   | 典型/预留订阅者                                            |
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

Phase 1 当前只注册 `SupplementalAuditHandler`：它处理汇率刷新成功/失败的系统补充
审计，并以独立 identity 处理所有事件的 dead letter。表中的缓存失效、外部安全通知、
同步导入与 `AuditLogRecorded` 订阅者是后续扩展目标，不是 P1-S11 已交付能力。账户余额
缓存失效和关键业务 AuditLog 仍在业务事务内同步完成；没有订阅者的已提交事件按成功
no-op 投递并标记 published，不得因此宣称对应外部副作用已经执行。

### 2.4 AuditLog 与事件处理器边界

以下审计属于业务事实的一部分，必须由 Use Case 通过 `IAuditLogRepository` 在同一
事务内同步写入：认证生命周期，以及账户、流水、分类、标签、偏好等用户发起的
关键资源变更。业务写入、AuditLog 和 outbox 任一失败时，整个事务回滚。

补充审计 handler（Phase 1 为 `SupplementalAuditHandler`）只允许记录没有同步业务审计
的系统事件、投递失败/dead-letter、安全告警处置等补充事实。它不得根据 `TransactionDeleted`、`AccountArchived`、
`AccountDangerouslyDeleted` 等事件重复写入同一业务动作的 AuditLog。补充审计必须
使用 `outbox_id + handler_name`（或等价键）保证幂等。

当前具体事件集中在 `pfh/domain/events/domain_events.h`，构造时必须显式传入
`occurred_at`，不得在事件内部读取系统时钟。代表性签名为：

```cpp
TransactionCreatedEvent(UserId, TransactionId, AccountId, TimePoint);
TransferCompletedEvent(
    UserId, TransferGroupId, AccountId source, AccountId target, TimePoint);
AccountDangerouslyDeletedEvent(UserId, AccountId, TimePoint);
```

---

## 3. 消费侧：轻量级事件总线 (Event Bus)

Phase 1 的 framework-neutral `LocalEventBus` 位于 Application event 模块，以事件名匹配显式 `IEventHandler`；生产装配仍由 Infrastructure/Bootstrap 完成。它是 outbox 消费侧的最终分发器，而不是业务写路径的一部分。未来如改为类型索引或外部消息总线，不改变 Outbox 与 handler 幂等契约。

### 3.1 事件总线接口与实现

```cpp
class IEventHandler {
public:
    virtual ~IEventHandler() = default;
    virtual std::string_view handler_name() const noexcept = 0;
    virtual bool handles(std::string_view event_name) const noexcept = 0;
    virtual EventHandlingResult handle(const OutboxMessage& message) = 0;
};

class IEventBus {
public:
    virtual ~IEventBus() = default;
    virtual void subscribe(std::shared_ptr<IEventHandler> handler) = 0;
    virtual EventHandlingResult publish(const OutboxMessage& message) = 0;
};
```

`OutboxPublisherJob` 本身已经在专用 background worker 中运行，因此 `LocalEventBus`
在该 worker 上按订阅顺序同步等待 handler 结果，不能再把 handler fire-and-forget
提交到另一个队列。只有 handler 成功后 Publisher 才能标记 `published`；失败或异常必须
返回包含 `handler_name` 的稳定错误，让当前 claim 进入重试。订阅列表只在复制快照时
持锁，执行 handler 时不持注册锁。

---

## 4. 事务整合：保证一致性 (Transactional Outbox)

正如之前强调的，事件不能在 Use Case 执行一半的时候发出去。我们需要改造《05_Repository_and_Persistence_Design.md》中设计的 `IUnitOfWork`，让它在同一个数据库事务里同时写业务表和 outbox 表。

### 4.1 工作单元聚合事件

我们在 `IUnitOfWork` 中增加事件收集的功能。在实体/聚合根处理时，把产生的事件“暂存”在 UoW 中。这个 UoW 必须是 request-scoped / 单次事务作用域，不能跨请求共享。

```cpp
// pfh/application/persistence/i_unit_of_work.h
#pragma once
#include <functional>
#include <expected>
#include <memory>
#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/events/i_domain_event.h"

class IUnitOfWork {
public:
    virtual ~IUnitOfWork() = default;

    // 暂存要写入 outbox 的事件
    virtual void register_event(
        std::shared_ptr<domain::IDomainEvent> event) = 0;

    // 执行事务；闭包内 Repository 必须使用同一个事务上下文
    virtual domain::RepositoryVoidResult execute_in_transaction(
        std::function<domain::RepositoryVoidResult(
            domain::ITransactionContext& tx)> action) = 0;
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
   - Phase 1 的 `LocalEventBus` 在 Outbox 专用 worker 上按注册顺序同步执行 handler，不再二次 fire-and-forget；Publisher 必须拿到真实处理结果后才能完成状态转换。
   - 后续可增加缓存失效或安全通知 handler，但仍须遵守同一结果回传、幂等回执和业务提交后执行规则。
   - 业务 AuditLog 不由 post-commit handler 补写；只有 2.4 节定义的补充审计可以进入对应 handler。
   - 所有 handler 都必须幂等，因为 outbox 允许重试。

```cpp
auto result = postgres::execute_transaction<void>(
    db_, user_id_, "unit of work",
    [&](const std::shared_ptr<drogon::orm::Transaction>& transaction)
        -> domain::RepositoryVoidResult {
        DrogonTransactionContext context(transaction, user_id_);
        auto changed = action(context);
        if (!changed) {
            return changed;
        }
        return write_outbox(context);
    });
pending_events_.clear();
return result;
```

`write_outbox` 读取事件自身的 `event_name()`、`aggregate_type()`、
`aggregate_id()`、`payload_json()` 和 `occurred_at()`；outbox UUID 由 PostgreSQL
`gen_random_uuid()` 生成。该步骤只写可靠事件事实，不执行 handler。

## 5. 业务用例中的应用：危险删除

让我们回顾《07_Workflow_and_Lifecycle_Design.md》中的危险删除流程。加入事件机制后，Use Case 变得非常干净。

```cpp
return uow_->execute_in_transaction(
    [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
        auto account = accounts_.find_by_id_for_update(
            tx, cmd.account_id, cmd.user_id);
        if (!account) return std::unexpected(account.error());

        const auto occurred_at = std::chrono::system_clock::now();
        auto audited = audit_logs_.append(
            tx, make_dangerous_delete_audit(*account, cmd, occurred_at));
        if (!audited) return audited;

        auto transfers = transactions_.physical_delete_transfers_touching_account(
            tx, cmd.account_id);
        if (!transfers) return transfers;

        auto transactions = transactions_.physical_delete_by_account(
            tx, cmd.account_id);
        if (!transactions) return transactions;

        auto cache = accounts_.delete_balance_cache(tx, cmd.account_id);
        if (!cache) return cache;

        auto deleted = accounts_.physical_delete(tx, cmd.account_id);
        if (!deleted) return deleted;

        // AuditLog 已与删除事实处于同一事务；事件只承载提交后的副作用。
        uow_->register_event(
            std::make_shared<domain::AccountDangerouslyDeletedEvent>(
                cmd.user_id, cmd.account_id, occurred_at));
        return {};
    });

```

### 5.1 独立的事件处理器 (Event Handlers)

在系统启动时（例如在 composition root 中），将已实现的处理器注册到总线上。这些处理器由 `OutboxPublisherJob` 在成功 claim outbox 事件后触发。下方安全通知代码仅是后续扩展示意，Phase 1 production composition root 未注册邮件或外部通知 handler。

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
2. 在 `execute_in_transaction` 的同一个事务里，把要派发的事件作为 JSON 插入到 `domain_events_outbox`。
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
last_failed_handler VARCHAR(128),
last_failed_at TIMESTAMPTZ,
occurred_at TIMESTAMPTZ NOT NULL,
created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
published_at TIMESTAMPTZ,
locked_at TIMESTAMPTZ,
locked_by VARCHAR(128),
claim_token UUID
);

CREATE INDEX idx_outbox_pending
ON domain_events_outbox(status, next_retry_at)
WHERE status IN ('pending', 'failed');

CREATE TABLE outbox_handler_receipts (
    outbox_id UUID NOT NULL REFERENCES domain_events_outbox(id) ON DELETE CASCADE,
    handler_name VARCHAR(128) NOT NULL,
    handled_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
    PRIMARY KEY (outbox_id, handler_name)
);
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

1. 使用 `FOR UPDATE SKIP LOCKED` 取到事件后设置 `status = 'processing'`，并生成 `locked_at`、`locked_by` 和不可复用的 `claim_token`
2. 处理成功后，只有当前 claim token 可以设置 `status = 'published'` 和 `published_at`
3. 处理失败后递增 `retry_count`，记录失败 handler、时间和脱敏摘要；旧 claim token 更新必须返回冲突
4. `next_retry_at` 使用指数退避，例如 1m、5m、15m、1h、6h
5. 超过 `max_retry_count` 后设置 `dead_letter`
6. `processing` 超过租约时视为一次失败并恢复；达到上限则直接进入 dead letter
7. PostgreSQL 的 due/lease 过期判断、`locked_at`、`published_at`、`last_failed_at` 与 `next_retry_at` 统一基于数据库 `NOW()`；Application 只传退避时长，`IClock` 用于可测试的业务时间与 In-Memory adapter，不能作为跨主机租约时钟

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
actor_type = system
operator_user_id = NULL
action = security_event
resource_type = DomainEventOutbox
resource_id = outbox_id
metadata = { "status": "dead_letter", "eventName": "ExchangeRateRefreshed", "handlerName": "SupplementalAuditHandler", "retryCount": 5 }
```

规则：

1. Outbox 失败不得修改已经提交的业务事实
2. 缓存失效类事件失败可以重试
3. 安全通知、审计补充类事件失败必须进入告警
4. Worker 重启后必须能继续处理 pending/failed 事件
5. 普通补充审计与 dead-letter 审计使用不同 handler identity，避免普通处理回执屏蔽后续死信事实
6. 补充 AuditLog 与 handler receipt 必须在一个数据库事务中提交；receipt 已存在时按幂等成功处理
