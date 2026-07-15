# Personal Finance Hub 调度与后台任务设计

Version: 2.0
Backend: C++23
Architecture: Clean Architecture
Status: Approved

---

## 1. 架构定位

Scheduler 是后台用例的触发入口。Drogon Event Loop 只负责定时触发，不执行网络、数据库或长时间计算。

```text
DrogonTimerScheduler
    -> RecurringJob
        -> BoundedThreadPool
            -> Application Use Case / OutboxPublisher
                -> PostgreSQL / HTTPS adapters
```

持续边界：

- Job 不包含领域规则，只调用 Application 服务。
- Event Loop callback 只做本机防重入和一次有界 `submit`。
- 后台异常不得逃逸到 Event Loop 或终止 worker。
- 运行状态、租约、claim 和重试不依赖进程内全局变量作为唯一事实来源。
- 当前不依赖 Redis、RabbitMQ 或 Kafka。

---

## 2. 运行组件

| 组件 | 职责 |
| ---- | ---- |
| `IJob` | 定义 `name`、`start`、`stop` 与 `trigger_now` 生命周期 |
| `DrogonTimerScheduler` | 在 Event Loop 注册和取消周期 timer |
| `BoundedThreadPool` | 以固定 worker 数和队列容量执行后台工作 |
| `RecurringJob` | 统一防重入、入队、租约、异常、日志、软超时与停止等待 |
| `JobManager` | 注册唯一名称的 Job，统一启动、失败回滚与逆序停止 |
| `PostgresJobLeaseRepository` | 使用 PostgreSQL 管理跨实例定时任务租约 |

`RecurringJob` 必须由 `std::shared_ptr` 持有。启动时验证名称、间隔、超时和租约配置；`run_immediately` 启用时在 timer 注册后触发一次。相同实例已有任务运行时，重复触发直接跳过。

线程池停止后拒绝新任务，并等待已经接受的任务处理完毕。队列满、停止中或 `submit` 抛出异常时，Job 清除本机 `running` 状态并记录脱敏告警。

---

## 3. 现行后台任务

### 3.1 Outbox Publisher

`outbox-publisher` 周期调用 `OutboxPublisher::run_once(worker_id)`：

1. 恢复超过 processing timeout 的 claim。
2. 使用 `FOR UPDATE SKIP LOCKED` 领取到期事件。
3. 为每条事件生成 `claim_token`。
4. 通过 `LocalEventBus` 同步调用已注册 Handler。
5. 成功后标记 published；失败后安排有界退避或进入 dead letter。
6. 使用 `outbox_handler_receipts` 保证补充审计等 Handler 幂等。

Outbox 允许多个实例按行并行领取，因此不使用全局 scheduled-job lease。完成和失败转换必须匹配当前 `claim_token`，旧 worker 不能覆盖新 claim。

### 3.2 Exchange Rate Refresh

`exchange-rate-refresh` 调用 `RefreshExchangeRatesUseCase`：

- `PostgresActiveCurrencyQuery` 使用独立 background role 只读查询跨租户活跃币种。
- FreeCurrencyAPI 是主源，exchangerate.fun 是整批备用源。
- 汇率快照、Outbox 和 AuditLog 使用 request role 写入非 RLS 表。
- 任务使用 `scheduled_job_leases` 防止多实例重复拉取。

Provider 传输、批次和能力边界见 [汇率系统设计](08_Exchange_Rate_System_Design.md)。

### 3.3 Session Cleanup

`session-cleanup` 在同一个无租户事务中，分别按 batch limit 删除：

- `refresh_tokens.expires_at <= NOW()`。
- `revoked_access_tokens.expires_at <= NOW()`。
- `revoked_sessions.expires_at <= NOW()`。

过期判断使用 PostgreSQL `NOW()`。任务使用 scheduled-job lease，可重复执行；软删除业务流水不属于该任务。

---

## 4. 分布式租约

汇率刷新和 Session cleanup 使用 `scheduled_job_leases`：

```sql
CREATE TABLE scheduled_job_leases (
    job_name VARCHAR(128) PRIMARY KEY,
    owner_id VARCHAR(128) NOT NULL,
    lease_token UUID NOT NULL,
    lease_until TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);
```

租约规则：

1. acquire、过期与 release 使用数据库时钟。
2. acquire 仅在租约不存在、已过期或属于同一 owner 时成功。
3. release 必须匹配 `job_name + owner_id + lease_token`。
4. `lease_duration` 必须大于 `job_execution_timeout`。
5. 当前不做 lease heartbeat；任务必须幂等，租约时长应覆盖正常最坏执行时间。

若未来引入不可重复副作用或超过固定租约的长任务，必须先增加 token-guarded renew，不能只放大软超时。

---

## 5. 超时与错误边界

`job_execution_timeout` 是软期限。C++ worker 不被强制终止，以免破坏事务和对象生命周期；超时只记录 warning。外部 HTTP 另有硬超时。

配置必须满足：

- worker 数为 `1..64`。
- 队列容量为 `1..10000`。
- interval、batch size 和 execution timeout 为正数。
- Outbox processing timeout 与 Job lease 均长于软期限。
- 主备 Provider 最坏串行请求时间不超过软期限。

日志只记录 Job 名、Job ID、耗时、计数和最多 512 字节的清洗摘要，不记录 Provider 响应、认证材料或数据库明细。

---

## 6. 数据库角色边界

- background role：`BYPASSRLS`、默认只读，只注入 `PostgresActiveCurrencyQuery`。
- request role：执行汇率 append、Outbox 状态转换、补充审计、Session cleanup 和租约写入。
- background client 不得注入 Controller、认证服务、普通 Repository 或任何写 adapter。
- 两个角色必须是不同的非 superuser，production composition root 在启动时验证权限。

---

## 7. 启动与停止

`ProductionCompositionRoot` 构造数据库 client、Provider、Application 服务、线程池、timer 和三个 Job。Drogon beginning advice 调用 `JobManager::start_all()`；任一 Job 启动失败时停止已启动项并退出服务。

关闭顺序：

1. `JobManager::stop_all()` 取消 timer。
2. 每个 `RecurringJob::stop()` 等待当前执行结束。
3. `BoundedThreadPool::shutdown()` 拒绝新任务并 drain 队列。
4. 销毁 Provider、Repository 与数据库 client。

SIGTERM 必须走同一顺序并以正常状态退出。

---

## 8. 验收规则

测试至少覆盖：

1. 重名 Job、重复启动、部分启动失败和重复停止。
2. 本机防重入、队列满、停止中和异常后的状态恢复。
3. 软超时日志与 HTTP 硬超时配置关系。
4. 多实例租约争用、过期接管、旧 token release 失败和数据库时钟。
5. Outbox 并发 claim、恢复、退避、dead letter 与 Handler receipt。
6. 汇率主备切换、双源失败和历史降级。
7. Session cleanup 的边界时间、batch limit 与事务回滚。
8. Event Loop 无阻塞工作，以及 SIGTERM 的 timer cancel、drain 和退出。
9. background role 不进入写路径或请求路径。

运行配置以 [`config/README.md`](../../config/README.md) 为准，跨平台门禁见 [测试策略](16_Testing_Strategy.md)。
