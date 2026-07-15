# Personal Finance Hub Phase 2 开发计划

Version: 2.0
Backend: C++23
Frontend: Vue 3
Status: Draft

---

## 1. 阶段目标

Phase 2 在 Phase 1 稳定后端之上交付可日常使用的个人财务产品，重点是前端体验、报表分析、数据维护、可观测性和发布质量。

### 1.1 范围

- Vue 3 + Vite 前端、路由、状态管理与 API Client。
- 认证、账户、流水、转账、分类、标签和偏好页面。
- Dashboard、净资产、现金流和分类分析。
- 账户归档、危险删除、余额重建与维护入口。
- 审计查询、后台任务状态、错误追踪与运行可见性。
- Frontend/Backend E2E、覆盖率、性能和发布门禁。

### 1.2 不在范围

- 银行、信用卡和支付平台同步。
- OCR、PDF 和邮件账单解析。
- 真实支付写操作。
- 完整加密货币实时定价。

后端汇率能力边界保持不变：20 种法币与 BTC 有实时路径，其余加密货币可能返回历史降级或不可用。前端必须忠实展示该状态。

---

## 2. 架构边界

```text
Vue 3 -> REST API -> Application -> Domain
                         ^            ^
                    Presentation  Infrastructure
```

- 前端只负责交互、展示和输入约束，不复制金融计算。
- 后端继续是金额、汇率、权限和事务的事实来源。
- API 变化先更新 OpenAPI 和 DTO，再实现前后端。
- 维护动作必须经过 Application、权限与审计。
- 现有 Phase 1 金融和多租户规则不得为 UI 便利而削弱。

---

## 3. 推荐顺序

### 3.1 P2-S01 阶段基线

- 从最新 `main` 创建独立 Phase 2 分支。
- 复核 OpenAPI、错误模型、认证流程和页面所需查询。
- 建立前后端目录、Node 版本和质量命令。

验收：前端构建与基础测试可在干净环境运行，API 差距形成明确清单。

### 3.2 P2-S02 应用骨架与认证

- 建立 Vue 3、Vite、Router、Store 和 HTTP Client。
- 实现登录、Token refresh、logout 和统一 401 处理。
- 建立响应错误、TraceId 和加载状态呈现。

验收：认证生命周期可通过真实后端 E2E 运行，敏感 Token 不出现在日志。

### 3.3 P2-S03 核心记账体验

- 账户列表、详情、创建、归档和危险删除确认。
- 流水列表、筛选、创建、分类与标签。
- 转账三种输入模式与手续费来源。
- 偏好、locale、timezone 与基准货币。

验收：用户可完成日常收入、支出、Adjustment 和 Transfer；金额始终以字符串提交。

### 3.4 P2-S04 报表与分析

- Dashboard Summary。
- 净资产趋势、现金流和 root 分类统计。
- 时间范围、基准货币和汇率不可用状态。
- CSV 导出与常见数据量性能基准。

验收：Transfer 不进入收支；月份遵循用户时区；缺失汇率不参与错误计算。

### 3.5 P2-S05 维护与可观测性

- 余额缓存重建和维护任务入口。
- 审计日志查询。
- Outbox、Scheduler、失败、dead letter 与任务运行状态。
- 管理操作权限、确认和审计。

验收：维护任务可追踪、可重试；高风险操作有确认和审计；生产响应继续脱敏。

### 3.6 P2-S06 发布门禁

- 前端 unit/component 测试。
- 后端现有 349/351 基线回归。
- 认证、记账、转账、报表和维护 E2E。
- Accessibility、性能、文档链接和依赖安全检查。
- Linux/Docker 生产构建与回滚清单。

---

## 4. 完成定义

Phase 2 完成时必须满足：

1. 普通用户可通过前端完成核心财务工作流。
2. 前端不承载后端金融规则的独立实现。
3. 所有高风险维护动作具备权限、确认和审计。
4. 后台任务和错误可被定位，不泄露敏感数据。
5. 前后端 E2E、Linux/Docker 和发布门禁通过。
6. 交付摘要、OpenAPI、指南和当前代码一致。

---

## 5. 依赖入口

- [总体开发计划](../Overall_Development_Plan.md)
- [Phase 1 开发计划](../Phase_1/Phase_1_Development_Plan.md)
- [REST API 契约](../../Architecture/10_REST_API_OpenAPI.json)
- [前端设计](../../Architecture/13_Frontend_Design.md)
- [测试策略](../../Architecture/16_Testing_Strategy.md)
