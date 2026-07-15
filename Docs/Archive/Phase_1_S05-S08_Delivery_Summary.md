# Phase 1 S05-S08 金融领域与持久化交付摘要

Version: 1.0
Status: Complete

---

## 1. 交付范围

P1-S05 至 P1-S08 完成 PFH 的金融正确性基础、核心领域闭环、PostgreSQL schema、Repository 和 Unit of Work。

| Step | 交付内容 |
| ---- | -------- |
| S05 | `Decimal`、`Currency`、`Money` 与 `ExchangeRate` |
| S06 | Account、Transaction、Transfer 与纯领域服务 |
| S07 | PostgreSQL 16+ schema、种子数据与 Flyway |
| S08 | Repository、RLS、Unit of Work、余额缓存与 Outbox |

---

## 2. 金融原语

### 2.1 Decimal

`Decimal` 使用编译器原生 128 位整数承载定点值，不依赖 `float` 或 `double`。它提供：

- 严格普通十进制字符串解析。
- 加减乘除、比较、符号与绝对值。
- Half-Even 舍入。
- 溢出保护。
- `NUMERIC(20,8)` 与 `NUMERIC(20,10)` 边界校验。

金额输入在内部舍入前检查数据库精度；外部 Provider rate 可在防腐层显式 Half-Even 归一到 scale 10。

### 2.2 Currency 与 Money

Domain 白名单包含 20 种法币和 13 种加密货币。`Money` 由 `Decimal + Currency` 组成，同币种才允许直接加减；跨币种计算必须显式提供汇率。

REST 与 JSON 金额统一使用字符串，避免二进制浮点或客户端格式化改变金融值。

### 2.3 ExchangeRate

- 数据库存储 `USD -> target` 快照。
- 非 USD 货币对通过 USD 枢纽在内存中推导。
- 汇率为正数，使用 `NUMERIC(20,10)`。
- 快照 append-only，历史查询选择 `fetched_at <= target_time` 的最新值。
- 缺失汇率返回明确错误，不使用 `0` 或 `1`。

---

## 3. 领域模型

### 3.1 Account 与 Transaction

Account 固定用户、币种、类型、归档状态和 `int64_t version`，支持可选分类覆盖。Transaction 区分 Income、Expense、Adjustment 与 Transfer：

- Income 和 Expense 的 API 金额是正数 magnitude，存储层负责符号映射。
- Adjustment 使用带符号语义：正数为流入，负数为流出，零额拒绝。
- Transfer 双腿不计入收入或支出。
- 流水采用追加与软删除模型，不提供普通更新。

### 3.2 TransferAggregate

转账支持三种严格输入模式：

1. 源金额 + 汇率。
2. 目标金额 + 汇率。
3. 源金额 + 目标金额，派生汇率。

手续费可来自 Source、Target 或同用户的 ThirdParty 账户，并持久化为同一 transfer group 下的独立负 Adjustment。聚合保存、余额影响、Outbox 和失败回滚处于同一事务边界。

Phase 1 不注册转账聚合删除路由；普通流水删除拒绝 Transfer 双腿和同组 Adjustment。

### 3.3 纯领域服务

- `TransferDomainService`：验证与构造转账聚合。
- `BalanceCalculationService`：按 signed storage 语义重建余额。
- `CurrencyConversionService`：执行直接、反向和 USD 枢纽折算。

这些服务不访问 Repository、不打开事务、不读取系统时间、不发布事件。

---

## 4. PostgreSQL 与 Flyway

### 4.1 Schema 范围

V1-V6 覆盖：

- users、preferences、accounts、categories、tags。
- transactions、transfer_groups、exchange_rates、balance_cache。
- refresh/revoked sessions、audit logs、Outbox、Handler receipt 和 job lease。
- 多租户复合外键、CHECK、索引与 FORCE RLS。

种子数据包含 33 种币种和 55 个系统分类模板，其中 27 个 root、28 个 child，board 分布为 40 expense、15 income。

### 4.2 迁移验证

PostgreSQL 16.14 + Flyway 10.22.0 已完成：

- V1-V6 空库 migrate。
- `info` 与 `validate`。
- 第二次 migrate no-op。
- V1-V5 legacy Outbox 升级。
- 种子、索引、枚举 cast、约束、RLS 与数值精度断言。

迁移静态门禁还会检查 enum 赋值 cast 和 Domain/数据库币种目录一致性。

---

## 5. Repository 与事务

### 5.1 Unit of Work

`DrogonUnitOfWork` 创建真实数据库 Transaction，并把同一事务上下文传给业务 Repository 和 Outbox 写入。Action error、异常或 Outbox 失败均回滚；只有物理提交完成后，事件才可以被发布。

注册路径支持在 User INSERT 获得 ID 后绑定 tenant，使 Preference、默认分类、Session、Audit 与 Outbox 在同一事务内完成。

### 5.2 多租户与角色

- request role：non-superuser、non-BYPASSRLS。
- background role：独立、non-superuser、BYPASSRLS、默认只读。
- background role 仅用于跨租户活跃币种查询。
- 请求 Repository 在短事务中使用 `SET LOCAL` 绑定 tenant。
- 连接池复用后 tenant context 不泄漏。

### 5.3 并发与缓存

- Account 保存使用 optimistic lock。
- Transfer 涉及的账户按 ID 排序锁定，避免锁顺序反转。
- 余额缓存以 `MAX(version)` 和最新流水 ID 判断有效性。
- 写路径在同一事务中使相关缓存失效或重建。
- ExchangeRate append-only，Transaction 只允许创建和软删除。

---

## 6. 验收结论

- 金融原语、领域实体和服务的正常、边界与溢出测试通过。
- 17 个 In-Memory integration scenarios 保持快速语义回归。
- production ON 的 PostgreSQL fixture 以 12/12 scenarios 验证 UoW、RLS、连接池、Repository、并发、缓存、NUMERIC、Outbox 与认证数据。
- Flyway V1-V6、legacy 升级和重复迁移通过。
- 当前实现与 `Docs/Architecture/02-08` 的数据、领域和持久化设计一致。
- 本阶段没有遗留的实现任务。

后续 Application、API 与运行时结果见 [S09-S12 交付摘要](Phase_1_S09-S12_Delivery_Summary.md)。
