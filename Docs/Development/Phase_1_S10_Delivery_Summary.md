# Phase 1 S10 REST API 与认证基础 - 交付记录

**更新日期**: 2026-07-14
**阶段**: P1-S10 REST API 与认证基础
**当前状态**: IN PROGRESS - P1-S10-01、S10-02、S10-03 已完成；S10-04 至 S10-11 待开发

---

## 1. 概述

本文是 P1-S10 的累计交付记录。已验收或正在验收以下内容：

- PostgreSQL 16.14 + Flyway 10.22.0 外部环境中的 V1-V3 空库迁移复测。
- P1-S10-01 REST 契约、金额符号、流水并发策略和转账删除边界收口。
- 转账手续费从 `CreateTransferCommand` 到 `TransferAggregate`、Repository 和报表语义的完整接线。
- P1-S10-02 Drogon/PostgreSQL 依赖接入与分层 CMake 目标。
- P1-S10-03 PostgreSQL Repository 与 `DrogonUnitOfWork` 适配器实现（本地静态门禁通过，外部连库验证留到 S10-04/S10-11 一并执行）。

以下内容尚未交付：composition root 接线、HTTP Controller、认证、API 回归、Outbox Publisher 和 Scheduler。因此本文不能作为 P1-S10 整体验收或真实持久化验收结论。

---

## 2. PostgreSQL 迁移外部复测

详细证据见 [Phase_1_S10_PostgreSQL_Persistence_Validation_Report.md](Phase_1_S10_PostgreSQL_Persistence_Validation_Report.md)。

### 2.1 缺陷与修复

首轮空库迁移在 V3 失败，SQL State 为 `42804`。根因是 7 个二级分类 `INSERT ... SELECT ... UNION ALL` 区块将 `default_board` 字面量推断为 `text`。

修复提交 `1833789` 为 28 个二级分类 board 字面量增加显式 `::category_board`，并新增离线 `migration_enum_casts` CTest 门禁。

### 2.2 最终结果

外部复测提交：`4621f69ef940868e10591f5449ac8be1dd9c95e3`。

| 验证项 | 结果 |
| ------ | ---- |
| PostgreSQL 16.14 全新空库 V1-V3 | PASS |
| Flyway `migrate` / `info` / `validate` | PASS |
| 第二次 `migrate` no-op | PASS |
| 33 条币种种子 | PASS |
| 55 条分类模板，27 root + 28 child | PASS |
| board 分布：40 expense + 15 income | PASS |
| 孤儿、父子 board 不一致、非 `zh-CN` | 均为 0 |
| 外部 Linux ARM64 CTest | 254/254 PASS |

该结果关闭 `tasks.md` #58，但不关闭 #46。外部复测时尚无 PostgreSQL Repository、`DrogonUnitOfWork` 或真实 fixture，不能据此验收事务、RLS、行锁、连接池租户上下文隔离或 NUMERIC round-trip。

---

## 3. P1-S10-01 交付

### 3.1 转账命令契约

`CreateTransferCommand` 当前固定为：

- 基础字段：`source_account_id`、`target_account_id`、`mode`、`outgoing_amount`、`incoming_amount`、`rate`、`description`、`occurred_at`。
- 手续费字段：可选 `fee_amount`、`fee_source`、`fee_account_id`。
- 所有金额和汇率均由十进制字符串解析，不接受浮点数边界。
- 三种 mode 只接受各自的两个输入字段；派生字段必须为空，避免同一请求携带相互冲突的事实。

手续费字段采用严格组合：

| `feeSource` | `feeAmount` | `feeAccountId` | 规则 |
| ----------- | ----------- | -------------- | ---- |
| 不提供 | 不提供 | 不提供 | 无手续费 |
| `SourceAccount` | 必填正数 | 禁止 | 使用源账户币种 |
| `TargetAccount` | 必填正数 | 禁止 | 使用目标账户币种 |
| `ThirdParty` | 必填正数 | 必填 | 同用户、未归档、与两端账户不同；使用该账户币种 |

