# Personal Finance Hub (PFH) - 待办任务跟踪 (Tasks)

## 📋 待办任务列表

### 1. 迭代与计划 (Planning)
- [x] 制定开发计划文档格式与规范 (更新到`Docs/Standards/Documents_Format_Standard.md`中) <!-- id: 1 -->
- [ ] 编写开发计划文档 (`Docs/Develop_Plan/Phase_1_Development_Plan.md`) <!-- id: 2 -->

### 2. 核心金融原语 (Core Financial Primitives)
- [ ] 实现 `Decimal` 定点数高精度计算类 <!-- id: 3 -->
- [ ] 实现 `Currency` 货币值对象 <!-- id: 4 -->
- [ ] 实现 `Money` 资金值对象 <!-- id: 5 -->
- [ ] 实现 `ExchangeRate` 汇率值对象 <!-- id: 6 -->

### 3. 领域模型与服务 (Domain Models & Services)
- [ ] 实现 `User` 与 `UserPreference` 领域实体 <!-- id: 7 -->
- [ ] 实现 `Account` 与 `Transaction` 领域实体 <!-- id: 8 -->
- [ ] 实现 `TransferAggregate` 领域聚合根 <!-- id: 9 -->
- [ ] 实现 `TransferDomainService` 转账领域服务 <!-- id: 10 -->
- [ ] 实现 `CurrencyConversionService` 汇率折算领域服务 <!-- id: 11 -->

### 4. 仓储与持久化 (Repository & Persistence)
- [ ] 配置 PostgreSQL 16+ 数据库连接与 Flyway 迁移脚本 <!-- id: 12 -->
- [ ] 实现 `DrogonUnitOfWork` 事务管理与事件暂存 <!-- id: 13 -->
- [ ] 实现 `UserRepository` 与 `UserPreferenceRepository` <!-- id: 14 -->
- [ ] 实现 `AccountRepository` 与 `TransactionRepository` <!-- id: 15 -->
- [ ] 实现 `ExchangeRateRepository` <!-- id: 16 -->

### 5. 应用层用例 (Application Use Cases)
- [ ] 实现 `CreateTransactionUseCase` 与 `DeleteTransactionUseCase` <!-- id: 17 -->
- [ ] 实现 `CreateTransferUseCase` <!-- id: 18 -->
- [ ] 实现 `RefreshExchangeRatesUseCase` <!-- id: 19 -->

### 6. 表现层与 API (Presentation & APIs)
- [ ] 实现 JWT 认证与 Refresh Token 过滤器 <!-- id: 20 -->
- [ ] 实现 `AccountController` 与 `TransactionController` <!-- id: 21 -->
- [ ] 实现 `ReportController` (轻量级 CQRS 报表查询) <!-- id: 22 -->

### 7. 基础设施与测试 (Infrastructure & Testing)
- [ ] 搭建 GoogleTest 单元测试框架 <!-- id: 23 -->
- [ ] 编写核心金融原语与领域服务的单元测试 <!-- id: 24 -->
- [ ] 编写 API 接口集成测试 <!-- id: 25 -->
