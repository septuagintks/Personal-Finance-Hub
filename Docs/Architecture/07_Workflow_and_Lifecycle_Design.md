# Personal Finance Hub 工作流与生命周期设计

Version: 2.0
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Approved

---

## 1. 标准请求生命周期

```text
HTTP Request
    -> Presentation validation + authenticated UserId
        -> FinanceApplicationService / AuthService
            -> request-scoped Repository + Unit of Work
                -> Domain rule evaluation
                -> PostgreSQL business writes + AuditLog + Outbox
            -> commit
        -> stable DTO / Error
    -> HTTP Response
```

持续规则：

1. Presentation 只解析 DTO、认证上下文和 HTTP 语义。
2. Application 负责用户归属、事务、Repository 编排和错误映射。
3. Domain Service 只执行纯业务规则，不访问 Repository、事务、事件或 I/O。
4. 同一用例的读取、锁定、业务写入、同步审计和 Outbox 使用同一个 Unit of Work。
5. Repository 失败或 Domain 规则失败会回滚整个事务。
6. Outbox 只在事务提交后由后台 Publisher 投递。

---

## 2. 跨账户转账

`CreateTransferUseCase` 支持三种输入模式：

- Outgoing + Rate -> Incoming。
- Outgoing + Incoming -> Rate。
- Incoming + Rate -> Outgoing。

手续费可来自 Source、Target 或 ThirdParty 账户，并保存为独立 signed Adjustment。

执行顺序：

1. 校验用户、账户、模式、金额、汇率、手续费和描述。
2. 将 source、target 与可选 fee account 去重后按 Account ID 升序锁定，避免并发锁环。
3. 在事务内验证账户归属、归档状态和币种。
4. `TransferDomainService` 构造出账、入账和 Adjustment，保证聚合平衡。
5. Repository 原子写入 `transfer_groups`、双腿和 Adjustment，并取得数据库分配的 ID。
6. 受影响账户的余额缓存失效。
7. 登记 `TransferCompletedEvent`，由 Unit of Work 写入 Outbox。
8. 提交后返回转账组、双腿、金额、汇率与手续费 DTO。

Transfer 双腿永不计入收入或支出；手续费和汇兑损益只通过 Adjustment 进入现金流。

---

## 3. 危险账户删除

危险删除是不可逆的物理清理，`confirmations` 必须精确等于 `3`。

同一事务内按以下顺序执行：

1. 使用 `FOR UPDATE` 锁定账户并验证用户归属。
2. 写入包含删除前快照的同步 `DangerousDelete` AuditLog；审计失败则终止。
3. 删除所有触及该账户的完整 Transfer 聚合，包括另一侧双腿、Adjustment 与 group。
4. 删除该账户剩余流水。
5. 删除 `account_balance_cache`。
6. 删除账户。
7. 登记 `AccountDangerouslyDeletedEvent`。

数据库不依赖 `ON DELETE CASCADE` 隐式完成该流程。当前没有邮件或外部通知 Handler；业务 AuditLog 已在删除事务内完成，Outbox 事件不得重复写同一业务审计。

---

## 4. 余额缓存读取与重建

余额缓存完全封装在 `AccountRepository` 内，Domain 与 Application 只接收 `BalanceSnapshot`。

读取流程：

1. 在租户事务内锁定目标账户。
2. 查询未删除流水的 `MAX(version)` 与 `MAX(id)`。
3. 仅当缓存的 `source_version` 与 `last_transaction_id` 同时匹配时命中。
4. 未命中时按 `transaction_time, id` 读取流水并调用 `BalanceCalculationService`。
5. 使用 `NUMERIC(20,8)` 原子 UPSERT 新快照，并递增 `cache_version`。

流水新增、软删除、Transfer 聚合删除和危险账户删除必须在各自写事务中删除受影响缓存。In-Memory adapter 不得用“流水数量”替代 PostgreSQL 的 `MAX(version)` 语义。

---

## 5. 用户注册与默认数据

注册接口接受 `username`、`password`、可选 `baseCurrency` 与可选 `preferredLocale`。未提供 locale 时使用 `zh-CN`；系统不通过 IP 或请求头推断 locale。

注册使用 bootstrap Unit of Work：

1. 事务以无租户状态创建用户并取得数据库 ID。
2. 将同一事务一次性绑定到新 `UserId`，绑定后不可切换租户。
3. locale 按 `preferredLocale -> primary language -> en-US -> zh-CN` 查找可用系统模板。
4. 创建 `UserPreference`，并根据最终 locale 设置默认时区。
5. 先复制 root 分类，再复制 child 分类；唯一约束和 UPSERT 保证结构幂等。
6. 将 `users.categories_initialized` 设为 true。
7. 保存 refresh token hash，写 Register AuditLog，登记 `UserRegisteredEvent`。
8. 业务数据与 Outbox 一起提交并返回 Token Pair。

当前 migration 只内置 `zh-CN` 分类模板，因此其他 locale 会按上述顺序降级。增加语言包只需写入完整、父子关系一致的 locale 模板数据，不改变注册流程。

任一步失败都会回滚用户、偏好、分类、Token、审计和 Outbox，不保留半初始化账户。重复初始化返回冲突。

---

## 6. 认证生命周期

- 注册和登录签发短期 Access Token 与只返回一次的 Refresh Token。
- Refresh Token 只存 hash；刷新成功后执行 rotation。
- 已使用 Refresh Token 再次出现时，撤销整个 session 并登记 `RefreshTokenReuseDetectedEvent`。
- Logout 同事务撤销 session 与当前 Access Token，并登记 `UserLoggedOutEvent`。
- `UserLoggedInEvent` 与 `TokenRefreshedEvent` 同样通过 Outbox 保存。
- 未知用户和错误密码使用相同 401 语义，避免账户枚举。

---

## 7. 后台生命周期

后台只运行三类 Job：Outbox Publisher、Exchange Rate Refresh 和 Session Cleanup。timer callback 只向有界线程池提交工作；网络、数据库和 Handler 均在 worker 中执行。

汇率刷新与 Session cleanup 使用 PostgreSQL 租约，Outbox 使用逐行 `SKIP LOCKED` claim。启动、停止、软超时、角色边界和 SIGTERM 顺序见 [调度设计](12_Scheduler_Design.md)。

---

## 8. 错误与验收

可预期失败使用 `std::expected` 逐层映射；基础设施异常在 Repository 或 Job 边界收束。事务提交前的任何失败都必须撤销业务事实与待写 Outbox。

测试至少覆盖：

1. 转账三种模式、三种手续费来源、锁顺序、回滚和缓存失效。
2. 危险删除确认、同步审计、完整 Transfer 清理和失败回滚。
3. 缓存命中、自愈、并发读取和 `MAX(version)`。
4. 注册 locale 降级、父子分类、重复初始化和 bootstrap 租户绑定。
5. Refresh rotation、reuse detection、logout 与过期清理。
6. 业务写入、AuditLog 与 Outbox 的事务原子性。
7. 两用户隔离、连接池复用与 RLS fail closed。

事件状态机见 [事件设计](14_Event_Design.md)，测试层次见 [测试策略](16_Testing_Strategy.md)。