### 3.2 领域与金额语义

- `TransferFee` 是 Application 完成账户解析后传给纯领域服务的输入，不引入 Repository 依赖。
- `fee_amount` 是正数 magnitude；聚合内手续费固定构造为负数 `TransactionType::Adjustment`。
- 手续费不并入 outgoing/incoming 主金额，三条流水共享同一个 `transfer_group_id` 和业务时间。
- Transfer 双边流水继续排除在 income/expense 之外；负 Adjustment 计入 expense 并减少对应账户余额。
- Adjustment 保持 signed 语义，未来返利或 FX gain 可以为正，手续费或 FX loss 为负。
- 本阶段不自动计算汇兑损益。现有命令只有成交汇率，没有市场基准汇率；伪造差额会制造不可审计的财务事实。

### 3.3 事务与持久化语义

- 基础字段和手续费字段组合在打开事务前校验，非法请求不产生业务写入或 outbox。
- 源、目标和可选第三方手续费账户在同一事务内按 ID 升序锁定，降低并发死锁风险。
- 三个账户均执行用户归属与归档校验，第三方账户不允许跨用户复用。
- In-Memory Repository 将数据库分配的 group ID 同时写入两端流水和全部 Adjustment。
- 转账聚合保存失败时，transfer group、双边流水、手续费和 outbox 一起回滚。
- 危险账户删除按 group 删除两端流水及全部 Adjustment，不遗留孤立手续费。
- Repository 对所有流水补充账户币种一致性检查，不再只校验转账双边流水。

### 3.4 已固定的其余边界

- Income/Expense 请求使用正数 magnitude；Adjustment 请求使用 signed amount。
- 对外响应使用业务金额，数据库 signed amount 不直接泄漏；Presentation mapper 仍在 P1-S10-05/S10-09 实现。
- Phase 1 不注册转账删除路由；完整 `DeleteTransferUseCase` 落地前只支持创建与查询。
- 流水采用追加 + 软删除，不提供普通更新，也不增加应用层行级乐观锁；账户并发继续使用 `Account.version`。

---

## 4. 主要改动

| 文件 | 内容 |
| ---- | ---- |
| `include/pfh/domain/transfer_aggregate.h` | 新增已解析的 `TransferFee` |
| `include/pfh/domain/transfer_domain_service.h` | 三种构造模式增加可选手续费输入 |
| `src/domain/transfer_domain_service.cpp` | 手续费路由、负 Adjustment 构造与聚合校验 |
| `include/pfh/application/dto.h` | `CreateTransferCommand`/`TransferResultDto` 手续费字段 |
| `include/pfh/application/use_cases/create_transfer_use_case.h` | 严格命令校验、三账户有序锁定与手续费接线 |
| `include/pfh/infrastructure/persistence/in_memory_transaction_repository.h` | grouped Adjustment 保存、币种校验和聚合级联删除 |
| `tests/unit/transfer_domain_service_test.cpp` | 领域手续费规则测试 |
| `tests/unit/use_case_test.cpp` | 用例、余额、cash flow、权限与错误路径测试 |
| `tests/integration/repository_integration_test.cpp` | 手续费聚合原子保存、回滚与级联删除测试 |

---

## 5. 验证结果

本次 P1-S10-01 本地 Windows GCC 16 Debug 门禁：

```text
build: PASS
unit/use-case: 255
In-Memory integration: 16
migration enum-cast gate: 1
CTest: 271/271 PASS
failed: 0
```

相较外部 V3 复测时的 254 项基线，本次新增 17 项测试：6 个 Domain、8 个 Use Case、3 个 Repository integration 场景。

S10-03 review 后本地基线增加 `postgresql_adapter_contracts`，并让 PostgreSQL 适配器在 OFF 模式下也通过 API stub 执行全源语法编译。该结果仍不包含真实 Drogon ABI 或 PostgreSQL 行为；S10-04 接入运行期 DbClient 后，必须用 PostgreSQL fixture 重跑聚合原子性、NUMERIC、RLS、行锁和连接池上下文场景。

