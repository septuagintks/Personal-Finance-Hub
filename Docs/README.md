# Personal Finance Hub 开发者文档中心

PFH 文档按“当前设计、开发计划、操作指南、稳定规范、完成归档”组织。工作树只保留当前实现和后续开发仍需读取的信息；过程讨论和中间状态通过 Git 历史追溯。

---

## 1. 文档结构

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
│   ├── 10_REST_API_OpenAPI.json
│   ├── 11_Sync_Framework_Design.md
│   ├── 12_Scheduler_Design.md
│   ├── 13_Frontend_Design.md
│   ├── 14_Event_Design.md
│   ├── 15_Error_Handling_Design.md
│   └── 16_Testing_Strategy.md
├── Development_Plans/
│   ├── Overall_Development_Plan.md
│   ├── Phase_1/Phase_1_Development_Plan.md
│   ├── Phase_2/Phase_2_Development_Plan.md
│   └── Phase_3/Phase_3_Development_Plan.md
├── Guides/
│   ├── Database_Migration_Guide.md
│   ├── Dependency_Installation_Guide.md
│   ├── Directory_Guidance.md
│   ├── Linux_Development_Workflow.md
│   ├── Ubuntu_Server_Deployment_Guide.md
│   └── Quick_Reference.md
├── Standards/
│   └── Documents_Format_Standard.md
└── Archive/
    ├── Documents_Optimization_Summary.md
    ├── Phase_1_Development_Record.md
    ├── Phase_1_S01-S04_Delivery_Summary.md
    ├── Phase_1_S05-S08_Delivery_Summary.md
    ├── Phase_1_S09-S12_Delivery_Summary.md
    ├── Phase_2_Development_Record.md
    ├── Phase_2_S01-S04_Delivery_Summary.md
    ├── Phase_2_S05-S08_Delivery_Summary.md
    └── Phase_2_S09-S12_Delivery_Summary.md
```

---

## 2. 阅读入口

### 2.1 开始开发

1. [总体开发计划](Development_Plans/Overall_Development_Plan.md)
2. 对应 Phase 的开发计划
3. [技术架构](Architecture/01_Technical_Architecture.md)
4. 相关领域设计与 OpenAPI
5. [快速参考](Guides/Quick_Reference.md)

### 2.2 Phase 1 结果

- [开发路线与最终边界](Archive/Phase_1_Development_Record.md)
- [S01-S04 工程基础](Archive/Phase_1_S01-S04_Delivery_Summary.md)
- [S05-S08 金融与持久化](Archive/Phase_1_S05-S08_Delivery_Summary.md)
- [S09-S12 应用、API 与最终签署](Archive/Phase_1_S09-S12_Delivery_Summary.md)

### 2.3 Phase 2 结果

- [开发路线与最终边界](Archive/Phase_2_Development_Record.md)
- [S01-S04 工程、契约与会话](Archive/Phase_2_S01-S04_Delivery_Summary.md)
- [S05-S08 日常记账工作流](Archive/Phase_2_S05-S08_Delivery_Summary.md)
- [S09-S12 分析、维护与最终签署](Archive/Phase_2_S09-S12_Delivery_Summary.md)

### 2.4 日常操作

- 配置变量：[`config/README.md`](../config/README.md)
- 依赖安装：[Dependency Installation](Guides/Dependency_Installation_Guide.md)
- Linux 构建与测试：[Linux Development Workflow](Guides/Linux_Development_Workflow.md)
- Ubuntu 服务器部署：[Ubuntu Server Deployment Guide](Guides/Ubuntu_Server_Deployment_Guide.md)
- 数据库迁移：[Database Migration Guide](Guides/Database_Migration_Guide.md)

---

## 3. 当前项目状态

Phase 1 后端基线已经完成并通过：

- Windows Debug/Release PostgreSQL OFF 349/349。
- Linux Debug/Release production ON 351/351。
- PostgreSQL 16.14、Flyway V1-V6、真实 Drogon API 与 12 个数据库 scenarios。
- FreeCurrencyAPI 主源、exchangerate.fun 整批备用和历史降级。
- Ubuntu 24.04 Docker 冷构建、non-root、双角色、FORCE RLS、Outbox/Scheduler 与优雅停止。

Phase 2 已完成并通过最终目标环境验收：Web 覆盖认证、账户、分类、标签、偏好、日常流水、Transfer 聚合、Dashboard、维度报表、CSV 导出、个人审计、余额维护和 Operator 运维面；数据库迁移已演进到 V10。Windows Debug/Release PostgreSQL OFF 为 382/382，Linux production ON 为 384/384，Vitest/MSW 为 63/63，Chromium/Firefox/WebKit 为 111/111；Docker、Provider、Outbox/Scheduler、恢复和回滚均通过。Phase 3 保留给账单导入与支付平台生态。

汇率实时能力当前覆盖 20 种法币与 BTC。其他 12 种加密货币没有实时保证，系统会返回完整历史降级或明确不可用；完整加密货币定价不在当前计划内。

---

## 4. 核心规则

- 金额和汇率不经二进制浮点。
- Domain Service 不访问 Repository、不打开事务、不发布事件。
- Application 负责权限、事务和用例编排。
- 业务事实与 Outbox 在同一数据库事务提交。
- 租户表使用 `user_id`、复合约束与 FORCE RLS。
- request/background 数据库角色分离。
- API 金额使用十进制字符串，错误响应稳定且脱敏。
- Drogon Event Loop 不执行阻塞网络、数据库或长任务。

---

## 5. 维护原则

- 架构行为只在 `Architecture/` 维护。
- Phase 范围和顺序只在 `Development_Plans/` 维护。
- 构建、配置和运行命令只在 `Guides/` 或 `config/README.md` 维护。
- 完成阶段只保留一份开发记录和少量交付摘要。
- 不为普通已修复缺陷保留单独文档。
- 目录变化同步更新本文件与 `Directory_Guidance.md`。
- 文档格式遵循 [Documents Format Standard](Standards/Documents_Format_Standard.md)。
