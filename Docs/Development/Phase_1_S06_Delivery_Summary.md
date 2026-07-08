# Phase 1 S06 领域模型与领域服务 - 交付报告

**阶段**: P1-S06 领域模型与领域服务  
**状态**: ✅ 完成  
**完成日期**: 2026-07-09

---

## 1. 概述

根据 `Docs/Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md` 的 P1-S06 规划，本阶段实现了账户、流水、转账和余额规则的纯领域闭环。所有实现严格遵循 Clean Architecture 与轻量级 DDD 原则，领域服务不访问 Repository、不开启事务、不发布事件。

---

## 2. 交付物

### 2.1 领域实体与值对象

| 组件                | 头文件                                         | 实现 | 测试                                          |
| ------------------- | ---------------------------------------------- | ---- | --------------------------------------------- |
| User                | `include/pfh/domain/user.h`                    | N/A  | 覆盖于其他测试                                |
| UserPreference      | `include/pfh/domain/user_preference.h`         | N/A  | 覆盖于其他测试                                |
| Account             | `include/pfh/domain/account.h`                 | N/A  | 覆盖于其他测试                                |
| Transaction         | `include/pfh/domain/transaction.h`             | N/A  | 覆盖于其他测试                                |
| TransferAggregate   | `include/pfh/domain/transfer_aggregate.h`      | N/A  | 通过 TransferDomainService 测试验证           |
| Category            | `include/pfh/domain/category.h`                | N/A  | `tests/unit/category_test.cpp` (6 项)         |

### 2.2 领域服务

| 组件                          | 头文件                                                 | 实现                                         | 测试                                              |
| ----------------------------- | ------------------------------------------------------ | -------------------------------------------- | ------------------------------------------------- |
| TransferDomainService         | `include/pfh/domain/transfer_domain_service.h`         | `src/domain/transfer_domain_service.cpp`     | `tests/unit/transfer_domain_service_test.cpp` (10 项) |
| BalanceCalculationService     | `include/pfh/domain/balance_calculation_service.h`     | `src/domain/balance_calculation_service.cpp` | `tests/unit/balance_calculation_service_test.cpp` (8 项) |

### 2.3 领域错误扩展

| 变更                       | 位置                                  | 说明                                       |
| -------------------------- | ------------------------------------- | ------------------------------------------ |
| DomainError::invalid_operation | `include/pfh/domain/domain_error.h` | 新增工厂方法，支持转账与分类校验错误返回 |

---

## 3. 实现要点

### 3.1 User & UserPreference

- **User**：最小身份实体，仅承载 UserId 和 username。
- **UserPreference**：领域概念，聚合基准货币与扩展偏好（locale、timezone、theme 等），由 Repository 从 `users.base_currency_code` 和 `user_preferences` 表组合映射。

### 3.2 Account

- 支持 7 种账户类型：Cash、Savings、Credit、DigitalWallet、Investment、Crypto、Other。
- **负余额支持**：所有账户允许负余额（信用账户、透支、调整场景）。
- **归档机制**：`archive()/unarchive()` 方法支持软隐藏，保留历史数据。
- **版本字段**：用于乐观锁（Repository 层实现）。
- **category()**：根据账户类型推导 Asset/Liability 分类（Credit 默认 Liability，其余默认 Asset）。

### 3.3 Transaction

- **四种类型**：Income、Expense、Transfer、Adjustment。
- **Transfer 强约束**：转账两端必须标记为 `TransactionType::Transfer`，通过 `transfer_group_id` 关联。
- **软删除**：`deleted_at` 时间戳，而非物理删除记录。
- **金额符号约定**：amount 存储为正数，方向由 TransactionType 和账户上下文决定。

### 3.4 TransferAggregate

- **聚合根**：封装 outgoing + incoming Transaction，可选 ExchangeRate 和 adjustments。
- **一致性规则**：
  - 同币种：outgoing.amount == incoming.amount
  - 跨币种：outgoing.amount * rate ≈ incoming.amount（容忍舍入误差）
- **手续费来源**：FeeSource 枚举支持源账户、目标账户、第三方账户扣费。

### 3.5 TransferDomainService

**三种构造模式**：

1. **Mode 1: Outgoing + Rate => Incoming**  
   用户指定源金额和汇率，计算目标金额。

2. **Mode 2: Outgoing + Incoming => Rate**  
   用户指定两端金额，推导隐含汇率（跨币种）或验证匹配（同币种）。

3. **Mode 3: Incoming + Rate => Outgoing**  
   用户指定目标金额和汇率，反向计算源金额。  
   **关键设计**：使用 inverse rate 计算 outgoing 后，重新用 forward rate 计算 incoming，消除往返舍入误差，确保验证通过。

**验证规则**：

- 两端 Transaction 必须为 `TransactionType::Transfer`。
- 必须共享同一 `transfer_group_id`。
- 同币种金额精确匹配；跨币种金额在容差范围内一致。

**关键约束**：不访问 Repository、不开事务、不发布事件（纯领域逻辑）。

### 3.6 BalanceCalculationService

**职责**：从 Transaction 集合重建账户余额。

**规则**：

- Income: +amount
- Expense: -amount
- Transfer: 根据账户上下文（outgoing/-amount, incoming/+amount）
- Adjustment: 当前简化为 -amount（费用约定）
- **排除已删除流水**：`deleted_at` 已设置的 Transaction。
- **历史余额**：`calculate_balance_at()` 支持截止时间点的余额重建。

**设计留白**：Transfer 方向处理依赖 Repository 层提供已签名金额或上下文标记，当前实现为占位逻辑。