---

## 6. P1-S10-02 与 S10-03 交付

### 6.1 S10-02：Drogon/PostgreSQL 依赖与分层 CMake

- 新增 `PFH_BUILD_POSTGRESQL` 选项（默认 OFF）。OFF 时运行 In-Memory 服务/测试，同时通过 `pfh_postgresql_adapter_compile_gate` 编译全部 PostgreSQL 翻译单元；ON 时把这些源文件加入真实 `pfh_infrastructure`。
- 当 `PFH_BUILD_POSTGRESQL=ON` 时，CMake 通过 `find_package` 强制要求 Drogon 和 PostgreSQL 库。
- 新增分层目标：`pfh_domain`、header-only `pfh_application`、`pfh_infrastructure`、header-only `pfh_presentation` 和 `pfh_server`。
- Domain 层保持零外部依赖，不链接 Drogon、PostgreSQL 或 spdlog。
- Application 层仅依赖 Domain；Infrastructure 实现 Application/Domain 端口；Presentation 依赖 Application，并只在 PostgreSQL/Drogon 生产模式链接 Drogon。
- 新增 `pfh_test_support` 库，供 unit/integration 测试共享 fixture 与 test helpers。
- 当前 HEAD 的 Windows GCC 16 / PostgreSQL OFF 构建、全量适配器离线编译与 CTest 均通过。外部 Linux ARM64 结果来自较早迁移复测提交，不能冒充当前 S10-03 实现验证。

### 6.2 S10-03：PostgreSQL Repository 实现

**适配器范围**：

| 适配器类 | 功能概要 |
| -------- | -------- |
| `UserRepositoryImpl` | create、find by id/username、save；提供事务感知读取，全局用户表写入额外校验“未绑定注册事务创建 / 同用户事务更新” |
| `UserPreferenceRepositoryImpl` | request-scoped find/fallback 与 upsert；事务感知重载保证 `users`/`user_preferences` read-your-writes |
| `AccountRepositoryImpl` | 创建、查询、乐观锁更新、`FOR UPDATE NOWAIT`、余额缓存 hit/miss/rebuild |
| `TransactionRepositoryImpl` | signed 单笔流水、Transfer + Adjustment 聚合保存、软删除、聚合物理删除与缓存失效 |
| `ExchangeRateRepositoryImpl` | append、latest、historical、pair history；历史查询使用 `fetched_at <= target_time` |
| `CategoryRepositoryImpl` | request-scoped 查询/保存、锁定读、父子 board 校验、root id 回溯 |
| `PostgresActiveCurrencyQuery` | 合并未归档账户币种与用户报表基准币种；要求独立后台只读连接 |
| `DrogonUnitOfWork` | 同一 Transaction 内业务写入 + outbox、异常回滚、commit callback 确认 |

**NUMERIC 映射与精度**：

- schema 金额列使用 `NUMERIC(20,8)`，汇率列使用 `NUMERIC(20,10)`，Transfer 快照列使用 `NUMERIC(30,10)`；客户端通过字符串 round-trip 避免二进制浮点截断。
- 应用层 `Decimal` 内部 `__int128` scale 固定 10^10，与数据库列 scale 一致。
- 所有 SQL `INSERT`/`UPDATE` 使用参数化查询，传递十进制字符串；`SELECT` 结果以字符串读取后由 `Decimal::parse_numeric_20_8/20_10` 重建。
- Repository 在写入前再次执行 `fits_numeric_20_8/20_10`，拒绝依赖数据库隐式舍入或溢出报错。

**RLS 上下文注入**：

