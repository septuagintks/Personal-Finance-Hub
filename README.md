# Personal Finance Hub (PFH)

Personal Finance Hub（PFH）是一个面向个人财务管理场景的聚合平台，目标是把账户、流水、转账、预算、报表、汇率和外部账单同步整合到一个高精度、可审计、可扩展的后端系统中。项目当前以架构设计和开发文档为主，详细开发规范见 [Docs/README.md](Docs/README.md)。

## 主要功能

- **多账户管理**：支持储蓄账户、信用账户、现金、投资账户、加密资产账户等账户类型，并允许按用户自定义子类型分组展示。
- **交易与转账记录**：支持收入、支出、调整和跨账户转账；转账场景覆盖跨币种金额推导、手续费、汇兑差异和转账聚合。
- **多币种与汇率管理**：以 USD 作为固定枢纽货币保存汇率快照，通过领域服务在内存中完成直接汇率、反向汇率和三角折算。
- **报表与净值分析**：按用户基准货币聚合账户余额、收支趋势、分类统计和 Dashboard 指标；报表读路径采用轻量级 CQRS 思路优化查询效率。
- **分类、标签与用户偏好**：支持收入/支出分类树、交易标签、默认基准货币、语言、时区、主题和默认报表周期等偏好配置。
- **外部账单同步与对账**：预留银行、支付平台、CSV/JSON 等 Provider 接入能力，通过幂等指纹避免重复导入，并支持同步后余额对账。
- **安全与审计**：REST API 使用 JWT 鉴权；危险操作、同步结果、汇率刷新和关键业务事件会进入审计与事件处理流程。

## 实现方式和架构设计概述

PFH 后端计划基于 **C++23 + Drogon + PostgreSQL 16+ + CMake** 构建，并采用 **Clean Architecture + 轻量级 DDD** 组织代码。系统以领域模型表达核心财务规则，让外部框架、数据库和第三方服务通过接口适配进入系统边界。

整体分层如下：

```text
Presentation  ->  Application  ->  Domain  <-  Infrastructure
REST API          Use Cases         Entities     PostgreSQL / Providers
JWT Filters       Query Services    Value Obj.   Repositories / UoW
DTO Mapping       Transactions      Services     Scheduler / Outbox
```

- **Domain 层**：承载 `Money`、`Decimal`、`Account`、`Transaction`、`TransferAggregate`、`ExchangeRate` 等核心模型，并集中处理金额精度、币种匹配、转账平衡、历史汇率选择等金融规则。
- **Application 层**：通过 `CreateTransactionUseCase`、`CreateTransferUseCase`、`RefreshExchangeRatesUseCase`、`RunSyncJobUseCase` 等用例编排仓储、事务和领域服务。
- **Infrastructure 层**：负责 PostgreSQL 持久化、Repository 实现、Unit of Work、Flyway 迁移、外部汇率 Provider、同步 Provider、后台调度任务和 Outbox 事件发布。
- **Presentation 层**：通过 Drogon 暴露 REST API，处理 JWT 鉴权、请求校验、DTO 映射、错误码转换和全局异常边界。

金额计算坚持不使用浮点数：金额使用 `NUMERIC(20,8)` 与强类型 `Money`，汇率使用更高精度快照并保留历史记录。写路径通过领域实体、Repository 和 Unit of Work 保证一致性；读路径在报表场景中可绕过领域实体，直接执行 SQL 聚合，再按用户基准货币完成折算。领域事件采用事务提交后的处理边界，并可结合 Outbox 方案保证副作用可重试、可审计。

## 文档入口

- [开发者文档中心](Docs/README.md)
- [技术架构总览](Docs/Architecture/01_Technical_Architecture.md)
- [数据库设计](Docs/Architecture/02_Database_Design.md)
- [领域模型设计](Docs/Architecture/03_Domain_Model_Design.md)
- [REST API 设计](Docs/Architecture/10_REST_API_Design.md)
- [测试策略](Docs/Architecture/16_Testing_Strategy.md)