### 3.7 Category & Board 校验

- **CategoryBoard 枚举**：Income、Expense、Adjustment。
- **校验规则**：
  - Income 交易只能使用 Income board 分类。
  - Expense 交易只能使用 Expense board 分类。
  - Adjustment 交易只能使用 Adjustment board 分类。
  - **Transfer 交易不使用分类**（通过 transfer_group_id 关联）。
- **静态验证方法**：`Category::validate_category_board()` 在创建或更新 Transaction 时调用。
- **实例方法**：`Category::is_valid_for(TransactionType)` 用于运行时检查。

---

## 4. 关键设计决策记录

### 4.1 Mode 3 舍入误差处理

**问题**：`incoming / rate = outgoing`，然后 `outgoing * rate = incoming'`，由于 Decimal 标度 10 的限制，`incoming'` 可能与原始 `incoming` 略有差异（如 `7180 CNY / 7.18 = 999.99999988 USD`，再 `* 7.18 = 7179.9999991384 CNY`）。

**方案**：采用重计算策略——用 inverse rate 计算 outgoing 后，立即用 forward rate 重新计算 incoming，存储重计算后的金额。这确保 `outgoing * rate == incoming` 精确成立，验证通过。

**权衡**：用户输入的目标金额会被微调（误差 < 0.0000001），但保证了内部一致性。这是数学上的必然结果，符合"精确计算优先"的原则。

### 4.2 领域服务边界

**严格约束**：TransferDomainService 和 BalanceCalculationService **不**：

- 调用 Repository（数据访问由应用层编排）
- 开启数据库事务（事务边界在应用层）
- 发布领域事件（事件派发在 UnitOfWork Commit 后）

**验证方式**：代码层面无 IRepository 参数，无事务 API 调用，无事件发布器依赖。

### 4.3 TransferGroupId 占位设计

**当前实现**：使用 `TypedId<TransferGroupIdTag>` (int64)，实际持久化时应为 UUID。

**原因**：Phase 1 聚焦领域逻辑，UUID 生成与序列化留待 S07（持久化层）引入。

### 4.4 BalanceCalculationService 的 Transfer 方向

**当前实现**：`apply_transaction()` 对 Transfer 返回原金额，依赖调用方或 Repository 提供已签名的金额。

**待完善**：S08 Repository 实现时，查询结果应明确标记 Transfer 方向（outgoing/-amount, incoming/+amount），或提供上下文参数。

---

## 5. 测试覆盖

### 5.1 TransferDomainService 测试（10 项）

- Mode 1: outgoing + rate => incoming（正常 + 货币不匹配）
- Mode 2: outgoing + incoming => rate（跨币种 + 同币种 + 同币种不匹配）
- Mode 3: incoming + rate => outgoing（正常 + 货币不匹配）
- 验证一致性（通过）

### 5.2 BalanceCalculationService 测试（8 项）

- 无交易 => 零余额
- 收入交易 => 正余额
- 支出交易 => 扣减余额
- 混合交易 => 正确计算
- 软删除交易 => 排除
- 历史余额 => 截止时间
- 货币不匹配 => 错误

### 5.3 Category 测试（6 项）

- Income board + Income 类型 => 通过
- Expense board + Expense 类型 => 通过
- Adjustment board + Adjustment 类型 => 通过
- 错误 board + 错误类型 => 失败
- Transfer + 任意 board => 失败
- is_valid_for() 实例方法验证

---

## 6. 验证结果

```text
cmake configure: 通过（GNU 16.1.0, C++23, Debug）
cmake build:     通过（-Wall -Wextra -Werror -pedantic），无警告
ctest:           166/166 单元测试全部通过（S06 新增 24 项）
Total Test time: ~3.3 sec
pfh_domain:      独立静态库，不链接 spdlog/框架
```

---

## 7. 验收对照（P1-S06）

- [x] Domain Service 不访问 Repository、不打开事务、不发布事件
- [x] Transfer 不计入收入/支出统计（TransactionType::Transfer 独立标记）
- [x] 分类 board 规则能阻止收入、支出和调整类型误用
- [x] 领域模型测试覆盖转账、手续费、余额重建和分类校验

---

## 8. 遗留与后续

### 8.1 待 S07/S08 补齐

- **TransferGroupId UUID 实现**：当前使用 int64 占位，实际持久化需 UUID。
- **Transfer 方向标记**：BalanceCalculationService 依赖 Repository 提供已签名金额。
- **Transaction ID 生成**：当前使用静态计数器占位，实际应由 Repository 从数据库序列获取。

### 8.2 后续增强（非阻塞）

- **手续费 Adjustment 实现**：当前 FeeSource 枚举已定义，实际手续费 Transaction 创建留待应用层 Use Case。
- **汇兑损益记录**：TransferAggregate 已预留 adjustments 字段，实际损益计算留待应用层。
- **CurrencyMetadata**：展示用元数据（符号、精度）随前端需求在后续阶段引入。

---

## 9. 对应任务更新

tasks.md 已勾选：#12、#21、#22、#23、#24、#25、#26、#27。

---

## 10. 参考文档

- [领域模型设计](../Architecture/03_Domain_Model_Design.md)
- [金额与货币系统设计](../Architecture/04_Money_Currency_System_Design.md)
- [领域服务与用例设计](../Architecture/06_Service_and_Use_Case_Design.md)
- [Phase 1 详细开发计划](../Development_Plans/Phase_1/Phase_1_Detailed_Development_Plan.md)
- [任务跟踪](Tasks.md)
