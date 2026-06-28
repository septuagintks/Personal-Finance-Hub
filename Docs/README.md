# Personal Finance Hub (PFH) - 开发者文档中心

欢迎来到 **Personal Finance Hub (PFH)** 的开发者文档中心。本项目是一个基于现代 C++23 构建的个人财务聚合平台，采用 **Clean Architecture（清洁架构）** 与 **轻量级 DDD（领域驱动设计）** 思想，旨在提供高内聚、低耦合、强类型、高精度的财务管理后端。

---

## 📂 目录结构指南

根据 [`Docs/Standards/directory.md`](Standards/directory.md) 规范，文档库结构如下：

```text
Docs/
├── README.md                           # 本文档（文档中心入口与现状综述）
│
├── Architecture/                       # 核心技术架构与详细设计规约
│   ├── 01_Technical_Architecture.md    # 技术架构总览 (Clean Architecture)
│   ├── 02_Database_Design.md           # 数据库设计 (PostgreSQL 16+, 包含 Flyway 迁移)
│   ├── 03_Domain_Model_Design.md       # 领域模型设计 (Domain Entities & Aggregates)
│   ├── 04_Money_Currency_System_Design.md # 核心金融原语设计 (Decimal, Money, 多币种)
│   ├── 05_Repository_and_Persistence_Design.md # 仓储与持久化设计 (Repository, Unit of Work)
│   ├── 06_Service_and_Use_Case_Design.md # 领域服务与应用层用例设计 (TransferDomainService)
│   ├── 07_Workflow_and_Lifecycle_Design.md # 核心工作流与生命周期 (时序、I18n 初始化)
│   ├── 08_Exchange_Rate_System_Design.md # 汇率系统设计 (三角折算、降级策略)
│   ├── 09_Reporting_and_Analytics_Design.md # 报表与分析设计 (轻量级 CQRS, 大数据折算优化)
│   ├── 10_REST_API_Design.md           # REST API 接口规约 (JWT 认证、黑名单)
│   ├── 11_Sync_Framework_Design.md     # 外部平台同步框架设计
│   ├── 12_Scheduler_Design.md          # 调度器与后台任务设计
│   ├── 13_Frontend_Design.md           # 前端设计规约 (Vue3 + ECharts)
│   ├── 14_Event_Design.md              # 领域事件设计 (Post-Commit Dispatch 机制)
│   ├── 15_Error_Handling_Design.md     # 全局异常与错误处理规约 (Drogon Exception Boundary)
│   └── 16_Testing_Strategy.md          # 测试策略与规范
│
├── Development/                        # 开发过程管理
│   ├── tasks.md                        # 待办任务跟踪
│   └── Documents_Optimize_Plan.md      # 文档优化计划
│
├── Develop_Plan/                       # 迭代与开发计划（新增）
│
├── Standards/                          # 团队与文档规范
│   ├── directory.md                    # 文档目录结构规范
│   └── Documents_Format_Standard.md    # 文档格式与排版标准
│
└── Completed_Modifications/            # 已完成的架构优化记录
    ├── Documents_Optimize_1.md         # 优化记录 1 (安全、多币种、I18n、异常、迁移)
    └── Documents_Optimize_2.md         # 优化记录 2 (偏好存储、服务命名、手续费、事件、报表)
```

---

## 🚀 项目核心技术特性

1. **后端技术栈**：C++23 + Drogon 框架 + CMake + PostgreSQL 16+ + spdlog。
2. **金融原语**：绝不使用浮点数表示金额，统一采用高精度定点数 `Decimal`（`NUMERIC(20,8)`）与强类型 `Money`。
3. **多币种与三角折算**：以 USD 为固定枢纽货币，非 USD 货币对通过 `CurrencyConversionService` 在纯内存中进行三角折算，数据库仅存储 N-1 条 USD 汇率对。
4. **轻量级 CQRS**：写路径通过 Domain 实体和 Repository 保证强一致性；读路径（报表）绕过 Domain 实体，直接执行 SQL 聚合，并在 C++ 内存中进行汇率折算，针对大数据量提供 SQL 端提前折算优化。
5. **事务后事件派发 (Post-Commit Dispatch)**：在 `DrogonUnitOfWork` 物理 Commit 成功后才派发领域事件，防止事务回滚导致事件错误派发。
6. **全局异常拦截**：通过 Drogon 全局异常处理器捕获非预期异常，生成唯一 `TraceId`，在保障生产环境安全（不泄露敏感信息）的同时提供完整的服务端日志追溯。

---

## 🔍 文档一致性与冲突修正说明

在最近的文档审查中，我们发现并修正了以下核心冲突，确保文档与代码实现高度一致：

* **UserPreference 存储冲突**：修正了 `05_Repository_and_Persistence_Design.md` 与 `03_Domain_Model_Design.md` 中关于 `UserPreference` 是否拥有独立表的矛盾。现已明确**保留独立的 `user_preferences` 表**，由 `IUserRepository` 联合查询或通过独立的 `IUserPreferenceRepository` 进行读写。
* **服务命名与职责冲突**：彻底删除了文档中关于 `ExchangeRateService` 和 `AccountingService` 的模糊表述。统一规范为：应用层使用 **`RefreshExchangeRatesUseCase`**（负责调度与 I/O），领域层使用 **`CurrencyConversionService`**（负责纯内存折算）。
* **手续费扣除灵活性**：在 `TransferDomainService::buildTransfer` 中引入了 `FeeSource`（源账户扣除、目标账户扣除、第三方账户扣除），增强了手续费扣除的业务灵活性。
* **事务后触发健壮性**：在 `DrogonUnitOfWork` 中，明确事件派发必须绑定在底层数据库连接真正 Commit 成功的回调中，避免事务物理提交失败时错误派发事件。
* **报表大数据折算优化**：在 `09_Reporting_and_Analytics_Design.md` 中补充了大数据量下的折算方案，对于日/月度聚合报表，在 SQL 端通过 `JOIN exchange_rates` 提前折算，避免阻塞 Drogon 的 Event Loop。

---

## 🛠️ 开发者快速上手

1. **阅读顺序推荐**：
   * 了解整体架构：`01_Technical_Architecture.md` -> `07_Workflow_and_Lifecycle_Design.md`
   * 掌握核心业务：`04_Money_Currency_System_Design.md` -> `06_Service_and_Use_Case_Design.md` -> `08_Exchange_Rate_System_Design.md`
   * 数据库与持久化：`02_Database_Design.md` -> `05_Repository_and_Persistence_Design.md`
2. **开发规范**：
   * 编写新文档或修改文档时，请严格遵守 `Docs/Standards/Documents_Format_Standard.md`。
   * 保持 `Docs/Standards/directory.md` 的目录树与实际文件结构同步。
