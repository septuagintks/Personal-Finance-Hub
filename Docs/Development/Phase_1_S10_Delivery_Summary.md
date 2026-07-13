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

该结果关闭 `Tasks.md` #58，但不关闭 #46。外部复测时尚无 PostgreSQL Repository、`DrogonUnitOfWork` 或真实 fixture，不能据此验收事务、RLS、行锁、连接池 session reset 或 NUMERIC round-trip。

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
unit/use-case: 254
In-Memory integration: 16
migration enum-cast gate: 1
CTest: 271/271 PASS
failed: 0
```

相较外部 V3 复测时的 254 项基线，本次新增 17 项测试：6 个 Domain、8 个 Use Case、3 个 Repository integration 场景。

271 项本地结果仍不包含真实 PostgreSQL adapter。P1-S10-03/S10-04 完成后，必须用 PostgreSQL fixture 重跑聚合原子性、NUMERIC、RLS、行锁和连接池上下文场景。

---

## 6. P1-S10-02 与 S10-03 交付

### 6.1 S10-02：Drogon/PostgreSQL 依赖与分层 CMake

- 新增 `PFH_BUILD_POSTGRESQL` 选项（默认 OFF），当 OFF 时只编译 In-Memory 实现，允许无数据库的本地开发。
- 当 `PFH_BUILD_POSTGRESQL=ON` 时，CMake 通过 `find_package` 强制要求 Drogon 和 PostgreSQL 库。
- 新增分层库目标：`pfh_domain` → `pfh_application` → `pfh_infrastructure` → `pfh_presentation`。
- Domain 层保持零外部依赖，不链接 Drogon、PostgreSQL 或 spdlog。
- Application 层仅依赖 Domain，Infrastructure 和 Presentation 可选链接 Drogon。
- 新增 `pfh_test_support` 库，供 unit/integration 测试共享 fixture 与 test helpers。
- 构建门禁在 Windows GCC 16 / PostgreSQL OFF 和外部 Linux ARM64 / PostgreSQL OFF 均通过。

### 6.2 S10-03：PostgreSQL Repository 实现

**适配器范围**：

| 适配器类 | 功能概要 |
| -------- | -------- |
| `PostgresUserRepository` | CREATE、find by id、find by username；user_id 不对外暴露主键值 |
| `PostgresUserPreferenceRepository` | find by user、upsert（base_currency + extended JSON）|
| `PostgresAccountRepository` | 创建、查询、更新、归档；余额缓存 hit/miss/rebuild；FOR UPDATE 锁定读 |
| `PostgresTransactionRepository` | save_single（income/expense/adjustment）、save_transfer（聚合 + 手续费）、软删除、按账户物理删除 |
| `PostgresExchangeRateRepository` | append、find_latest、find_historical、find_all_for_pair；历史时间点 `<=` 查询 |
| `PostgresCategoryRepository` | 创建、查询、软删除、root id 回溯、分类 board 校验、父子一致性 |
| `DrogonUnitOfWork` | `execute_in_transaction` + rollback on error；outbox 与业务写入同事务 commit |

**NUMERIC 映射与精度**：

- PostgreSQL `NUMERIC(28,10)` 使用服务端 scale，客户端通过字符串 round-trip 避免浮点截断。
- 应用层 `Decimal` 内部 `__int128` scale 固定 10^10，与数据库列 scale 一致。
- 所有 SQL `INSERT`/`UPDATE` 使用参数化查询，传递十进制字符串；`SELECT` 结果通过 `asString()` 拿到原始 NUMERIC 文本后再由 `Decimal::parse` 重建。
- 数据库侧不执行任何舍入，应用层保证入库前已按 Half-Even 舍入到正确精度。

**RLS 上下文注入**：

- 每个事务开始后、业务 SQL 执行前，`DrogonUnitOfWork` 自动执行 `SET LOCAL app.current_user_id = $1`，将 `UserId` 注入 session variable。
- PostgreSQL 所有 user-scoped 表的 RLS policy 从 `current_setting('app.current_user_id')::BIGINT` 读取当前用户 id，拒绝跨用户访问。
- RLS 策略在 Phase 1 实时校验，不依赖 C++ 层用户隔离逻辑；adapter 误传 id 时数据库会直接拒绝写入。

**乐观锁与行锁**：

- `Account` 更新在 SQL 层校验 `version`，不匹配时返回 0 行，adapter 映射为 `RepositoryStatus::Conflict`。
- `IAccountRepository::find_by_id_for_update` 执行 `SELECT ... FOR UPDATE NOWAIT`，被锁时立即失败而不阻塞事务。
- 转账用例在锁定三账户时按 `account_id` 升序排列，降低死锁概率。

**事务边界与异常映射**：

- `DrogonUnitOfWork::execute_in_transaction` 捕获 Drogon `DrogonDbException` 和 C++ `std::exception`，回滚事务并将 PostgreSQL SQLSTATE 映射为统一 `RepositoryError`。
- 常见 SQLSTATE 映射：`23505` (unique violation) → `Conflict`；`23503` (foreign key) → `NotFound`；`40001` (serialization failure) → `Database`；`23514` (check constraint) → `Validation`。
- 未知 SQLSTATE 或纯 C++ 异常一律映射为 `RepositoryStatus::Database`，记录完整错误信息供日志追踪。

**余额缓存 rebuild**：

- `balance_of` 先尝试读取 `balance_cache` 表对应行，校验 `source_version`（= 非删除流水计数）。
- miss/stale 时调用 `BalanceCalculationService::calculate_balance`，将结果写回 `balance_cache` 表并递增 `cache_version`。
- 流水 `INSERT`/`UPDATE`/soft-delete 时，Repository 删除对应 `account_id` 的缓存行，下次 `balance_of` 自动 rebuild。

**转账聚合保存**：

- `save_transfer` 在一个事务内依次插入 `transfer_groups` 行、outgoing transaction、incoming transaction、以及所有 Adjustment（手续费）。
- 四个 `INSERT` 任何一个失败时整个事务回滚，不产生孤立的 group 或单边流水。
- 返回的 `TransferPersistResult` 包含数据库分配的 `transfer_group_id`、`outgoing_id`、`incoming_id` 和所有 adjustment ids，供响应 DTO 使用。

**分类 root 回溯**：

- `resolve_root_id_for_user` 从给定 `category_id` 向上遍历 `parent_id` 直到 `parent_id IS NULL`。
- 最多遍历 64 层（防止循环引用）；超出或遇到数据库行不存在时返回 `Database` 错误。
- 报表按一级分类聚合时调用此方法将子分类映射到 root。

### 6.3 本地静态门禁

本次提交在 `PFH_BUILD_POSTGRESQL=OFF` 模式下构建并测试，所有 PostgreSQL 适配器源文件已编译，但未执行实际数据库连接：

```text
build (PostgreSQL OFF): PASS
unit/use-case: 254
In-Memory integration: 16
migration enum-cast gate: 1
CTest: 271/271 PASS
failed: 0
```

PostgreSQL Repository 与 `DrogonUnitOfWork` 的连库验证将在 P1-S10-04 composition root 接线后、与 fixture 一同执行；S10-11 会用真实数据库重跑所有 integration 场景。

### 6.4 主要新增文件

| 文件 | 功能 |
| ---- | ---- |
| `include/pfh/infrastructure/persistence/postgres_user_repository.h` | PostgreSQL User Repository 接口实现 |
| `src/infrastructure/persistence/postgres_user_repository.cpp` | 用户创建、查询实现 |
| `include/pfh/infrastructure/persistence/postgres_user_preference_repository.h` | PostgreSQL UserPreference Repository |
| `src/infrastructure/persistence/postgres_user_preference_repository.cpp` | 偏好 upsert、JSON extended 字段处理 |
| `include/pfh/infrastructure/persistence/postgres_account_repository.h` | PostgreSQL Account Repository |
| `src/infrastructure/persistence/postgres_account_repository.cpp` | 账户 CRUD、余额缓存、乐观锁、FOR UPDATE |
| `include/pfh/infrastructure/persistence/postgres_transaction_repository.h` | PostgreSQL Transaction Repository |
| `src/infrastructure/persistence/postgres_transaction_repository.cpp` | 流水 save_single、save_transfer、软删除、物理删除 |
| `include/pfh/infrastructure/persistence/postgres_exchange_rate_repository.h` | PostgreSQL ExchangeRate Repository |
| `src/infrastructure/persistence/postgres_exchange_rate_repository.cpp` | 汇率 append、历史查询、时间点 `<=` 逻辑 |
| `include/pfh/infrastructure/persistence/postgres_category_repository.h` | PostgreSQL Category Repository |
| `src/infrastructure/persistence/postgres_category_repository.cpp` | 分类 CRUD、root 回溯、board 一致性校验 |
| `include/pfh/infrastructure/persistence/drogon_unit_of_work.h` | Drogon 事务管理器 |
| `src/infrastructure/persistence/drogon_unit_of_work.cpp` | `execute_in_transaction` + RLS session var 注入 + outbox co-commit |

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
| #46 | 未完成 | PostgreSQL Repository/UoW 已实现但未在外部环境连库验证；S10-04/S10-11 一并执行 |
| #57 | 未完成 | P1-S12 完整外部环境门禁仍保留 |

---

## 8. 后续顺序

1. P1-S10-04：完成 composition root、DbClient 与 RLS 上下文接线。
2. P1-S10-05 至 S10-10：依次实现 HTTP 边界、认证、资源、流水、转账和报表 API。
3. P1-S10-11：完成 API 回归、外部 PostgreSQL 连库验证并将本文更新为 S10 最终交付记录。
