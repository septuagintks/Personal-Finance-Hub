# Phase 2 S09-S12 分析、维护与最终签署摘要

Version: 1.0
Status: Complete

---

## 1. 交付范围

P2-S09 至 P2-S12 完成分析导出、用户维护、运行可见性、产品硬化和目标环境签署。

| Step | 交付内容 |
| ---- | -------- |
| S09 | Dashboard、净资产趋势、维度分析和 CSV 导出 |
| S10 | 用户审计、余额维护、角色与运行可见性 |
| S11 | Accessibility、安全、性能和 Release Candidate |
| S12 | Linux/PostgreSQL/Docker、恢复、回滚和最终签署 |

---

## 2. 分析与导出

- Dashboard 展示净资产、当月收入/支出、账户分布和 Top root 支出分类。
- 分析接口提供月度净资产趋势和 root 分类、账户、标签维度，明确基准币种、估值时刻和 current/historical 状态。
- 报表窗口由后端按用户 IANA 时区计算；Transfer 排除、signed Adjustment、DST 和历史汇率保持 Phase 1 语义。
- 缺失完整汇率路径时整份结果明确失败，不返回部分总计或前端旧缓存。
- CSV 与流水筛选一致，使用 RFC 4180、UTF-8、公式注入防护、业务 magnitude、366 天和 10,000 行上限。

---

## 3. 用户维护与运维面

### 3.1 用户维护

- 当前用户审计按唯一 ID 降序游标分页，只返回批准字段和 TraceId。
- 单账户或全部账户余额缓存重建保持 ledger 权威余额，并以 `MAX(transaction.version)` 对齐 source version。
- 账户、分类、标签创建支持持久化幂等重放；后台任务按数据库时钟和有界批次清理过期记录。

### 3.2 授权与可见性

- V10 持久化 `USER` / `OPERATOR`；公共注册只能创建 `USER`，受保护请求以数据库当前角色为准。
- `/livez` 与应用队列解耦；`/readyz` 检查 request DB、migration 和 Scheduler 并保持脱敏。
- Operator API 提供运行摘要、认证 Metrics、脱敏 dead-letter 列表和幂等并发安全重试。
- role-init 在任何修改前拒绝重名、管理员复用、管理型角色以及 request/background 任一方向的角色成员关系。

---

## 4. 发布硬化

- ECharts 保持 Reports 路由按需加载；initial、total、async chunk 和 CSS 均受 gzip 预算约束。
- 直接依赖版本和许可证固定，production source map、秘密模式和已知高危漏洞进入门禁。
- Web Edge 设置 CSP、frame denial、MIME、Referrer、Permissions、COOP 和缓存策略。
- Web/Backend 镜像为 non-root、read-only、`cap_drop=ALL`、no-new-privileges；Backend 不发布宿主端口。
- 受信外部 TLS 终止保留精确 HTTPS scheme，其他 forwarded scheme 不会被提升为 HTTPS。

---

## 5. 最终验证矩阵

| 环境与门禁 | 结果 |
| ---------- | ---- |
| Windows GCC 16 Debug / PostgreSQL OFF | 382/382 PASS |
| Windows GCC 16 Release / PostgreSQL OFF | 382/382 PASS |
| Linux GCC 13 Debug / PostgreSQL ON | 384/384 PASS |
| Linux GCC 13 Release / PostgreSQL ON | 384/384 PASS |
| Linux PostgreSQL OFF | 382/382 PASS |
| Flyway / PostgreSQL | V1-V10 migrate/info/validate/no-op PASS |
| Repository / RLS | 双角色、11 张 FORCE RLS 表、连接复用、锁与 NUMERIC PASS |
| Frontend unit | Vitest/MSW 63/63 PASS |
| Windows Edge | 37/37 PASS |
| Chromium / Firefox / WebKit | 111/111 PASS |
| Accessibility | 键盘、焦点、对比度 7.38:1、200% zoom、reduced motion、axe PASS |
| Runtime | 同源 Auth/API、Provider、Outbox/Scheduler、restart、SIGTERM PASS |
| Recovery | PostgreSQL restore、migration forward fix、immutable image rollback PASS |

### 5.1 性能结果

| Profile | Fixture | First page p95 | Filter p95 | Dashboard p95 | CSV first-byte p95 |
| ------- | ------- | -------------- | ---------- | ------------- | ------------------ |
| Daily | 10,000 / 20 / 200 | 2.02 ms | 1.99 ms | 7.57 ms | 65.05 ms |
| Stress | 100,000 / 50 / 500 | 3.21 ms | 3.10 ms | 20.01 ms | 95.79 ms |

页面 filter interactive、LCP、INP 和 CLS 在 Daily/Stress 数据集均满足预算，axe 为 0；关键 SQL 使用 shared-buffer hit 且无物理读取。

### 5.2 恢复结果

- PostgreSQL custom dump 在独立 PostgreSQL 16 实例恢复后，migration 与核心业务计数一致。
- 恢复实例的 health、readiness、login、核心查询和 SIGTERM 通过。
- migration 失败采用新版本 forward fix，不改写 V1-V10 checksum。
- 故障 successor 退出后，Compose 可从已验证不可变镜像恢复，health、数据和登录均通过。

---

## 6. 安全与能力边界

- Token、Cookie、数据库凭据、Provider key、完整 URL/响应、dump、原始日志和 benchmark 产物均未进入仓库。
- 完整加密货币定价未交付；报表只接受现有实时值、完整历史降级或明确不可用。
- 外部账单导入、银行和支付平台接入保留到 Phase 3。
- 当前同源 Web 产品不承诺原生移动端、离线写入、多组织权限或多节点调度。

完整路线见 [Phase 2 开发记录](Phase_2_Development_Record.md) 与 [Phase 2 开发计划](../Development_Plans/Phase_2/Phase_2_Development_Plan.md)。
