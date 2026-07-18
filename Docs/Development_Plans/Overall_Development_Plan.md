# Personal Finance Hub 总体开发计划

Version: 2.0
Architecture: Clean Architecture + Lightweight DDD
Status: Active

---

## 1. 总体目标

PFH 以个人财务聚合为核心，按“后端正确性、产品可用性、外部生态”三个阶段推进。任何后续功能都必须保持 Phase 1 已确认的金融精度、事务一致性、多租户隔离和错误安全边界。

---

## 2. 阶段总览

| Phase | 定位 | 主要交付 | 状态 |
| ----- | ---- | -------- | ---- |
| Phase 1 | 基础闭环与核心正确性 | C++23 后端、PostgreSQL、认证、记账、转账、报表、Outbox、Scheduler、Docker | Complete |
| Phase 2 | 产品体验与稳定性 | Vue 3 前端、日常记账体验、分析增强、维护与可观测性 | In Progress；S11 Windows RC Complete |
| Phase 3 | 外部生态预留 | 账单导入、银行/支付平台接入、同步框架与自动化 | Reserved |

### 2.1 Phase 1

Phase 1 已完成：

- `Decimal`、`Money`、`Currency`、`ExchangeRate` 和 Transfer 聚合。
- PostgreSQL 16+、Flyway V1-V6、Repository、Unit of Work、FORCE RLS 与双角色。
- 认证、账户、分类、标签、偏好、流水、转账和报表 API。
- Transactional Outbox、Scheduler、认证数据清理与汇率刷新。
- FreeCurrencyAPI 主源、exchangerate.fun 整批备用和历史降级。
- Windows、Linux、真实 PostgreSQL、Drogon 和 Docker 全量门禁。

详情见 [Phase 1 开发计划](Phase_1/Phase_1_Development_Plan.md) 与 [Phase 1 开发记录](../Archive/Phase_1_Development_Record.md)。

### 2.2 Phase 2

Phase 2 把稳定后端推进为可日常使用的产品：

- Vue 3 前端骨架、认证状态和统一 API Client。
- 账户、流水、转账、分类、标签和偏好管理。
- Dashboard、净资产、现金流和分类分析。
- 归档、危险删除、余额重建与维护入口。
- 审计查询、后台任务可见性、错误追踪和发布门禁。

完整加密货币实时定价不在当前计划内。前端与报表必须正确展示后端返回的实时值、历史降级或不可用状态，不得自行补默认汇率。

详情见 [Phase 2 开发计划](Phase_2/Phase_2_Development_Plan.md)。

### 2.3 Phase 3

Phase 3 是未排期的外部生态预留：

- CSV、Excel、PDF、邮件、银行与信用卡账单导入。
- 支付平台和开放银行只读同步。
- Provider 插件、幂等指纹、去重、预览、冲突处理和重试。
- 商户标准化、规则分类、订阅识别和提醒。

这些能力只有在 Phase 2 的用户确认、审计、维护和可观测性成熟后才进入实现。详情见 [Phase 3 开发计划](Phase_3/Phase_3_Development_Plan.md)。

---

## 3. 阶段治理

### 3.1 分支规则

- 每个 Phase 使用独立长期开发分支。
- Phase 内完成代码、测试、文档和交付摘要后才允许合并 `main`。
- 合并必须是可审计的 fast-forward 或显式合并，不重写已交付历史。
- 下一 Phase 从最新 `main` 创建，不从旧 feature 分支继续叠加。

### 3.2 完成定义

每个 Phase 的完成条件包括：

1. 计划范围内的核心用户路径可运行。
2. 自动化测试和目标部署环境门禁通过。
3. 架构文档、API 契约、指南和代码一致。
4. 未纳入范围的能力以边界声明表达，不伪装为已实现。
5. 交付摘要只记录最终能力、结果和持续约束。

### 3.3 跨平台验证

Windows 负责快速开发回归；Linux production ON、PostgreSQL 和 Docker 是后端阶段交付的目标环境门禁。跨设备协作通过独立 `.codex` 仓库传递固定 commit、测试矩阵和所有权。

---

## 4. 当前入口

- 当前稳定后端结果：Phase 1。
- 当前开发范围：Phase 2 S12 跨平台目标环境验收与签署。
- 外部账单与支付平台：Phase 3 Reserved。
- 现行技术设计：`Docs/Architecture/`。
- 构建、配置和测试：`Docs/Guides/` 与 `config/README.md`。
