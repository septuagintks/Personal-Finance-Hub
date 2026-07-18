# Personal Finance Hub 数据库设计

Version: 2.0
Backend: C++23
Database: PostgreSQL 16+
Status: Approved

---

## 1. 设计原则

- `transactions` 是账务事实来源，余额缓存和报表均可重建。
- 金额使用 `NUMERIC(20,8)`，汇率使用 `NUMERIC(20,10)`。
- 时间使用 `TIMESTAMPTZ`，应用边界统一为 UTC。
- Tenant 数据显式携带 `user_id`，并由复合外键与 FORCE RLS 双重约束。
- 核心关系不依赖隐式 `ON DELETE CASCADE`；危险删除由 Application 按固定顺序编排。
- 汇率与 Outbox 采用 append/retry 模型，不覆盖历史事实。
- Schema 只通过 Flyway versioned migration 演进。

---

## 2. 数据分区

### 2.1 全局参考与运行数据

| 表 | 职责 |
| -- | ---- |
| `currencies` | 法币与受控加密货币元数据 |
| `system_category_templates` | 按 locale 组织的分类模板 |
| `exchange_rates` | USD 枢纽汇率历史快照 |
| `domain_events_outbox` | 待投递和已投递的领域事件 |
| `outbox_handler_receipts` | Handler 幂等回执 |
| `outbox_retry_commands` | Operator dead-letter 重试的幂等命令事实 |
| `scheduled_job_leases` | 跨实例定时任务租约 |

这些表不使用租户 RLS。业务写入、Outbox 转换、补充审计和租约由普通 request role 在无租户事务中执行。

### 2.2 用户与账务数据

| 表 | 职责 |
| -- | ---- |
| `users` | 身份、密码 hash、基准币种与初始化状态 |
| `user_preferences` | locale、timezone、格式与首页偏好 |
| `accounts` | 用户账户与币种 |
| `categories` | 用户收入/支出分类树 |
| `transactions` | Income、Expense、Transfer 与 Adjustment |
| `transfer_groups` | Transfer 模式、汇率与快照元数据 |
| `transaction_corrections` | 普通流水的追加式更正关系 |
| `transfer_corrections` | Transfer 聚合的追加式更正关系 |
| `transaction_tags` | 用户标签 |
| `transaction_tag_relations` | 流水与标签的租户内关系 |
| `account_balance_cache` | 可重建余额快照 |

上述租户账务表及 `request_idempotency` 均启用并强制 RLS；当前角色初始化断言共覆盖 11 张租户表。

### 2.3 认证与审计

| 表 | 职责 |
| -- | ---- |
| `refresh_tokens` | Refresh Token hash、session 与 rotation 状态 |
| `revoked_access_tokens` | Access Token `jti` 撤销 |
| `revoked_sessions` | 整个 session family 撤销 |
| `audit_logs` | 用户或系统审计事实 |

数据库只保存 Token hash 或稳定标识，不保存明文密码、Refresh Token、Access Token 或 Provider key。

### 2.4 写请求幂等

`request_idempotency` 按 `(user_id, operation, idempotency_key)` 保存受保护写请求的 SHA-256 指纹、状态、过期时间和受控响应快照：

- 表启用并强制 FORCE RLS，`user_id` 必须绑定当前租户。
- 同键同指纹返回第一次结果；同键不同指纹返回 `409 Conflict`。
- `Transaction`、`Transfer` 的幂等占位、业务事实、Outbox 和响应快照在同一 Unit of Work 提交。
- Account、Category 和 Tag 创建同样在业务事务内保存精确响应快照，调用方超时重试不会重复创建资源。
- 过期键在同键重用时于当前事务回收；定期清理使用 `expires_at` 索引，不改变已提交业务事实。
- 后台清理只可调用 V10 提供的有界 `SECURITY DEFINER` 函数；函数固定 `search_path`、使用数据库时钟和 `FOR UPDATE SKIP LOCKED`，且不授予任意租户表访问权限。
- 响应快照只包含允许对外暴露的 DTO 字段，不保存 Token、密码或底层异常。

---

## 3. 用户与偏好

`users.username` 唯一，`password_hash` 使用 Argon2id PHC 字符串。`role` 是持久化的 `user` 或 `operator`，公共注册始终写入 `user`。`base_currency_code` 引用 `currencies(code)`，用于偏好缺失时的兼容读取。`categories_initialized` 只在注册默认数据事务完成后设为 true。

`user_preferences.user_id` 与 `users.id` 一一对应，包含：

- `base_currency_code`。
- `locale` 与 IANA `timezone`。
- 日期、数字格式与主题。
- 默认首页与报表周期。

注册事务先创建 User，再一次性绑定 tenant，随后写 Preference、默认分类、Session、Audit 与 Outbox。

