# Personal Finance Hub Phase 1 开发记录

Version: 1.0
Status: Complete

---

## 1. 阶段结论

Phase 1 已完成 PFH 后端最小可运行闭环：工程基础、金融原语、领域模型、PostgreSQL 持久化、认证与核心 REST API、Transactional Outbox、后台调度、真实汇率 Provider，以及 Windows、Linux、PostgreSQL 和 Docker 门禁。

阶段交付分支为 `feature/phase1-foundation`。最终代码与文档通过完整门禁后合并到 `main`。

---

## 2. 开发路线

| Step | 交付结果 | 归档入口 |
| ---- | -------- | -------- |
| P1-S01 | 目录结构与 Clean Architecture 物理边界 | S01-S04 摘要 |
| P1-S02 | CMake、C++23、依赖和编译门禁 | S01-S04 摘要 |
| P1-S03 | GoogleTest、CTest 与质量脚本 | S01-S04 摘要 |
| P1-S04 | 强类型 ID、错误模型、配置 overlay 与日志 | S01-S04 摘要 |
| P1-S05 | `Decimal`、`Money`、`Currency`、`ExchangeRate` | S05-S08 摘要 |
| P1-S06 | Account、Transaction、Transfer 与领域服务 | S05-S08 摘要 |
| P1-S07 | PostgreSQL 16+ schema 与 Flyway V1-V6 | S05-S08 摘要 |
| P1-S08 | Repository、Unit of Work、RLS 与 Outbox 持久化 | S05-S08 摘要 |
| P1-S09 | 核心 Application Use Case 与报表查询 | S09-S12 摘要 |
| P1-S10 | REST API、认证、资源管理与 production composition root | S09-S12 摘要 |
| P1-S11 | Outbox Publisher、Scheduler、汇率刷新和认证清理 | S09-S12 摘要 |
| P1-S12 | 跨平台、真实数据库、Docker 与最终一致性签署 | S09-S12 摘要 |

---

## 3. 最终交付范围

### 3.1 核心业务

- 用户注册、登录、Refresh Token rotation/reuse detection 与登出。
- 账户、分类、标签、偏好、流水和转账的创建或查询闭环。
- 三种转账输入模式和 Source、Target、ThirdParty 手续费来源。
- Balance、Net Worth、Cash Flow 与 Dashboard Summary。
- 用户时区月份窗口、一级分类聚合和多币种折算。

### 3.2 数据与一致性

- PostgreSQL 16+、Flyway V1-V6、FORCE RLS 和双数据库角色。
- 业务写入与 Outbox 同事务提交。
- Account optimistic lock、余额缓存重建和 `MAX(version)` source version。
- 汇率 append-only、历史时间点查询和 USD 枢纽折算。
- Outbox claim token、租约恢复、有限退避、dead letter 和幂等 Handler receipt。

### 3.3 运行时

- Drogon 入站 HTTP 服务和 libcurl 出站 HTTPS。
- FreeCurrencyAPI 主源与 exchangerate.fun 整批备用。
- Bounded worker pool、定时任务 lease、会话清理和优雅停止。
- Ubuntu 24.04 多阶段 Docker 镜像、non-root 运行与 healthcheck。

---

## 4. 验收结果

| 门禁 | 最终结果 |
| ---- | -------- |
| Windows Debug / PostgreSQL OFF | 349/349 PASS |
| Windows Release / PostgreSQL OFF | 349/349 PASS |
| Linux Debug / PostgreSQL ON | 351/351 PASS |
| Linux Release / PostgreSQL ON | 351/351 PASS |
| Linux PostgreSQL OFF | 349/349 PASS |
| PostgreSQL fixture | 12/12 scenarios PASS |
| Flyway | V1-V6 migrate/info/validate/no-op PASS |
| Drogon runtime | Auth、RLS、财务、报表、响应头和 SIGTERM PASS |
| Provider runtime | 主源、整批备用、双源失败与历史降级 PASS |
| Docker | 冷构建、healthy、non-root、双角色、Outbox/Scheduler PASS |
| 文档与安全 | Markdown 链接、格式、秘密扫描和一致性 review PASS |

---

## 5. 现行边界

### 5.1 汇率覆盖

PFH Domain 白名单包含 20 种法币和 13 种加密货币。当前 Provider 组合可为 20 种法币和 BTC 提供实时路径；ETH、USDT、USDC、BNB、XRP、ADA、DOGE、SOL、TRX、MATIC、DOT 与 WBTC 没有实时汇率保证。

请求含未覆盖币种时，系统不会拆分成功批次或混用来源；只有全部目标存在历史快照时才声明历史降级可用，否则明确返回不可用。完整加密货币定价不在当前开发计划内。

### 5.2 API 与数据维护

- Phase 1 不开放转账聚合删除路由。
- 普通流水采用追加与软删除模型，不提供普通更新。
- 危险账户删除必须显式确认，并完整清理相关聚合。
- 完整前端、外部账单导入和支付平台接入不属于 Phase 1。

### 5.3 平台要求

- 生产部署目标为 Linux，并要求可用的 IANA tzdb 与 `tzdata`。
- 生产构建必须启用真实 Drogon、PostgreSQL、OpenSSL、Argon2 和 libcurl 依赖。
- Windows PostgreSQL OFF 结果用于快速回归，不替代 Linux production ON 与真实 PostgreSQL 门禁。

---

## 6. 文档入口

- [S01-S04 交付摘要](Phase_1_S01-S04_Delivery_Summary.md)
- [S05-S08 交付摘要](Phase_1_S05-S08_Delivery_Summary.md)
- [S09-S12 交付摘要](Phase_1_S09-S12_Delivery_Summary.md)
- [Phase 1 开发计划](../Development_Plans/Phase_1/Phase_1_Development_Plan.md)
- [技术架构](../Architecture/01_Technical_Architecture.md)
- [测试策略](../Architecture/16_Testing_Strategy.md)
