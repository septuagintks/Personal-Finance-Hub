# Phase 1 S11 Outbox、调度与后台任务基础 - 交付记录

**更新日期**: 2026-07-15
**阶段**: P1-S11 Outbox、调度与后台任务基础
**当前状态**: LOCAL COMPLETE - S11-01 至 S11-07 已完成实现、专项 review、Windows 全量回归与离线 production compile gate；真实 V6/PostgreSQL/Drogon/外部 HTTP runtime 保留到 P1-S12

> **后续替代说明（2026-07-15）**：本记录中的 OpenExchangeRates 内容是 S11 当时的交付事实。当前实现已由 FreeCurrencyAPI 主源、exchangerate.fun 整批备用源和 `FailoverExchangeRateProvider` 替代；现行行为以 `08_Exchange_Rate_System_Design.md`、S12 交付记录和当前代码为准。

---

## 1. 交付范围

本轮完成以下 Phase 1 后台运行闭环：

1. Outbox 使用带 claim token 的处理租约批量领取，支持崩溃恢复、有限退避、失败可观测与 dead letter。
2. 本地 EventBus 在业务事务提交后分发事件；补充系统审计使用 `outbox_id + handler_name` 持久化回执保证幂等。
3. OpenExchangeRates Provider 通过 framework-neutral HTTP transport 接入，严格校验响应集合、base、重复 key、Unix 时间戳和正数 `NUMERIC(20,10)` 汇率。
4. Scheduler 使用 Drogon timer 触发、有界专用工作池执行；timer callback 不执行 HTTP、数据库等待或长时间 CPU 工作。
5. 汇率刷新、Outbox 发布和认证过期数据清理纳入统一 JobManager 生命周期。
6. 汇率刷新与认证清理使用 PostgreSQL 过期租约避免多实例重复执行；Outbox 依靠行级 `SKIP LOCKED` 并行消费，不增加全局任务锁。
7. Production composition root 完成后台依赖装配，同时保持 request/background DbClient 权限边界。

---

## 2. 核心设计结论

### 2.1 数据库角色边界

- `background_db` 保持 BYPASSRLS + default read-only，只注入 `PostgresActiveCurrencyQuery`，用于跨租户收集活跃币种。
- Outbox 状态更新、汇率 append、补充 AuditLog、token 清理和任务租约全部使用普通 request-role DbClient，在无租户事务中访问非 RLS 表。
- 后台特权 client 不注入 EventBus、Provider、Job、Controller 或用户 Use Case。

### 2.2 Outbox 状态机

- 可领取状态：`pending`、到期的 `failed`。
- 领取状态：`processing`，必须同时持有 `locked_at`、`locked_by`、`claim_token`。
- 完成状态：`published`。
- 失败状态：按 1m、5m、15m、1h、6h 有上限退避；达到 `max_retry_count` 后进入 `dead_letter`。
- 过期 processing lease 会增加 retry count，并记录 `last_failed_handler=outbox-lease`、失败时间和摘要。
- 所有 publish/fail 状态更新同时校验 outbox id、`processing` 状态和 claim token；旧 worker 不能提交新租约。
- PostgreSQL 的 due、claim、失败退避、过期恢复、scheduled lease 和认证撤销记录清理使用数据库 `NOW()` 作为共享时钟，Application 只传退避时长，避免应用主机时钟偏差改变所有权、重试节奏或提前删除安全控制记录。

### 2.3 幂等审计

- 普通补充审计和 dead-letter 审计使用不同 handler identity，避免普通回执吞掉后续死信事实。
- PostgreSQL 在同一事务内先插入 handler receipt，再追加系统 AuditLog；任一步失败整体回滚。
- AuditLog 新增 `AuditActorType::User/System`；系统审计要求 `operator_user_id IS NULL` 且事务无 tenant scope。
- 已有同步业务审计的账户、流水、分类、标签、偏好和认证动作不由异步 handler 重复记录。
- Phase 1 未注册缓存或外部安全通知 handler；这些事件当前为成功 no-op 投递，余额缓存和关键业务审计由事务内写路径保证，通知能力不得记为已交付。

### 2.4 Scheduler 超时语义

