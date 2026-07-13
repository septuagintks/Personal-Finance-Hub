# Personal Finance Hub (PFH) - 开发者文档中心

欢迎来到 **Personal Finance Hub (PFH)** 的开发者文档中心。本目录用于维护项目的架构设计、开发计划、规范标准与阶段性修改记录，当前内容围绕一个基于现代 C++23 构建的个人财务聚合平台展开，采用 **Clean Architecture（清洁架构）** 与 **轻量级 DDD（领域驱动设计）** 思想，目标是提供高内聚、低耦合、强类型、高精度的财务管理后端。

---

## 1. 目录结构指南

根据 [`Docs/Guides/Directory_Guidance.md`](Guides/Directory_Guidance.md) 规范，当前仓库中已提交的文档结构如下：

```text
Docs/
├── README.md                           # 本文档（文档中心入口与现状综述）
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
│   ├── Phase_1_S01-S03_Delivery_Summary.md # Phase 1 S01-S03 工程骨架交付记录
│   ├── Phase_1_S04_Delivery_Summary.md # Phase 1 S04 基础类型与错误模型交付记录
│   ├── Phase_1_S05_Delivery_Summary.md # Phase 1 S05 金融原语交付记录
│   ├── Phase_1_S06_Delivery_Summary.md # Phase 1 S06 领域模型与领域服务交付记录
│   ├── Phase_1_S07_Delivery_Summary.md # Phase 1 S07 数据库迁移与持久化基础交付记录
│   ├── Phase_1_S08_Delivery_Summary.md # Phase 1 S08 Repository 与 Unit of Work 交付记录
│   ├── Phase_1_S09_Delivery_Summary.md # Phase 1 S09 Application Use Cases 交付记录
│   ├── Phase_1_S10_Delivery_Summary.md # Phase 1 S10 累计交付记录（进行中）
│   ├── Phase_1_S10_PostgreSQL_Persistence_Validation_Report.md # V3 外部复测最终报告
│   └── Tasks.md                        # 待办任务跟踪
│
├── Development_Plans/                  # 阶段性开发计划
│   ├── Overall_Development_Plan.md     # 三阶段总开发计划大纲
│   ├── Phase_1_Development_Plan.md     # Phase 1 后端最小闭环开发计划
│   ├── Phase_1/                        # Phase 1 细化子计划
│   │   └── Phase_1_Detailed_Development_Plan.md # Phase 1 具体开发顺序与测试收尾
│   ├── Phase_2_Development_Plan.md     # Phase 2 产品体验与稳定性计划
│   └── Phase_3_Development_Plan.md     # Phase 3 外部接入与预留扩展计划
│
├── Guides/                             # 实时指南与操作说明
│   ├── Directory_Guidance.md           # 当前文档目录结构指南
│   ├── Dependency_Installation_Guide.md # 依赖安装说明
│   ├── Database_Migration_Guide.md     # 数据库迁移操作指南（Flyway）
│   ├── Linux_Development_Workflow.md   # Linux 编译、运行与测试工作流
│   ├── Local_Test_Environment.md       # 历史 Linux 测试环境与当前复核状态
│   └── Quick_Reference.md              # 开发者快速参考（构建、测试、命令速查）
│
├── Standards/                          # 团队与文档规范
│   └── Documents_Format_Standard.md    # 文档格式与排版标准
│
└── Archive/                            # 已完成的优化记录与交付归档
    ├── Config_Env_Overlay_Design.md    # 配置环境变量覆盖设计与实现记录
    ├── Documents_Optimize_1.md         # 优化记录 1 (安全、多币种、I18n、异常、迁移)
    ├── Documents_Optimize_2.md         # 优化记录 2 (偏好存储、服务命名、手续费、事件、报表)
    └── Documents_Optimize_3.md         # 优化记录 3 (文档治理、任务清单、计划归档)
```

计划类文档按需创建并跟随任务推进维护：`Docs/Development/Documents_Optimize_Plan.md` 仅在有正在设计中的文档优化方案时创建；当前文档优化事项已归档到 `Docs/Archive/Documents_Optimize_3.md`。开发阶段交付记录在开发中存放于 `Docs/Development/`，推荐命名为 `Phase_<N>_S<start>-S<end>_Delivery_Summary.md`，验收完成后归档到 `Docs/Archive/`。总体路线以 [Development_Plans/Overall_Development_Plan.md](Development_Plans/Overall_Development_Plan.md) 为准，阶段性开发计划以 `Docs/Development_Plans/Phase_N_Development_Plan.md` 为准，开发任务跟踪以 [Development/Tasks.md](Development/Tasks.md) 为准。

---

## 2. 项目核心技术特性

