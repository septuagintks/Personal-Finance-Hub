# Personal Finance Hub 事件设计

Version: 2.0
Backend: C++23
Architecture: Transactional Outbox
Status: Approved

---

## 1. 目标与边界

领域事件描述已经发生的业务事实。可靠性由 PostgreSQL Transactional Outbox 提供，`LocalEventBus` 只是 Outbox claim 成功后的进程内分发器。

核心规则：

- Domain 事件是强类型、框架无关的 C++ 对象。
- Application 在用例事务内登记事件。
- Unit of Work 将业务事实与 Outbox 行写入同一数据库事务。
- 事务提交前不发布事件。
- Handler 可重试且必须幂等，不得改写已经提交的核心业务事实。
- 需要与业务操作同生共死的 AuditLog 必须同步写入原事务，不能委托给 Handler。

---

## 2. 事件契约

每个 `IDomainEvent` 提供：

- `event_name`。
- `aggregate_type` 与 `aggregate_id`。
- 稳定 JSON payload。
- `occurred_at`。

用户范围事件的 payload 必含 `userId` 与 `occurredAt`。`occurredAt` 使用 UTC epoch seconds；字符串经过 JSON 转义。Payload 不得包含密码、Token、Provider key、Authorization、SQL 或用户完整财务明细。

### 2.1 现行事件目录

| 事件 | 产生路径 | 聚合 |
| ---- | -------- | ---- |
| `TransactionCreated` | 创建普通流水 | Transaction |
| `TransactionDeleted` | 软删除普通流水 | Transaction |
| `TransferCompleted` | 创建 Transfer 聚合 | TransferGroup |
| `AccountArchived` | 归档账户 | Account |
| `AccountDangerouslyDeleted` | 物理删除账户 | Account |
| `CategoryCreated` / `CategoryDeleted` | 分类维护 | Category |
| `UserPreferenceUpdated` | 更新偏好 | UserPreference |
| `UserRegistered` | 注册 | User |
| `UserLoggedIn` / `TokenRefreshed` / `UserLoggedOut` | 认证生命周期 | AuthSession |
| `RefreshTokenReuseDetected` | Refresh Token 重放 | AuthSession |
| `ExchangeRateRefreshed` | 成功追加汇率快照 | ExchangeRate |
| `ExchangeRateRefreshFailed` | Provider 双源失败 | ExchangeRateBatch |

事件名、aggregate 和 payload 字段是持久化契约；修改时必须同步 migration 兼容性、Handler、测试和归档说明。

---

## 3. Unit of Work 集成

标准写路径：

1. Application 打开 Unit of Work。
2. Repository 在同一个 `ITransactionContext` 中读取、锁定和写入。
3. 用例调用 `register_event`。
4. 用例返回成功后，Unit of Work 把 pending events 插入 `domain_events_outbox`。
5. 数据库提交业务事实与 Outbox。
6. 用例失败、Outbox 插入失败或 commit 失败时整体回滚并清空 pending events。

嵌套事务不受支持。bootstrap 注册事务先创建用户，再一次性绑定租户；绑定前后登记的事件仍由同一个 Unit of Work 写入。

---

## 4. Outbox 状态机

```text
pending / failed
    -> processing (worker + claim_token + locked_at)
        -> published
        -> failed (retry_count + next_retry_at)
        -> dead_letter
```

数据库规则：

- claim 使用 `FOR UPDATE SKIP LOCKED`，允许多个 Publisher 并行。
- processing timeout 到期后可恢复旧 claim。
- 每次 claim 生成新 `claim_token`；完成或失败转换必须匹配该 token。
- 重试使用有界退避，超过 `max_retry_count` 进入 dead letter。
- due、lease、claim 恢复和退避以 PostgreSQL `NOW()` 为事实时钟。
- 错误只保存长度受限、已清洗的摘要。

详细运行时规则见 [调度设计](12_Scheduler_Design.md)。

---

## 5. Event Bus 与 Handler

`LocalEventBus` 按注册顺序同步调用匹配的 `IEventHandler`：

- Handler 名称在单个进程内唯一。
- 发布时使用订阅列表快照，避免持锁执行 Handler。
- 第一个失败会终止本次投递并返回 `handler_name` 与脱敏摘要。
- Handler 抛出的异常在 Event Bus 边界转换为失败。
- 没有匹配 Handler 的事件视为成功投递。

### 5.1 当前生产 Handler

production composition root 只注册 `SupplementalAuditHandler`：

- 处理 `ExchangeRateRefreshed` 与 `ExchangeRateRefreshFailed`，记录系统级补充审计。
- 作为全局 dead-letter Handler，为未审计 dead letter 记录 SecurityEvent。
- 通过 `outbox_handler_receipts` 与 `append_once` 保证幂等。

其他业务事件当前没有邮件、外部通知或缓存 Handler；它们在无匹配 Handler 时正常标记 published。账户删除、认证和资源维护的业务 AuditLog 已在各自事务内同步写入。

---

## 6. 审计边界

| 类型 | 写入时机 | 示例 |
| ---- | -------- | ---- |
| 业务审计 | 与业务事实同事务 | Register、Login、DangerousDelete、资源维护 |
| 补充系统审计 | Outbox Handler | 汇率刷新结果、Provider 失败 |
| dead-letter 审计 | Outbox recovery | 事件最终投递失败 |

Handler 不得重复已有业务审计。补充审计失败会使当前事件投递失败并进入正常重试路径；receipt 与 audit 必须同事务提交。

---

## 7. 错误与安全

- Handler 失败不回滚已经提交的业务事务。
- 重试不得重复发送不可幂等副作用；新增外部 Handler 前必须先设计稳定幂等键。
- `last_error`、日志和 Audit metadata 不保存 Secret、Token、完整响应或堆栈路径。
- 未识别事件名不得反序列化为任意类型；Handler 只根据明确白名单处理。
- Outbox 表、receipt 和补充审计由 request role 访问，不使用 background BYPASSRLS client。

---

## 8. 验收规则

测试至少覆盖：

1. 业务写入与 Outbox 同事务提交及失败回滚。
2. 强类型事件名、aggregate、payload、`userId` 与 `occurredAt`。
3. 并发 claim、旧 token、processing recovery 和数据库时钟。
4. Handler 顺序、无匹配 Handler、异常收束和失败 Handler identity。
5. 有界退避、最大重试与 dead letter。
6. `outbox_handler_receipts` 对普通处理和 dead-letter 审计的幂等性。
7. 同步业务审计不被补充 Handler 重复。
8. Payload、错误摘要与日志的秘密扫描。

最终跨平台结果归档在 [Phase 1 S09-S12 交付摘要](../Archive/Phase_1_S09-S12_Delivery_Summary.md)。
