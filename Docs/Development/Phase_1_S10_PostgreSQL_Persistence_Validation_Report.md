# Phase 1 S10 PostgreSQL Persistence - 验证报告

**验证日期**: 2026-07-13  
**验证提交**: `0a1ac5f7600f3ba5573520826847c08bb783b27a`  
**对应任务**: `Tasks.md` #46 / P1-S10-03 / P1-S10-04 / P1-S12-03  
**验证结论**: **FAIL / BLOCKED**

---

## 1. 验证目标

本轮针对以下验收目标执行验证：

- 实现 `DrogonUnitOfWork`。
- 实现 PostgreSQL 版 `*RepositoryImpl`。
- 在 composition root 中以真实持久化替换 In-Memory 实现。
- 将现有 Repository integration scenarios 对 PostgreSQL 16+ 真实测试库复跑。
- 在空库上完成 Flyway V1-V3 迁移。

---

## 2. 验证环境

- Host: macOS / Darwin ARM64
- Linux VM: Colima / Ubuntu 24.04 / Linux 6.8 / aarch64
- Compiler: GCC / G++ 13.3.0
- CMake: 3.28.3
- Ninja: 1.11.1
- PostgreSQL: 16.14 (`postgres:16-alpine`)
- Flyway: 10.22.0 (`flyway/flyway:10`)

数据库验证使用隔离 Docker network 与 tmpfs 数据目录，验证后已删除临时容器和网络，未保留测试数据卷。

---

## 3. 验证结果摘要

| 验收项 | 结果 | 说明 |
| ------ | ---- | ---- |
| `DrogonUnitOfWork` | FAIL | 当前源码中不存在实现 |
| PostgreSQL `*RepositoryImpl` | FAIL | 当前源码中不存在实现 |
| Drogon/PostgreSQL CMake 接线 | FAIL | `find_package(Drogon)` 与 `find_package(PostgreSQL)` 仍被注释 |
| composition root 使用真实持久化 | FAIL | `main.cpp` 仍是启动占位，不装配 Repository/UoW |
| PostgreSQL integration test target | FAIL | 当前 integration target 只构造 In-Memory Repository |
| 同一批 scenarios 在真实库复跑 | BLOCKED | 无 PostgreSQL adapter 与测试 fixture，无法执行 |
| PostgreSQL 16 空库迁移 | FAIL | V3 enum 类型错误，迁移回滚 |
| Linux Debug build | PASS | 43/43 build steps completed |
| Linux Debug tests | PASS | 253/253 passed |
| Linux Release build | PASS | 43/43 build steps completed |
| Linux Release tests | PASS | 253/253 passed |
| Debug/Release bootstrap smoke | PASS | 配置读取与 logger 初始化成功 |

现有 253 个测试由 240 个 unit/use-case tests 和 13 个 In-Memory repository integration tests 构成。它们证明当前 Domain/Application/In-Memory 基线没有回归，但不能作为真实 PostgreSQL 持久化验收结果。

---

## 4. 阻断问题

### 4.1 真实持久化 adapter 尚未实现

源码检索未发现以下目标：

- `DrogonUnitOfWork`
- PostgreSQL `AccountRepositoryImpl`
- PostgreSQL `TransactionRepositoryImpl`
- PostgreSQL `UserRepositoryImpl`
- PostgreSQL `UserPreferenceRepositoryImpl`
- PostgreSQL `CategoryRepositoryImpl`
- PostgreSQL `ExchangeRateRepositoryImpl`

当前 `tests/integration/repository_integration_test.cpp` 明确使用：

- `InMemoryStore`
- `InMemoryUnitOfWork`
- `InMemoryAccountRepository`
- `InMemoryTransactionRepository`
- `InMemoryExchangeRateRepository`
- `InMemoryUserRepository`
- `InMemoryUserPreferenceRepository`

因此现有 integration scenarios 没有连接 PostgreSQL，也没有验证 SQL、连接池、事务对象、RLS、真实行锁或数据库约束。

### 4.2 V3 空库迁移失败

执行：

```bash
flyway migrate
```

环境：

```text
PostgreSQL 16.14
Flyway OSS 10.22.0
```

结果：

```text
V1__initial_schema.sql: PASS
V2__seed_initial_currencies.sql: PASS
V3__seed_system_category_templates.sql: FAIL
```

数据库错误：

```text
SQL State: 42804
ERROR: column "default_board" is of type category_board
       but expression is of type text
```

首个失败点位于 `V3__seed_system_category_templates.sql` 的二级分类 `INSERT ... SELECT ... UNION ALL` 段。`'expense'` 和 `'income'` 在该表达式中被推断为 `text`，写入 `category_board` enum 列时没有隐式转换。

同一模式在多个二级分类插入段重复出现，不能只修复首个 `food_parent` 段。应对所有相关 SELECT 列使用显式 enum 类型，例如：

```sql
'expense'::category_board
'income'::category_board
```

V3 失败后的数据库状态符合事务回滚预期：

```text
flyway_schema_history: V1=true, V2=true
currencies: 33
system_category_templates: 0
```

这说明 V3 自身完整回滚，但当前空库无法迁移到最新版本。

---

## 5. 尚未被真实数据库验证的关键语义

在 PostgreSQL adapter 和共享测试 fixture 落地前，以下行为仍无真实数据库证据：

- 业务写入与 outbox 在同一数据库事务内提交或回滚。
- `SELECT ... FOR UPDATE` 锁定读取及并发行为。
- `Account.version` 乐观锁冲突。
- 余额缓存 `source_version` 与 schema `version` 语义对齐。
- `NUMERIC(20,8)` / `NUMERIC(20,10)` 边界和数据库 round-trip。
- `SET app.current_user_id` 下的 RLS 用户隔离和 fail-closed 行为。
- 连接归还连接池前执行 `RESET app.current_user_id`。
- transfer group、双边流水和 adjustment 的原子写入。
- append-only exchange rate 与历史时间点查询。
- 数据库唯一约束、复合外键和跨用户写入拒绝。

---

## 6. 修复与复测要求

1. 修复 V3 中所有 `default_board` 的 enum 类型表达式。
2. 在 PostgreSQL 16+ 空库重新执行 `flyway migrate`、`flyway validate` 和 `flyway info`。
3. 实现 `DrogonUnitOfWork` 和 PostgreSQL Repository adapters，并在 CMake 中完成 Drogon/PostgreSQL 依赖与 target 接线。
4. 将现有 13 个 repository scenarios 抽为可复用测试契约，分别运行 In-Memory fixture 与 PostgreSQL fixture。
5. 增加 RLS、连接池 session reset、真实并发、行锁、乐观锁和 NUMERIC round-trip 场景。
6. 在 Debug 和 Release 下重新执行完整 CTest，并记录 PostgreSQL/Flyway 版本、commit hash 和测试结果。

在上述项目完成前，`Tasks.md` #46 不满足完成条件，不应标记为 `[x]`。

---

## 7. 本轮执行说明

- 本轮只执行验证和记录报告，未修改生产源码、迁移脚本或任务状态。
- 构建目录位于 ignored 的 `build/` 下。
- 临时 PostgreSQL 容器、Docker network 已清理。
- Colima 已在验证结束后关闭。
