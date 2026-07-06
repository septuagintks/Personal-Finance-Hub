# Personal Finance Hub (PFH) - 开发者文档中心

欢迎来到 **Personal Finance Hub (PFH)** 的开发者文档中心。本目录用于维护项目的架构设计、开发计划、规范标准与阶段性修改记录，当前内容围绕一个基于现代 C++23 构建的个人财务聚合平台展开，采用 **Clean Architecture（清洁架构）** 与 **轻量级 DDD（领域驱动设计）** 思想，目标是提供高内聚、低耦合、强类型、高精度的财务管理后端。

---

## 1. 目录结构指南

根据 [`Docs/Standards/directory.md`](Standards/directory.md) 规范，当前仓库中已提交的文档结构如下：

```text
Docs/
├── README.md                           # 本文档（文档中心入口与现状综述）
│
├── Architecture/                       # 核心技术架构与详细设计规约
│   ├── 01_Technical_Architecture.md    # 技术架构总览 (Clean Architecture)
│   ├── 02_Database_Design.md           # 数据库设计（PostgreSQL 16+，包含 Flyway 迁移）
│   ├── 03_Domain_Model_Design.md       # 领域模型设计 (Domain Entities & Aggregates)
│   ├── 04_Money_Currency_System_Design.md # 核心金融原语设计（Decimal、Money、多币种）
│   ├── 05_Repository_and_Persistence_Design.md # 仓储与持久化设计（Repository、Unit of Work）
│   ├── 06_Service_and_Use_Case_Design.md # 领域服务与应用层用例设计 (TransferDomainService)
│   ├── 07_Workflow_and_Lifecycle_Design.md # 核心工作流与生命周期（时序、I18n 初始化）
│   ├── 08_Exchange_Rate_System_Design.md # 汇率系统设计（三角折算、降级策略）
│   ├── 09_Reporting_and_Analytics_Design.md # 报表与分析设计（轻量级 CQRS、大数据折算优化）
│   ├── 10_REST_API_Design.md           # REST API 接口规约 (JWT 认证、黑名单)
│   ├── 11_Sync_Framework_Design.md     # 外部平台同步框架设计
│   ├── 12_Scheduler_Design.md          # 调度器与后台任务设计
│   ├── 13_Frontend_Design.md           # 前端设计规约（Vue 3 + ECharts）
│   ├── 14_Event_Design.md              # 领域事件设计（Post-Commit Dispatch 机制）
│   ├── 15_Error_Handling_Design.md     # 全局异常与错误处理规约（Drogon Exception Boundary）
│   └── 16_Testing_Strategy.md          # 测试策略与规范
│
├── Development/                        # 开发过程管理
│   └── tasks.md                        # 待办任务跟踪
│
├── Develop_Plan/                       # 阶段性开发计划
│   ├── Overall_Development_Plan.md     # 三阶段总开发计划大纲
│   └── Phase_1_Development_Plan.md     # Phase 1 后端最小闭环开发计划
│
├── Standards/                          # 团队与文档规范
│   ├── directory.md                    # 文档目录结构规范
│   └── Documents_Format_Standard.md    # 文档格式与排版标准
│
└── Completed_Modifications/            # 已完成的架构优化记录
    ├── Documents_Optimize_1.md         # 优化记录 1 (安全、多币种、I18n、异常、迁移)
    ├── Documents_Optimize_2.md         # 优化记录 2 (偏好存储、服务命名、手续费、事件、报表)
    └── Documents_Optimize_3.md         # 优化记录 3 (文档治理、任务清单、计划归档)
```

计划类文档按需创建并跟随任务推进维护：`Docs/Development/Documents_Optimize_Plan.md` 仅在有正在设计中的文档优化方案时创建；当前文档优化事项已归档到 `Docs/Completed_Modifications/Documents_Optimize_3.md`。总体路线以 [Develop_Plan/Overall_Development_Plan.md](Develop_Plan/Overall_Development_Plan.md) 为准，阶段性开发计划以 [Develop_Plan/Phase_1_Development_Plan.md](Develop_Plan/Phase_1_Development_Plan.md) 为准，开发任务跟踪以 [Development/tasks.md](Development/tasks.md) 为准。