1. **后端技术栈**：C++23 + Drogon 框架 + CMake + PostgreSQL 16+ + spdlog。
2. **金融原语**：绝不使用浮点数表示金额，统一采用高精度定点数 `Decimal`（`NUMERIC(20,8)`）与强类型 `Money`。
3. **多币种与三角折算**：以 USD 为固定枢纽货币，非 USD 货币对通过 `CurrencyConversionService` 在纯内存中进行三角折算，数据库仅存储 N-1 条 USD 汇率对。
4. **轻量级 CQRS**：写路径通过 Domain 实体和 Repository 保证强一致性；读路径（报表）绕过 Domain 实体，直接执行 SQL 聚合，并在 C++ 内存中进行汇率折算，针对大数据量提供 SQL 端提前折算优化。
5. **事务后事件派发 (Post-Commit Dispatch)**：在 `DrogonUnitOfWork` 物理 Commit 成功后才派发领域事件，防止事务回滚导致事件错误派发。
6. **全局异常拦截**：通过 Drogon 全局异常处理器捕获非预期异常，生成唯一 `TraceId`，在保障生产环境安全（不泄露敏感信息）的同时提供完整的服务端日志追溯。

当前进度（2026-07-13）：P1-S01 至 P1-S09 与 P1-S10-01 已完成，正在执行 P1-S10-02 及后续内容。Windows GCC 16 当前 271/271 通过；V3 已在外部 PostgreSQL 16.14 + Flyway 10.22.0 空库复测通过。Drogon/PostgreSQL 生产接线、REST API、后台任务和 P1-S12 完整真实环境验收尚未完成，不能据此视为 Phase 1 已完成。

---

## 3. 文档一致性与修正说明

最近的文档审查已经修正了多处跨文档冲突。当前入口文档按以下状态理解：

- **UserPreference 存储设计**：`02_Database_Design.md`、`03_Domain_Model_Design.md`、`04_Money_Currency_System_Design.md` 与 `05_Repository_and_Persistence_Design.md` 已统一为“领域对象由 `users.base_currency_code` 默认值与 `user_preferences` 扩展偏好组合映射”。
- **服务命名与职责边界**：应用层统一使用 `RefreshExchangeRatesUseCase` 负责调度和 I/O 编排，领域层统一使用 `CurrencyConversionService` 负责纯内存汇率折算。
- **手续费扣除灵活性**：`TransferDomainService` 三种构造模式均支持可选 `TransferFee`，覆盖源账户、目标账户与第三方账户扣费，并落为独立负 Adjustment。
- **事务与事件一致性**：`DrogonUnitOfWork` 的事务闭包必须绑定同一个数据库事务上下文，业务写入和 outbox 写入同事务提交，提交前不直接派发事件。
- **报表大数据折算优化**：`09_Reporting_and_Analytics_Design.md` 已补充 SQL 端提前折算方案；缺失汇率时返回明确错误，不使用 `0` 或 `1` 等默认值参与财务计算。

---

## 4. 开发者快速上手

1. **阅读顺序推荐**：先看 [Architecture/01_Technical_Architecture.md](Architecture/01_Technical_Architecture.md) 和 [Architecture/07_Workflow_and_Lifecycle_Design.md](Architecture/07_Workflow_and_Lifecycle_Design.md)，再看 [Architecture/04_Money_Currency_System_Design.md](Architecture/04_Money_Currency_System_Design.md)、[Architecture/06_Service_and_Use_Case_Design.md](Architecture/06_Service_and_Use_Case_Design.md)、[Architecture/08_Exchange_Rate_System_Design.md](Architecture/08_Exchange_Rate_System_Design.md)，最后补齐 [Architecture/02_Database_Design.md](Architecture/02_Database_Design.md) 与 [Architecture/05_Repository_and_Persistence_Design.md](Architecture/05_Repository_and_Persistence_Design.md)。
2. **阶段计划**：进入代码实现前，先阅读 [Development_Plans/Overall_Development_Plan.md](Development_Plans/Overall_Development_Plan.md) 与对应 Phase 开发计划，并以 [Development/Tasks.md](Development/Tasks.md) 跟踪进度。
3. **开发规范**：新文档或修改文档时，请遵守 [Standards/Documents_Format_Standard.md](Standards/Documents_Format_Standard.md)；目录树变更时，请同步更新 [Guides/Directory_Guidance.md](Guides/Directory_Guidance.md)。
4. **Linux 工作流**：最终部署目标为 Linux；P1-S10 开发期间按 [Guides/Linux_Development_Workflow.md](Guides/Linux_Development_Workflow.md) 保持可复现命令，并在 P1-S12 由另一台机器完成 Linux、Docker 与真实 PostgreSQL 阻断门禁。
