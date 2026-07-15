# Phase 1 S10 PostgreSQL Persistence - 验证报告

**验证日期**: 2026-07-13
**首轮验证提交**: `0a1ac5f7600f3ba5573520826847c08bb783b27a`
**V3 修复提交**: `183378981e33a8aa9853e19cdd4a19c75d5a6e77`
**外部复测提交**: `4621f69ef940868e10591f5449ac8be1dd9c95e3`
**对应任务**: `tasks.md` #28 / #46 / #57 / #58
**报告状态**: FINAL - 结果已汇入 `Phase_1_S10_Delivery_Summary.md`
**最终结论**: **V3 MIGRATION PASS；S10 REAL PERSISTENCE BLOCKED**

> 结论边界：`BLOCKED` 描述的是上述外部复测提交当时的状态。后续 S10-03 已实现核心 Repository/UoW，但尚未在同类真实环境复跑；当前实现进度以 `Phase_1_S10_Delivery_Summary.md` 为准，本报告保留为迁移测试原始证据。

---

## 1. 验证范围与结论边界

本报告记录两轮外部环境验证：

1. 首轮检查 P1-S10 PostgreSQL 持久化前置状态，并在 PostgreSQL 16+ 空库执行 Flyway V1-V3。
2. 修复 V3 `category_board` enum 类型推断问题后，在相同环境重新执行空库迁移、Flyway 校验、数据断言和完整 CTest。

本报告最终确认：

- V1-V3 已能在 PostgreSQL 16.14 全新空库连续迁移成功。
- V3 enum cast 缺陷已修复，SQL State `42804` 未复现。
- Linux ARM64 Debug/Release 与当前 254 项测试基线通过。
- `DrogonUnitOfWork`、PostgreSQL Repository、composition root 和真实 PostgreSQL integration fixture 尚未实现，因此 #46 及相关真实持久化语义仍为 `BLOCKED`。

V3 迁移通过不能解释为 PostgreSQL Repository/UoW/RLS 已通过。

---

## 2. 验证环境

| 项目 | 环境 |
| ---- | ---- |
| Host | macOS / Darwin ARM64 |
| Linux VM | Colima / Ubuntu 24.04 / Linux 6.8 / aarch64 |
| Compiler | GCC / G++ 13.3.0 |
| CMake | 3.28.3 |
| Ninja | 1.11.1 |
| Python | 3.12.3 |
| PostgreSQL | 16.14 (`postgres:16-alpine`) |
| Flyway | OSS 10.22.0 (`flyway/flyway:10.22.0`) |
| Database | 隔离 Docker network + tmpfs 全新空库 `pfh_validation` |

临时 PostgreSQL 容器、Docker network 和测试数据均在验证后清理，Colima 已关闭。

---

## 3. 首轮验证结果

### 3.1 预期内的未实现项

首轮源码检查确认以下内容尚未实现：

- `DrogonUnitOfWork`。
- PostgreSQL `AccountRepositoryImpl`、`TransactionRepositoryImpl`、`UserRepositoryImpl`、`UserPreferenceRepositoryImpl`、`CategoryRepositoryImpl`、`ExchangeRateRepositoryImpl`。
- Drogon/PostgreSQL CMake 生产接线。
- 生产 composition root。
- PostgreSQL integration test target 与共享 fixture。

这些项目属于 P1-S10-02 至 P1-S10-04 的后续开发范围，状态应记为 `NOT IMPLEMENTED / BLOCKED`，不是已有实现发生回归。

### 3.2 首轮真实缺陷

首轮执行 `flyway migrate` 时：

```text
V1__initial_schema.sql: PASS
V2__seed_initial_currencies.sql: PASS
V3__seed_system_category_templates.sql: FAIL

SQL State: 42804
ERROR: column "default_board" is of type category_board
       but expression is of type text
```

根因是 V3 的 7 个二级分类 `INSERT ... SELECT ... UNION ALL` 区块中，`expense/income` 字面量被 PostgreSQL 推断为 `text`，无法隐式写入 `category_board` enum。

V3 失败后事务完整回滚：

```text
flyway_schema_history: V1=true, V2=true
currencies: 33
system_category_templates: 0
```

首轮同时通过 Linux Debug/Release 构建、253/253 CTest 和 bootstrap smoke；这些结果证明 Domain/Application/In-Memory 基线没有回归，但不构成真实持久化证据。

---

## 4. 修复内容与纸面检查

修复提交 `1833789` 完成：

1. 7 个二级分类区块的 28 个 `default_board` 字面量全部增加显式类型转换。
2. `'expense'::category_board` 共 19 处，`'income'::category_board` 共 9 处。
3. 顶级分类继续使用具备目标列类型上下文的 `INSERT ... VALUES`。
4. `group_name` 保持 `TEXT` 字符串，不错误转换为 `category_board`。
5. 新增离线 CTest `migration_enum_casts`，阻止同一 SQL 形态回归。

