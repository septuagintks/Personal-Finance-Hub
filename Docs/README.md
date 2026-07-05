# Personal Finance Hub (PFH) - 开发者文档中心

欢迎来到 **Personal Finance Hub (PFH)** 的开发者文档中心。本目录用于维护项目的架构设计、开发计划、规范标准与阶段性修改记录，当前内容围绕一个基于现代 C++23 构建的个人财务聚合平台展开，采用 **Clean Architecture（清洁架构）** 与 **轻量级 DDD（领域驱动设计）** 思想，目标是提供高内聚、低耦合、强类型、高精度的财务管理后端。

---

## 📂 目录结构指南

根据 [Standards/directory.md](Standards/directory.md) 规范，当前文档库结构如下：

```text
Docs/
├── README.md
├── Architecture/
│   ├── 01_Technical_Architecture.md
│   ├── 02_Database_Design.md
│   ├── 03_Domain_Model_Design.md
│   ├── 04_Money_Currency_System_Design.md
│   ├── 05_Repository_and_Persistence_Design.md
│   ├── 06_Service_and_Use_Case_Design.md
│   ├── 07_Workflow_and_Lifecycle_Design.md
│   ├── 08_Exchange_Rate_System_Design.md
│   ├── 09_Reporting_and_Analytics_Design.md
│   ├── 10_REST_API_Design.md
│   ├── 11_Sync_Framework_Design.md
│   ├── 12_Scheduler_Design.md
│   ├── 13_Frontend_Design.md
│   ├── 14_Event_Design.md
│   ├── 15_Error_Handling_Design.md
│   └── 16_Testing_Strategy.md
├── Development/
│   └── tasks.md
├── Standards/
│   ├── directory.md
│   └── Documents_Format_Standard.md
└── Completed_Modifications/
   ├── Documents_Optimize_1.md
   └── Documents_Optimize_2.md
```

当前仓库中尚未单独创建 `Development/Documents_Optimize_Plan.md` 或 `Develop_Plan/` 目录，因此开发计划相关内容主要通过 [Development/tasks.md](Development/tasks.md) 统一跟踪。

---

## 🚀 项目核心技术特性

1. **后端技术栈**：C++23 + Drogon 框架 + CMake + PostgreSQL 16+ + spdlog。
2. **金融原语**：绝不使用浮点数表示金额，统一采用高精度定点数 `Decimal`（`NUMERIC(20,8)`）与强类型 `Money`。
3. **多币种与三角折算**：以 USD 为固定枢纽货币，非 USD 货币对通过 `CurrencyConversionService` 在纯内存中进行三角折算，数据库仅存储 N-1 条 USD 汇率对。
4. **轻量级 CQRS**：写路径通过 Domain 实体和 Repository 保证强一致性；读路径（报表）绕过 Domain 实体，直接执行 SQL 聚合，并在 C++ 内存中进行汇率折算，针对大数据量提供 SQL 端提前折算优化。
5. **事务后事件派发 (Post-Commit Dispatch)**：在 `DrogonUnitOfWork` 物理 Commit 成功后才派发领域事件，防止事务回滚导致事件错误派发。
6. **全局异常拦截**：通过 Drogon 全局异常处理器捕获非预期异常，生成唯一 `TraceId`，在保障生产环境安全（不泄露敏感信息）的同时提供完整的服务端日志追溯。

---

## 🔍 文档一致性与修正说明

在最近的文档审查中，我们已经整理并统一了以下内容，确保当前文档集内部表述一致：

* **UserPreference 存储冲突**：`03_Domain_Model_Design.md` 与 `05_Repository_and_Persistence_Design.md` 已统一为独立的 `user_preferences` 表。
* **服务命名与职责冲突**：应用层统一使用 `RefreshExchangeRatesUseCase`，领域层统一使用 `CurrencyConversionService`。
* **手续费扣除灵活性**：`TransferDomainService::buildTransfer` 已引入 `FeeSource`，覆盖源账户、目标账户与第三方账户扣费场景。
* **事务后触发健壮性**：`DrogonUnitOfWork` 的事件派发绑定在数据库连接真正 Commit 成功之后。
* **报表大数据折算优化**：`09_Reporting_and_Analytics_Design.md` 已补充 SQL 端提前折算方案，避免阻塞 Drogon Event Loop。

---

## 🛠️ 开发者快速上手

1. **阅读顺序推荐**：先看 [Architecture/01_Technical_Architecture.md](Architecture/01_Technical_Architecture.md) 和 [Architecture/07_Workflow_and_Lifecycle_Design.md](Architecture/07_Workflow_and_Lifecycle_Design.md)，再看 [Architecture/04_Money_Currency_System_Design.md](Architecture/04_Money_Currency_System_Design.md)、[Architecture/06_Service_and_Use_Case_Design.md](Architecture/06_Service_and_Use_Case_Design.md)、[Architecture/08_Exchange_Rate_System_Design.md](Architecture/08_Exchange_Rate_System_Design.md)，最后补齐 [Architecture/02_Database_Design.md](Architecture/02_Database_Design.md) 与 [Architecture/05_Repository_and_Persistence_Design.md](Architecture/05_Repository_and_Persistence_Design.md)。
2. **开发规范**：新文档或修改文档时，请遵守 [Standards/Documents_Format_Standard.md](Standards/Documents_Format_Standard.md)；目录树变更时，请同步更新 [Standards/directory.md](Standards/directory.md)。