- 写路径由 `DrogonUnitOfWork` 在首条业务 SQL 前执行事务级 `SET LOCAL app.current_user_id`；租户读路径也创建短 Transaction，在同一连接上 SET 后查询。
- PostgreSQL 所有 user-scoped 表的 RLS policy 从 `current_setting('app.current_user_id')::BIGINT` 读取当前用户 id，拒绝跨用户访问。
- request-scoped Repository 同时校验构造时的 tenant、方法参数、实体 owner 与 Transaction context；RLS 作为数据库末端防线。禁止在普通池化 `DbClient` 上 SET 后再发下一条租户查询。
- `SET LOCAL` 随 commit/rollback 自动清除，不在已结束事务上手工 RESET；S12 仍需证明连接池复用不会串租户。

**乐观锁与行锁**：

- `Account` 更新在 SQL 层校验 `version`，不匹配时返回 0 行，adapter 映射为 `RepositoryStatus::Conflict`。
- `IAccountRepository::find_by_id_for_update` 执行 `SELECT ... FOR UPDATE NOWAIT`，被锁时立即失败而不阻塞事务。
- 转账用例在锁定三账户时按 `account_id` 升序排列，降低死锁概率。
- `balance_of` 在检查/重建缓存前锁定账户；单笔流水写入与软删除也锁定对应账户。危险删除会按 ID 尝试锁定转账两端及 grouped Adjustment 账户，无法立即取得时回滚，避免缓存重建与余额事实并发交错。

**事务边界与异常映射**：

- `DrogonUnitOfWork::execute_in_transaction` 捕获 Drogon `DrogonDbException` 和 C++ `std::exception`，失败时回滚并清空本次事件队列。
- Drogon Transaction 没有直接 `commit()` API；实现释放最后一个 Transaction owner 触发提交，并等待 commit callback 返回 `true` 后才报告成功，禁止执行字面量 `COMMIT`。
- 用户名/分类唯一约束映射为稳定 `Conflict`；其他底层异常记录完整服务端日志，对 Application 只返回不含 SQL/驱动文本的 `DatabaseError`。当前未声称已经可靠解析全部 SQLSTATE。

**余额缓存 rebuild**：

- `balance_of` 在同一租户事务读取 `account_balance_cache`，同时校验 `source_version = COALESCE(MAX(transactions.version), 0)` 与最新未删除流水 ID；不使用 In-Memory 的流水数量简化规则。
- miss/stale 时调用 `BalanceCalculationService::calculate_balance`，同事务 UPSERT 缓存并递增 `cache_version`。
- 单笔流水、Transfer/Adjustment、软删除与聚合物理删除都会删除全部受影响账户缓存，下次 `balance_of` rebuild。
- 物理删除普通流水时先在同一数据修改 CTE 中删除余额缓存，再删除 tag relations 与流水，避免 `account_balance_cache.last_transaction_id` 外键仍引用待删流水。

**转账聚合保存**：

- `save_transfer` 在一个事务内依次插入 `transfer_groups` 行、outgoing transaction、incoming transaction、以及所有 Adjustment（手续费）。
- Adjustment 入库时重建为数据库刚分配的真实 `transfer_group_id`，不会保留 Domain 的无效占位 ID。
- 任一 INSERT 失败时由外层 UoW 回滚，不产生孤立 group 或单边流水。
- `TransferPersistResult` 按当前接口返回 `transfer_group_id`、`outgoing_id` 和 `incoming_id`；接口不承诺返回 Adjustment IDs。
- 账户危险删除先物化所有关联 group，再删除 tag relations、两端与 grouped Adjustment、group 行和全部受影响缓存；第三方手续费账户单独触碰 group 时也能完整级联。

**分类 root 回溯**：

- `resolve_root_id_for_user` 从给定 `category_id` 向上遍历 `parent_id` 直到 `parent_id IS NULL`。
- 最多遍历 64 层（防止循环引用）；超出或遇到数据库行不存在时返回 `Database` 错误。
- 报表按一级分类聚合时调用此方法将子分类映射到 root。

### 6.3 本地静态门禁

本次 review 在 `PFH_BUILD_POSTGRESQL=OFF` 模式下构建并测试。生产适配器通过窄 Drogon API stub 编译，另有结构门禁检查 CMake 条件、TypedId、RLS、提交方式、余额版本和 Transfer 级联；未执行真实数据库连接：