---

## 2. 项目核心技术特性

1. **后端技术栈**：C++23 + Drogon 框架 + CMake + PostgreSQL 16+ + spdlog。
2. **金融原语**：绝不使用浮点数表示金额，统一采用高精度定点数 `Decimal`（`NUMERIC(20,8)`）与强类型 `Money`。
3. **多币种与三角折算**：以 USD 为固定枢纽货币，非 USD 货币对通过 `CurrencyConversionService` 在纯内存中进行三角折算，数据库仅存储 N-1 条 USD 汇率对。
4. **轻量级 CQRS**：写路径通过 Domain 实体和 Repository 保证强一致性；读路径（报表）绕过 Domain 实体，直接执行 SQL 聚合，并在 C++ 内存中进行汇率折算，针对大数据量提供 SQL 端提前折算优化。
5. **事务后事件派发 (Post-Commit Dispatch)**：在 `DrogonUnitOfWork` 物理 Commit 成功后才派发领域事件，防止事务回滚导致事件错误派发。
6. **全局异常拦截**：通过 Drogon 全局异常处理器捕获非预期异常，生成唯一 `TraceId`，在保障生产环境安全（不泄露敏感信息）的同时提供完整的服务端日志追溯。

---

## 3. 文档一致性与修正说明

最近的文档审查已经修正了多处跨文档冲突。当前入口文档按以下状态理解：

* **UserPreference 存储设计**：`02_Database_Design.md`、`03_Domain_Model_Design.md`、`04_Money_Currency_System_Design.md` 与 `05_Repository_and_Persistence_Design.md` 已统一为“领域对象由 `users.base_currency_code` 默认值与 `user_preferences` 扩展偏好组合映射”。
* **服务命名与职责边界**：应用层统一使用 `RefreshExchangeRatesUseCase` 负责调度和 I/O 编排，领域层统一使用 `CurrencyConversionService` 负责纯内存汇率折算。
* **手续费扣除灵活性**：`TransferDomainService::buildTransfer` 已引入 `FeeSource`，覆盖源账户、目标账户与第三方账户扣费场景。
* **事务与事件一致性**：`DrogonUnitOfWork` 的事务闭包必须绑定同一个数据库事务上下文，业务写入和 outbox 写入同事务提交，提交前不直接派发事件。
* **报表大数据折算优化**：`09_Reporting_and_Analytics_Design.md` 已补充 SQL 端提前折算方案；缺失汇率时返回明确错误，不使用 `0` 或 `1` 等默认值参与财务计算。

---

## 4. 开发者快速上手

1. **阅读顺序推荐**：先看 [Architecture/01_Technical_Architecture.md](Architecture/01_Technical_Architecture.md) 和 [Architecture/07_Workflow_and_Lifecycle_Design.md](Architecture/07_Workflow_and_Lifecycle_Design.md)，再看 [Architecture/04_Money_Currency_System_Design.md](Architecture/04_Money_Currency_System_Design.md)、[Architecture/06_Service_and_Use_Case_Design.md](Architecture/06_Service_and_Use_Case_Design.md)、[Architecture/08_Exchange_Rate_System_Design.md](Architecture/08_Exchange_Rate_System_Design.md)，最后补齐 [Architecture/02_Database_Design.md](Architecture/02_Database_Design.md) 与 [Architecture/05_Repository_and_Persistence_Design.md](Architecture/05_Repository_and_Persistence_Design.md)。
2. **阶段计划**：进入代码实现前，先阅读 [Develop_Plan/Overall_Development_Plan.md](Develop_Plan/Overall_Development_Plan.md) 与 [Develop_Plan/Phase_1_Development_Plan.md](Develop_Plan/Phase_1_Development_Plan.md)，并以 [Development/tasks.md](Development/tasks.md) 跟踪进度。
3. **开发规范**：新文档或修改文档时，请遵守 [Standards/Documents_Format_Standard.md](Standards/Documents_Format_Standard.md)；目录树变更时，请同步更新 [Standards/directory.md](Standards/directory.md)。