---

## 4. 账户、分类与标签

### 4.1 账户

Account 记录 type、subtype、asset/liability category、币种、描述、归档状态和 `version`。

- `(id, user_id)` 唯一，供租户复合外键引用。
- `is_archived` 与 `archived_at` 必须一致。
- 更新使用 optimistic lock。
- 归档不删除历史流水。

### 4.2 分类

`categories` 按 `board` 区分 income 与 expense：

- `(user_id, board, parent_id, name)` 使用 `NULLS NOT DISTINCT` 唯一约束。
- `(parent_id, user_id)` 引用同一用户的父分类。
- `template_id` 可追溯到全局模板。
- 软删除分类继续用于历史报表名称解析。

系统模板同样按 `locale + group_name + parent_id + name` 唯一。当前种子包含 `zh-CN` 的 55 个模板。

### 4.3 标签

标签在单个用户内名称唯一。`transaction_tag_relations` 同时携带 `user_id`，并通过复合外键保证 Transaction 与 Tag 属于同一 tenant。

---

## 5. 流水与转账

### 5.1 signed amount

`transactions.amount` 保存带符号金额：

| 类型 | 存储符号 | 报表语义 |
| ---- | -------- | -------- |
| Income | 正 | 收入 |
| Expense | 负 | 支出 |
| Transfer | Source 负、Target 正 | 不计入收支 |
| Adjustment | 正或负，零值拒绝 | 正为流入，负为流出 |

REST 层仍以正数幅度表达普通收入/支出，Presentation/Application 负责与存储符号互转。

Transaction 使用软删除和追加模型，不提供普通更新。`version` 用于持久化语义与余额缓存有效性，不把“未删除流水数量”当作版本。

### 5.2 流水更正关系

`transaction_corrections` 保存原流水、替代流水、租户和更正时间。原流水和替代流水分别唯一，复合外键保证两端属于同一用户；删除账户时关系随完整流水事实级联清理。更正必须在同一事务内创建替代流水、软删除原流水、写入关系、失效相关余额缓存并提交 Audit/Outbox，不允许原地修改金额、账户、分类或时间。

### 5.3 Transfer 聚合

`transfer_groups` 保存：

- `transfer_mode`：1 Outgoing+Rate、2 BothAmounts、3 Incoming+Rate。
- 可选 `exchange_rate NUMERIC(20,10)`。
- Provider 与快照时间。

ID 使用 `BIGSERIAL`，由数据库在保存时分配。双腿和可选手续费 Adjustment 通过 `transfer_group_id + user_id` 绑定到同一聚合。

Transfer 必须原子创建。`transfer_corrections` 以原组和替代组分别唯一的复合租户外键保存更正链；更正追加新组并软删除旧组全部成员，不修改 `transfer_groups` 历史事实。聚合删除同样只软删除双腿与全部 Adjustment。两条写路径都先锁原组，再按账户 ID 升序锁定所有受影响账户，并在同一事务处理缓存、Audit 与 Outbox。危险账户删除必须先删除所有触及账户的完整聚合，不能留下另一侧孤立流水。

---

## 6. 余额缓存

`account_balance_cache` 只是一种可丢弃的读优化：

- `balance` 使用账户币种和 `NUMERIC(20,8)`。
- 已提交缓存行可直接命中；其有效性由所有流水变更事务内删除缓存行来保证。
- `source_version` 等于重建时未删除流水的 `MAX(version)`。
- `last_transaction_id` 等于重建时未删除流水的 `MAX(id)`。
- miss 时锁账户并复查缓存，再以 signed `SUM(amount)` 与两个 `MAX` 一次聚合后原子 UPSERT。
- 流水新增、软删除、Transfer 清理与危险删除在同一事务中使相关缓存失效。

---

## 7. 汇率

`exchange_rates` 保存 USD 枢纽方向的 append-only 快照：

- `base_currency_code` 与 `target_currency_code` 不得相同。
- `rate > 0`，使用 `NUMERIC(20,10)`。
- `fetched_at` 决定历史可用时间。
- `source` 记录实际成功 Provider。

历史查询选择 `fetched_at <= target_time` 的最新快照。交叉汇率在 Application/Domain 中由同一时间点的 USD 快照推导。

---

## 8. Audit、Outbox 与 Scheduler

### 8.1 Audit

`audit_logs.actor_type` 区分 user、operator 与 system：user/operator actor 必须有 `operator_user_id`，system actor 必须为空。用户危险操作和 Operator dead-letter 重试均与对应事实同事务；汇率与 dead-letter 补充审计由幂等 Handler 写入。可选 `trace_id` 只保存受控请求关联标识。

### 8.2 Outbox

