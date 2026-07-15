# Phase 1 S09 Application Use Cases - 交付记录

**完成日期**: 2026-07-12
**阶段**: P1-S09 Application Use Case
**状态**: ✅ 用例与报表查询服务已落地，并通过 use case 单元测试设计覆盖

---

## 1. 概述

本阶段把 Domain 规则、Repository 与 Unit of Work 编排成可供 API 调用的应用用例。

交付范围：

- `CreateTransactionUseCase` / `DeleteTransactionUseCase`
- `CreateTransferUseCase`
- `RefreshExchangeRatesUseCase`（含 provider 降级）
- 账户查询 / 余额查询
- `ReportQueryService`（net worth / cash flow / dashboard summary）
- Domain/Repository → Application 错误映射

---

## 2. 交付物

| 文件 | 说明 |
| ---- | ---- |
| `include/pfh/application/error_mapping.h` | Domain/Repository 错误映射到 application::Error |
| `include/pfh/application/dto.h` | 命令与 DTO（金额为字符串） |
| `include/pfh/application/ports/i_exchange_rate_provider.h` | 外部汇率 Provider 端口 |
| `include/pfh/application/use_cases/create_transaction_use_case.h` | 创建流水 |
| `include/pfh/application/use_cases/delete_transaction_use_case.h` | 软删除流水 |
| `include/pfh/application/use_cases/create_transfer_use_case.h` | 创建转账（三种输入模式） |
| `include/pfh/application/use_cases/account_query_use_cases.h` | 账户列表 / 余额查询 |
| `include/pfh/application/use_cases/refresh_exchange_rates_use_case.h` | 汇率刷新 + 降级 |
| `include/pfh/application/query/report_query_service.h` | 报表查询 |
| `tests/unit/use_case_test.cpp` | 用例单元测试 |

---

## 3. 关键设计

### 3.1 Use Case 职责

- 权限/归属校验（`find_by_id_for_user`）
- 事务边界（`IUnitOfWork::execute_in_transaction`）
- Repository 编排
- Domain Service 调用（TransferDomainService / CurrencyConversionService）
- Outbox 事件登记（提交前不派发）

### 3.2 错误映射

| 来源 | 应用层 |
| ---- | ------ |
| Domain validation / rule | Validation / DomainRuleViolation |
| Repository NotFound | NotFound（跨用户不泄露存在性） |
| Repository Conflict | Conflict |
| Repository DatabaseError | InfrastructureFailure（不泄露 SQL） |

### 3.3 报表规则

- Cash flow **显式排除 Transfer**
- Net worth 将账户余额折算到用户 base currency
- 缺失汇率返回明确错误，不用 `0/1` 默认值

### 3.4 汇率刷新降级

Provider 失败时：

- 不写库
- 返回 `degraded=true`
- 保留历史汇率可用

---

## 4. 验收对照

| 验收项 | 状态 |
| ------ | ---- |
| Use Case 负责权限校验、事务边界、Repository 编排 | ✅ |
| Domain Rule Violation 稳定映射 | ✅ |
| Infrastructure Failure 不泄露数据库细节 | ✅ |
| 报表不绕过金额/汇率规则，Transfer 排除 cash flow | ✅ |
| 金额输入/输出为字符串 | ✅ DTO/Command |

Tasks：

- [x] #35 Create/Delete Transaction UseCase
- [x] #36 CreateTransferUseCase
- [x] #37 RefreshExchangeRatesUseCase
- [x] #38 账户/余额查询
- [x] #39 报表 QueryService

---

## 5. 本地验证

```powershell
cd e:\AMLY\works\C++\PFH
cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug
cmake --build build -j
ctest --test-dir build --output-on-failure
```

重点：`UseCaseTest.*` 与既有 unit/integration 全绿。

---

## 6. 后续

1. S10 REST API / JWT 把这些用例接到 Controller
2. 真实 Provider 实现替换 Mock
3. Drogon SQL Repository 替换 In-Memory

---

## 7. 参考

- [Service and Use Case Design](../Architecture/06_Service_and_Use_Case_Design.md)
- [Reporting Design](../Architecture/09_Reporting_and_Analytics_Design.md)
- [Error Handling Design](../Architecture/15_Error_Handling_Design.md)
- [S08 Delivery Summary](Phase_1_S08_Delivery_Summary.md)
- [tasks](../Development/tasks.md)
