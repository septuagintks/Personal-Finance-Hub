# Phase 1 S08 Repository 与 Unit of Work - 交付记录

**完成日期**: 2026-07-12
**阶段**: P1-S08 Repository 与 Unit of Work
**状态**: ✅ 接口 + In-Memory 语义实现 + 集成测试通过；Drogon/PostgreSQL 适配器待接线

---

## 1. 概述

根据 `Phase_1_Detailed_Development_Plan.md` 的 P1-S08 规划，本阶段建立持久化读写路径与事务一致性边界。

由于尚未接入 Drogon ORM，本阶段交付：

1. **Domain 仓储接口**（无框架依赖）
2. **Application `IUnitOfWork`**
3. **Infrastructure In-Memory 实现**（与 PostgreSQL 规则语义对齐）
4. **Integration tests**（用户隔离、乐观锁、余额缓存、汇率历史、UoW/outbox）

后续 `DrogonUnitOfWork` / SQL Repository 只需替换实现，**不改 Domain 接口**。

---

## 2. 交付物

### 2.1 Domain

| 文件 | 说明 |
| ---- | ---- |
| `include/pfh/domain/repositories/repository_error.h` | RepositoryStatus / RepositoryError |
| `include/pfh/domain/repositories/i_transaction_context.h` | 不透明事务上下文 |
| `include/pfh/domain/repositories/i_user_repository.h` | User 持久化 |
| `include/pfh/domain/repositories/i_user_preference_repository.h` | 偏好读取（含 fallback） |
| `include/pfh/domain/repositories/i_account_repository.h` | 账户 + 余额快照 + 乐观锁 |
| `include/pfh/domain/repositories/i_transaction_repository.h` | 单笔流水 + TransferAggregate 原子写 |
| `include/pfh/domain/repositories/i_exchange_rate_repository.h` | 汇率 append-only + 历史查询 |
| `include/pfh/domain/events/i_domain_event.h` | 领域事件接口 |
| `include/pfh/domain/events/simple_domain_event.h` | 测试用最小事件 |

### 2.2 Application

| 文件 | 说明 |
| ---- | ---- |
| `include/pfh/application/persistence/i_unit_of_work.h` | `register_event` + `execute_in_transaction` |

### 2.3 Infrastructure（In-Memory）

| 文件 | 说明 |
| ---- | ---- |
| `in_memory_store.h` | committed + staging 存储 |
| `in_memory_transaction_context.h` | 事务句柄 |
| `in_memory_unit_of_work.h` | 同事务提交/回滚 + outbox 共落 |
| `in_memory_user_repository.h` | User / UserPreference |
| `in_memory_account_repository.h` | 用户隔离、乐观锁、余额缓存 |
| `in_memory_transaction_repository.h` | 单笔/转账原子写 + 金额符号映射 |
| `in_memory_exchange_rate_repository.h` | append-only + historical |

### 2.4 测试

| 文件 | 说明 |
| ---- | ---- |
| `tests/integration/repository_integration_test.cpp` | Repository 集成场景 |
| `tests/integration/CMakeLists.txt` | `pfh_integration_tests`（`DISCOVERY_MODE PRE_TEST`） |

---

## 3. 关键设计

### 3.1 Unit of Work

- 写路径必须在 `execute_in_transaction` 内
- 成功：业务 staging + outbox staging 一并 commit
- 失败：两者一并 rollback，不留脏数据
- 提交前不派发事件（只写 outbox）

### 3.2 TransferAggregate 原子写

`save_transfer` 在同一事务中：

1. 校验两端为 `TransactionType::Transfer` 且同 user
2. 分配/复用 `transfer_group_id`
3. 写 `transfer_groups`
4. 写出账（负金额）+ 入账（正金额）
5. 写 adjustments（如有）
6. 失效两端余额缓存

### 3.3 金额符号分层（与 S06/S07 对齐）