- `job_execution_timeout` 是软执行期限：超时任务记录 warning，但不使用不安全的线程强制终止。
- HTTP Provider 使用独立请求超时；数据库操作依赖连接/语句失败返回并由 Job 异常边界收束。
- 分布式 lease 必须长于软期限；进程崩溃后由 `lease_until` 自动释放所有权。
- Outbox processing timeout 同样必须长于软期限，Provider hard timeout 不得超过软期限；配置加载和 production root 双层校验这些关系。
- Executor、lease adapter 或日志边界抛异常时统一收束，`running` 状态始终释放；工作池构造中途失败也会停止并 join 已创建线程。
- Job 停止时先取消 timer，再等待已接受任务结束，最后关闭并清空有界工作池。
- Phase 1 没有 lease heartbeat：极端任务超过 lease 时可能由另一实例接管，因此现有 Job 保持可重复执行，默认 lease 对正常硬超时留有裕量；长任务重叠与恢复属于 S12 runtime 验收，不宣称绝对不重叠。

---

## 3. 主要产物

| 范围 | 产物 |
| ---- | ---- |
| S11-01/02 | `OutboxPublisher`、In-Memory/PostgreSQL Outbox Repository、租约恢复、退避与死信 |
| S11-03 | `LocalEventBus`、`SupplementalAuditHandler`、幂等审计 store、系统审计 actor |
| S11-04 | `IHttpTransport`、`DrogonHttpTransport`、`OpenExchangeRatesProvider`、Provider identity 与 `IClock` 注入 |
| S11-05 | `CleanupExpiredSessionsUseCase`、三表清理 adapter、job lease adapter、三类具体 Job |
| S11-06 | `BoundedThreadPool`、`RecurringJob`、`DrogonTimerScheduler`、`JobManager` 和 production lifecycle advice |
| Migration | `V6__outbox_scheduler_foundation.sql`：outbox lease/失败字段、handler receipts、system audit actor、scheduled job leases |
| Config | Scheduler/Provider 线程、队列、间隔、批次、超时和租约配置及双层 fail-fast 校验 |

---

## 4. 本地验证结果

Windows GCC 16、Debug、`PFH_BUILD_POSTGRESQL=OFF`：

```text
CMake configure: PASS
Build: PASS
CTest: 341/341 PASS
PostgreSQL adapter compile gate: PASS
Production bootstrap compile gate: PASS
Production security compile gate: PASS
```

当前 341 项由 292 个 unit/use-case、17 个 In-Memory integration、28 个 framework-neutral API 和 4 个静态门禁组成。

S11 新增专项覆盖：

- 并发 claim 不重复、processing 崩溃恢复、旧 claim token 拒绝。
- 退避时间、最大重试、dead letter、失败 handler/时间/摘要。
- publisher→EventBus→幂等 AuditLog→published 完整闭环。
- 普通/死信回执隔离、审计失败后重试且不重复。
- Provider 数值 token 精度、重复 JSON key、缺失/额外币种、非法 base/时间戳/汇率、HTTP 错误。
- 有界队列溢出、优雅 drain、本机防重入、executor/lease 异常恢复、双实例租约竞争和旧租约 token。
- Refresh/Access/Session 三类过期认证数据的批量、幂等清理。

---

## 5. P1-S12 阻断项

以下内容未在当前 Windows 环境执行，不能由 compile gate 或 In-Memory 测试替代：

1. PostgreSQL 16+ 空库执行 V1-V6，以及已有 processing outbox 行升级到 V6 的迁移场景。
2. 真实 `FOR UPDATE SKIP LOCKED` 多连接 claim、claim token、lease timeout、handler receipt 原子性与 job lease 竞争。
3. 真实 request-role 权限下的 Outbox、汇率、AuditLog、认证清理和 scheduled lease 写入。
4. Drogon timer lifecycle、工作池关停、真实 OpenExchangeRates HTTPS/TLS/timeout 和响应解析。
5. 进程重启恢复、长任务软超时日志、真实 token 清理和 dead-letter 审计。
6. Linux Debug/Release、应用 Docker 镜像、健康检查和 S10+S11 合并后完整 API/runtime 回归。
7. 无 heartbeat 条件下任务超过 lease 的接管行为，确认重复汇率快照不改变查询结果、认证清理保持幂等，并评估后续是否需要 lease renew。

这些项目统一由 `tasks.md` #57 与 P1-S12-03 至 S12-06 跟踪。取得可追溯外部结果前，Phase 1 不具备合并到 `main` 的最终签署条件。