Outbox 状态为 pending、processing、published、failed 或 dead_letter。processing 行必须同时存在 `locked_at`、`locked_by` 与 `claim_token`，其他状态必须全部为空。

- claim 使用 `FOR UPDATE SKIP LOCKED`。
- 完成/失败必须匹配当前 token。
- retry count 受约束并使用 `next_retry_at`。
- `outbox_handler_receipts(outbox_id, handler_name)` 保证副作用幂等。

### 8.3 Scheduler lease

`scheduled_job_leases` 以 `job_name` 为主键，记录 owner、token 和过期时间。release 必须匹配 owner 与 token，所有时间判断使用数据库 `NOW()`。

---

## 9. 多租户与角色

RLS policy 使用 `pfh_current_user_id()` 读取事务级 `app.current_user_id`：

- request role 是无成员继承的 non-superuser、non-BYPASSRLS，且默认允许写事务；它只能读取 Flyway 历史，不能修改 migration 事实。
- 每个 request-scoped 短事务使用 `SET LOCAL` 绑定 UserId。
- 未绑定 tenant 时 RLS fail closed。
- request/background role 不得参与任一方向的角色成员关系，避免运行角色继承额外能力，或其他角色通过 `SET ROLE` 取得运行权限。
- background role 与 request/admin role 必须不同，是 NOINHERIT、non-superuser、BYPASSRLS、默认只读；角色初始化会先清除旧表、序列和函数直授权，再只授予 `accounts(currency_code, is_archived)` 与 `users(base_currency_code)` 的列级 `SELECT`，不得读取 `users.password_hash` 或其他账户/身份字段。
- background client 不进入 Controller、认证、普通 Repository 或写 adapter。

应用授权角色与数据库连接角色是两个独立边界：`users.role` 决定 USER/OPERATOR API 权限，request/background role 决定 SQL 与 RLS 能力。Operator HTTP 请求仍使用受限 request role，不能获得 background role 或管理员数据库权限。

Tenant 关系同时使用 `(id, user_id)` 复合键，避免仅靠应用过滤或单列外键产生跨用户关联。

---

## 10. Flyway V1-V10

| 版本 | 结果 |
| ---- | ---- |
| V1 | 核心 schema、约束、索引与 FORCE RLS |
| V2 | 20 种法币与 13 种加密货币元数据 |
| V3 | 55 个 `zh-CN` 系统分类模板 |
| V4 | session 撤销与认证审计动作 |
| V5 | Transfer 汇率对齐 `NUMERIC(20,10)` |
| V6 | Outbox lease、Handler receipt、system audit 与 Job lease |
| V7 | 租户内金融写请求幂等记录与 FORCE RLS |
| V8 | 追加式流水更正关系、复合租户外键与 FORCE RLS |
| V9 | 追加式 Transfer 聚合更正关系与 FORCE RLS |
| V10 | 持久化应用角色、Operator Audit/重试事实与有界幂等清理函数 |

应用不自行调用 Flyway。迁移由部署流程或独立容器在应用启动前执行。已发布 migration 不得改写；修正使用下一个 versioned migration。

V1-V10 的迁移、角色授权和重复执行由部署门禁验证；真实 PostgreSQL 结果见 [Phase 2 S09-S12 交付摘要](../Archive/Phase_2_S09-S12_Delivery_Summary.md)。操作流程见 [Database Migration Guide](../Guides/Database_Migration_Guide.md)。

---

## 11. 验收规则

数据库门禁至少覆盖：

1. V1-V10 空库和 legacy upgrade。
2. 33 种币种、55 个分类模板与父子 board 一致性。
3. FORCE RLS、两用户隔离、未绑定 fail closed 和连接池复用。
4. request/background 角色名预检、管理属性、成员继承、默认读写状态、Flyway 历史只读、background 列级权限，以及 USER/OPERATOR 应用授权隔离。
5. `NUMERIC(20,8/10)` round-trip、Half-Even 和超界拒绝。
6. Unit of Work commit/rollback 与业务/Outbox 原子性。
7. optimistic lock、Transfer 锁顺序与完整聚合清理。
8. 余额缓存命中、失效和 `MAX(version)` 重建。
9. 汇率 append-only 与历史时间点查询。
10. Outbox claim/retry/dead letter、Handler receipt 和 scheduled lease。
11. 幂等键同请求重放、冲突、过期回收、RLS 和业务/Outbox 原子性。
12. Operator retry command 并发幂等、Audit/TraceId 与 dead-letter 状态转换原子性。

Repository 细节见 [持久化设计](05_Repository_and_Persistence_Design.md)，最终测试矩阵见 [Phase 1 S05-S08 交付摘要](../Archive/Phase_1_S05-S08_Delivery_Summary.md)。