```text
build (PostgreSQL OFF): PASS
unit/use-case: 254
In-Memory integration: 16
migration enum-cast gate: 1
PostgreSQL adapter contract gate: 1
PostgreSQL adapter compile gate: PASS (build target)
CTest: 273/273 PASS
failed: 0
```

该 stub 只验证项目源码语法和所用 API 形状，不替代真实 Drogon 头文件/ABI。真实 Repository/UoW 连库验证在 P1-S10-04 接线后与 fixture 一同执行，并在 S12 目标环境签署。

### 6.4 主要新增文件

| 文件 | 功能 |
| ---- | ---- |
| `include/pfh/infrastructure/persistence/*_repository_impl.h` | 6 个核心 PostgreSQL Repository 实现声明 |
| `src/infrastructure/persistence/*_repository_impl.cpp` | User/Preference/Account/Transaction/Category/ExchangeRate SQL 实现 |
| `include/pfh/application/ports/i_active_currency_query.h` | 从租户 AccountRepository 拆出的系统级调度查询端口 |
| `src/infrastructure/persistence/postgres_active_currency_query.cpp` | 独立后台只读连接的跨租户活跃币种查询 |
| `include/pfh/infrastructure/persistence/postgres_repository_support.h` | 固定事务读、commit callback、context 校验与统一异常边界 |
| `src/infrastructure/persistence/postgres_repository_support.cpp` | 回滚、日志与稳定 RepositoryError 映射 |
| `include/pfh/infrastructure/persistence/drogon_unit_of_work.h` | request-scoped/全局任务可选租户 UoW |
| `src/infrastructure/persistence/drogon_unit_of_work.cpp` | Transaction + RLS + outbox co-commit |
| `tests/support/drogon_stub/` | OFF 模式编译专用最小 Drogon API stub |
| `tests/sql/validate_postgresql_adapter_contracts.py` | PostgreSQL adapter 离线结构回归门禁 |

---

## 7. 任务状态

| 任务 | 状态 | 说明 |
| ---- | ---- | ---- |
| P1-S10-01 | 完成 | 契约、手续费、符号、并发与删除边界已固定 |
| P1-S10-02 | 完成 | Drogon/PostgreSQL 依赖接入与分层 CMake 目标 |
| P1-S10-03 | 完成 | PostgreSQL Repository 与 `DrogonUnitOfWork` 实现；本地静态门禁通过 |
| #48 | 完成 | 手续费 Application/Domain/Repository 路径已接通 |
| #50 | 完成 | 流水并发策略已固定 |
| #52 | 完成 | Phase 1 转账删除边界已固定 |
| #55 | 完成 | DTO 金额符号说明已固定 |
| #58 | 完成 | V3 PostgreSQL 16.14 空库复测通过 |
| #28 | 部分完成 | Flyway 已验证，运行期 DbClient 未接线 |
| #46 | 部分完成 | 核心 Repository/UoW 已实现并静态复核；composition root、真实 fixture 和目标环境签署待 S10-04/S12 |
| #51 | 部分完成 | `MAX(version)` + 最新流水 ID 与全写路径缓存失效已实现；真实 DB 复核待 S12 |
| #53 | 完成 | Application/Infrastructure/Presentation 分层 CMake 目标已落地 |
| #57 | 未完成 | P1-S12 完整外部环境门禁仍保留 |

---

## 8. 后续顺序

1. P1-S10-04：完成 composition root、DbClient 与 RLS 上下文接线。
2. P1-S10-05 至 S10-10：依次实现 HTTP 边界、认证、资源、流水、转账和报表 API；S10-06 注册 bootstrap UoW 必须支持创建 User 后在同一事务一次性绑定新 tenant，不能拆分默认数据初始化事务。
3. P1-S10-11：完成 API 回归与开发环境 PostgreSQL fixture 复跑并更新本文；P1-S12 在目标 Linux/Docker 环境作最终签署。
