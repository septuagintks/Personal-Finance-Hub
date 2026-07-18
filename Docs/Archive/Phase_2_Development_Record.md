# Personal Finance Hub Phase 2 开发记录

Version: 1.0
Status: Complete

---

## 1. 阶段结论

Phase 2 已把 Phase 1 的财务后端扩展为可日常使用的响应式 Web 产品，完成浏览器会话、账户与记账工作流、分析导出、用户维护、受控运维面和同源部署，并通过 Windows、Linux、PostgreSQL、Drogon、Docker 与三浏览器交付门禁。

阶段交付分支为 `feature/phase2-product-experience`。目标环境最终复核无 blocker，完整代码与结果文档经签署后 fast-forward 合并到 `main`。

---

## 2. 开发路线

| Step | 交付结果 | 归档入口 |
| ---- | -------- | -------- |
| P2-S01 | 阶段边界、技术栈、威胁模型和性能预算 | S01-S04 摘要 |
| P2-S02 | OpenAPI、Web 会话、分页、错误和幂等基础 | S01-S04 摘要 |
| P2-S03 | Vue 应用、同源 Web Edge 和前端质量体系 | S01-S04 摘要 |
| P2-S04 | 注册、登录、恢复、刷新和退出闭环 | S01-S04 摘要 |
| P2-S05 | 账户完整生命周期 | S05-S08 摘要 |
| P2-S06 | 分类、标签和用户偏好 | S05-S08 摘要 |
| P2-S07 | 普通流水查询、创建、更正和删除 | S05-S08 摘要 |
| P2-S08 | Transfer 聚合查询、创建、更正和删除 | S05-S08 摘要 |
| P2-S09 | Dashboard、分析报表和 CSV 导出 | S09-S12 摘要 |
| P2-S10 | 审计、余额维护、授权和运行可见性 | S09-S12 摘要 |
| P2-S11 | Accessibility、安全、性能和发布硬化 | S09-S12 摘要 |
| P2-S12 | 跨平台、恢复、回滚和最终签署 | S09-S12 摘要 |

---

## 3. 最终交付范围

### 3.1 产品与会话

- `Candy's Ledger` 公开入口、认证页面和应用 Shell。
- 内存 Access Token、HttpOnly Refresh Cookie 和同源 Origin 保护。
- single-flight refresh、多标签页互斥、会话撤销和用户缓存隔离。
- Desktop、Tablet、Mobile、`zh-CN` / `en-US` 和用户 IANA 时区。

### 3.2 财务工作流

- 账户、分类、标签和偏好的查询与维护。
- 普通流水的组合筛选、游标分页、创建、详情、原子更正和软删除。
- Transfer 三种输入模式、三种手续费来源、聚合详情、原子更正和聚合软删除。
- Dashboard、净资产趋势、root 分类/账户/标签维度和服务端 CSV 导出。

### 3.3 维护与运行

- 当前用户审计和单账户/全账户余额缓存重建。
- 持久化 `USER` / `OPERATOR`、脱敏 readiness、Metrics 和 dead-letter 受控重试。
- V7-V10 migration、金融写请求幂等、追加式更正关系和运行角色权限。
- non-root Web/Backend 镜像、只读文件系统、最小 capability 和优雅停止。

---

## 4. 验收结果

| 门禁 | 最终结果 |
| ---- | -------- |
| Windows Debug / Release PostgreSQL OFF | 382/382 PASS |
| Linux Debug / Release PostgreSQL ON | 384/384 PASS |
| Linux PostgreSQL OFF | 382/382 PASS |
| PostgreSQL / Flyway | V1-V10、双角色、11 张 FORCE RLS 表和真实 fixture PASS |
| Frontend unit | Vitest/MSW 63/63 PASS |
| Windows Edge | 37/37 PASS |
| Chromium / Firefox / WebKit | 111/111 PASS |
| Accessibility | 键盘、焦点、对比度、zoom、reduced motion 和 axe PASS |
| Daily / Stress 性能 | 10,000 / 100,000 行预算 PASS |
| Compose runtime | Auth、财务、报表、维护、Provider、Outbox/Scheduler PASS |
| 恢复与回滚 | PostgreSQL restore、migration forward fix、镜像 rollback PASS |
| 安全与文档 | 依赖、许可证、source map、秘密、日志和链接门禁 PASS |

---

## 5. 现行边界

### 5.1 汇率覆盖

实时路径覆盖 20 种法币与 BTC。其余 12 种加密货币没有实时保证；只有完整历史快照存在时才允许降级，否则明确返回不可用。完整加密货币定价不在当前开发计划内。

### 5.2 外部生态

- 本阶段只提供数据导出，不提供外部账单导入。
- 银行、开放银行、信用卡和支付平台接入保留到 Phase 3。
- 系统不执行真实支付或资金划转。

### 5.3 产品与部署

- `Candy's Ledger` 仍是暂定产品名；PFH 保持仓库、后端和 API 名称。
- 当前角色模型固定为 `USER` / `OPERATOR`，不提供通用 IAM 管理台。
- 生产目标为 Linux 同源部署，并要求真实 Drogon、PostgreSQL、OpenSSL、Argon2、libcurl 和 IANA tzdb。

---

## 6. 文档入口

- [S01-S04 交付摘要](Phase_2_S01-S04_Delivery_Summary.md)
- [S05-S08 交付摘要](Phase_2_S05-S08_Delivery_Summary.md)
- [S09-S12 交付摘要](Phase_2_S09-S12_Delivery_Summary.md)
- [Phase 2 开发计划](../Development_Plans/Phase_2/Phase_2_Development_Plan.md)
- [REST API 设计](../Architecture/10_REST_API_Design.md)
- [前端设计](../Architecture/13_Frontend_Design.md)
- [测试策略](../Architecture/16_Testing_Strategy.md)