纸面检查结果：

| 检查项 | 结果 |
| ------ | ---- |
| V1 `category_board` 仅包含 `income/expense` | PASS |
| parent CTE / UNION 区块 | 7 |
| 二级分类写入 | 28 |
| 显式 `::category_board` | 28 |
| 二级分类裸 board 字面量 | 0 |
| 父分类查询按 locale/group/root 收窄 | PASS |
| 离线 `migration_enum_casts` | PASS |

离线门禁是针对本次 SQL 形态的正则回归保护，不是 PostgreSQL parser，不能替代本报告的真实空库复测。

---

## 5. 外部复测结果

修复提交祖先检查通过：

```text
183378981e33a8aa9853e19cdd4a19c75d5a6e77 is ancestor of HEAD: PASS
```

最终结果：

| 检查项 | 结果 | 实际结果 |
| ------ | ---- | -------- |
| 空库确认 | PASS | 迁移前 public schema table count = 0 |
| `flyway migrate` | PASS | V1、V2、V3 连续成功，schema 到达 v3 |
| `flyway info` | PASS | V1-V3 均为 `Success`，无 Pending/Failed/Missing |
| `flyway validate` | PASS | `Successfully validated 3 migrations` |
| 第二次 `migrate` | PASS | Schema 已是最新版本，无需迁移 |
| 数据完整性断言 | PASS | 数量、层级、board 和 locale 全部符合预期 |
| `migration_enum_casts` | PASS | 1/1 passed |
| 完整 CTest | PASS | 254/254 passed，0 failed |

本轮没有异常 SQL State，原 SQL State `42804` 未复现。

### 5.1 数据完整性实际值

```text
flyway_schema_history: 1=true, 2=true, 3=true
currency_count: 33
template_count: 55
root_count: 27
child_count: 28
default_board=expense: 40
default_board=income: 15
orphan_count: 0
parent_board_mismatch_count: 0
non_zh_cn_count: 0
```

第二次 `migrate` 后计数保持为 33 条币种、55 条分类模板、27 个根分类和 28 个子分类。

### 5.2 CTest 实际值

```text
migration_enum_casts: 1/1 passed
full CTest: 254/254 passed
failed: 0
total test time: 0.87 sec
```

254 项测试由以下部分组成：

- 240 个 unit/use-case tests。
- 13 个 In-Memory repository integration tests。
- 1 个 migration enum-cast gate。

---

## 6. 最终验收判断

### 6.1 已通过

- PostgreSQL 16.14 空库 V1-V3 连续迁移。
- Flyway `migrate`、`info`、`validate` 与第二次 no-op。
- V2 币种和 V3 分类模板数据完整性。
- V3 enum cast 离线回归门禁。
- Linux ARM64 Debug/Release 构建与测试基线。

### 6.2 仍阻断

在 PostgreSQL adapter 和共享测试 fixture 落地前，以下行为仍无真实数据库证据：

- 业务写入与 outbox 同事务提交/回滚。
- `SELECT ... FOR UPDATE` 与真实并发行为。
- `Account.version` 乐观锁冲突。
- 余额缓存 `source_version` 与 schema 版本语义。
- `NUMERIC(20,8/10)` 数据库 round-trip。
- RLS fail-closed 与连接池事务级租户上下文隔离。
- Transfer 双边流水和 Adjustment 原子写入。
- Exchange rate append-only 与历史查询。
- 复合外键、唯一约束和跨用户写入拒绝。

---

## 7. 任务状态结论

| 任务 | 结论 |
| ---- | ---- |
| #28 PostgreSQL/Flyway 基础 | 保持 `[~]`；迁移已验证，运行期 DbClient 尚未接线 |
| #46 真实持久化 | 保持 `[ ]`；adapter 与真实 integration scenarios 未实现 |
| #57 外部环境门禁 | 保持 `[ ]`；Linux/Flyway 子项通过，完整 API/Outbox/Scheduler 门禁未完成 |
| #58 V3 真实空库复跑 | 可标记 `[x]` |

Flyway 关于默认 `sql` location 未来可能弃用的 warning 不影响本轮结果，后续升级 Flyway 时再显式配置 location。

---

## 8. 最终结论

**V3 enum cast 修复复测：PASS。**

V3 迁移阻断已经关闭。真实 PostgreSQL Repository/UoW/RLS 验收仍保持 `BLOCKED`，必须在 P1-S10-03/S10-04 实现后使用真实 fixture 重新验证。本报告的最终结果已纳入 `Phase_1_S10_Delivery_Summary.md`，后续 S10 开发不再改写本次外部复测基线。