| 层 | 约定 |
| -- | ---- |
| Domain 构造（TransferDomainService） | 正数幅度 + 角色方向 |
| Repository 边界 | transfer 出负入正；Expense 正数幅度取负 |
| BalanceCalculationService | 兼容正数幅度与已签名金额 |

### 3.4 余额缓存

`balance_of`：

1. 读取未删除流水
2. 比较 `source_version`
3. 命中返回缓存
4. 未命中调用 `BalanceCalculationService` 重建并回写

### 3.5 汇率

- `append`：只插入，不更新历史
- `find_historical`：`fetched_at <= target_time` 的最新一条

---

## 4. 构建与耦合修复摘要

### 4.1 `pfh_integration_tests_NOT_BUILT`

**根因：** 集成目标未链接时，`gtest_discover_tests` 注册占位测试。

**修复：**

1. 去掉对不可默认构造类型的 `map[]`，改 `insert_or_assign` / `emplace` / `at`
2. 修复 `find_by_user` / `find_by_account` override 默认参数
3. `gtest_discover_tests(... DISCOVERY_MODE PRE_TEST)`

### 4.2 与前序耦合

| 问题 | 处理 |
| ---- | ---- |
| Domain `CategoryBoard::Adjustment` 与 DB 仅 income/expense 冲突 | Domain 仅保留 Income/Expense；Adjustment 用 Expense board |
| env `PFH_*` 与旧无前缀名分叉 | `JsonConfigLoader` 优先 `PFH_*`，回退无前缀 |
| Domain 临时 TransactionId 与 store 序列冲突 | repository 推进序列 |
| Transfer group id 占位 0 | repository 分配真实 id |
| 金额符号 Domain vs DB 分叉 | Repository 边界映射 + Balance 双兼容 |

---

## 5. 验收对照

| 验收项 | 状态 |
| ------ | ---- |
| 业务写入与 outbox 同事务提交 | ✅ |
| 回滚不留业务/outbox 脏数据 | ✅ |
| 用户隔离 | ✅ |
| 乐观锁 | ✅ |
| 余额缓存命中/重建 | ✅ |
| 汇率 append-only + 历史查询 | ✅ |
| TransferAggregate 原子写 | ✅ |
| 禁止事务外写 | ✅ |
| 集成测试可构建并通过 | ✅ |
| 真实 Drogon/PostgreSQL 实现 | ⏳ 后续 |
| OutboxPublisherJob | ⏳ S11（Tasks #34） |

对应 Tasks：

- [x] #13 Repository 集成测试
- [x] #30 UnitOfWork（In-Memory 语义实现；Drogon 适配器待接线）
- [x] #31 User / UserPreference Repository
- [x] #32 Account / Transaction Repository
- [x] #33 ExchangeRate Repository
- [ ] #34 OutboxPublisherJob（S11）

后续状态（2026-07-15）：#34 已在 P1-S11 完成实现与本地门禁；本表保留 S08 交付当时的范围快照，真实 PostgreSQL/Drogon runtime 仍由 P1-S12 验证。

---

## 6. 本地验证

```powershell
cd e:\AMLY\works\C++\PFH
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

成功标志：

1. 无 `pfh_integration_tests_NOT_BUILT`
2. unit + integration 全部 Passed

---

## 7. 后续

1. 接入 Drogon/PostgreSQL，将 In-Memory 仓储替换为 SQL 实现
2. 同一批 integration scenarios 对真实测试库复跑
3. 进入 **S09 Application Use Cases**

---

## 8. 参考

- [Repository Design](../Architecture/05_Repository_and_Persistence_Design.md)
- [Database Design](../Architecture/02_Database_Design.md)
- [Event Design](../Architecture/14_Event_Design.md)
- [Phase 1 Detailed Plan](../Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md)
- [S07 Delivery Summary](Phase_1_S07_Delivery_Summary.md)
- [tasks](tasks.md)
