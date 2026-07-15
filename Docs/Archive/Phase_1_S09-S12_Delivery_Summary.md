# Phase 1 S09-S12 应用、API 与最终签署摘要

Version: 1.0
Status: Complete

---

## 1. 交付范围

P1-S09 至 P1-S12 把领域与持久化能力装配为可运行后端，并完成真实 Linux、PostgreSQL、Drogon、Provider 和 Docker 验收。

| Step | 交付内容 |
| ---- | -------- |
| S09 | Application Use Case、报表与事件契约 |
| S10 | REST API、认证、资源管理与 production composition root |
| S11 | Outbox Publisher、Scheduler、外部汇率与认证清理 |
| S12 | 跨平台、真实数据库、Docker 和最终一致性签署 |

---

## 2. Application 与报表

### 2.1 写路径

Application 统一负责用户归属校验、事务边界、Repository 编排、Domain 调用、Outbox 事件和错误映射。主要写路径包括：

- 注册、登录、Refresh Token rotation/reuse detection 与登出。
- 账户创建、归档和危险删除。
- 分类、标签与用户偏好维护。
- 普通流水创建与软删除。
- 转账聚合创建和手续费处理。
- 汇率刷新与认证数据清理。

命令时间可选，缺省由注入的 Clock 补当前时间，避免生成 epoch 数据。

### 2.2 查询与报表

`ReportQueryService` 提供 Balance、Net Worth、Cash Flow 和 Dashboard Summary：

- Transfer 双腿不计入收入与支出。
- Adjustment 按 signed 语义分配流入或流出。
- Dashboard 月份按 `UserPreference.timezone` 计算半开窗口。
- Top expense 按一级 root 分类聚合。
- 多币种使用直接、反向或 USD 枢纽汇率；缺失时返回不可用。

---

## 3. REST API 与认证

### 3.1 API 范围

Phase 1 暴露 `/api/v1` 下的认证、账户、分类、标签、偏好、币种、流水、转账和报表接口。OpenAPI 3.1 契约与 Drogon 路由由静态门禁保持一致。

边界规则包括：

- JSON 对象拒绝未知字段和错误类型。
- 金额只接受和返回十进制字符串。
- 时间使用带时区的 RFC 3339 并规范化为 UTC。
- ID、枚举、字符串长度和数据库精度在进入 Use Case 前校验。
- 错误响应包含稳定 `error_code`、`message` 与 TraceId。
- 生产响应不暴露 SQL、路径、Token、密钥或底层异常。

### 3.2 认证安全

- Password 使用 Argon2id；可选 pepper 不落库。
- Access Token 使用 HS256 JWT，校验 issuer、audience、subject、session、jti 和过期时间。
- Refresh Token 只存 hash，成功刷新执行 rotation。
- 复用旧 Refresh Token 会撤销整个 session。
- Logout 同事务撤销 Refresh Session 和当前 Access Token。
- 未知用户与错误密码使用稳定 401 响应，不泄露账户状态。

---

## 4. Outbox 与后台运行时

### 4.1 Outbox Publisher

Outbox 支持：

- `FOR UPDATE SKIP LOCKED` 批量 claim。
- claim token 防止旧 worker 提交新租约。
- 1m、5m、15m、1h、6h 有界退避。
- lease 恢复、dead letter 和失败摘要。
- Handler receipt 与 supplemental Audit 同事务幂等。

数据库 `NOW()` 是 due、lease 和退避的事实时钟。

### 4.2 Scheduler

Drogon timer callback 只执行本机防重入并把任务提交到有界 worker pool。HTTP、PostgreSQL 和 Handler 工作不阻塞 Event Loop。

汇率刷新与 Session cleanup 使用 PostgreSQL scheduled lease；JobManager 负责启动、停止和优雅 drain。

### 4.3 汇率 Provider

- 主源：FreeCurrencyAPI。
- 整批备用：exchangerate.fun。
- 出站 HTTPS：libcurl，启用 peer/host 验证、HTTPS-only、无 redirect、hard timeout 和 1 MiB 响应上限。
- 响应通过 SAX 保留原始 numeric token，不经二进制浮点。
- 主源 transport、HTTP 或响应校验失败后，原请求批次整体切换备用源。
- 成功快照记录实际 source；双源失败事件使用组合 identity。

当前实时能力覆盖 20 种法币与 BTC。其余 12 种加密货币无实时保证，缺失时只允许完整历史快照降级或明确不可用。

---

## 5. 最终验证矩阵

| 环境与门禁 | 结果 |
| ---------- | ---- |
| Windows GCC 16 Debug / PostgreSQL OFF | 349/349 PASS |
| Windows GCC 16 Release / PostgreSQL OFF | 349/349 PASS |
| Linux GCC 13 Debug / PostgreSQL ON | 351/351 PASS |
| Linux GCC 13 Release / PostgreSQL ON | 351/351 PASS |
| Linux PostgreSQL OFF | 349/349 PASS |
| Provider 定向 Unit | 13/13 PASS |
| PostgreSQL fixture | 12/12 scenarios PASS |
| Flyway | V1-V6 migrate/info/validate/no-op PASS |
| Drogon runtime | 认证、RLS、财务、报表、响应头和 SIGTERM PASS |
| Docker | 冷构建、healthy、non-root、双角色、Outbox/Scheduler PASS |

### 5.1 真实 Provider 场景

| 场景 | 结果 |
| ---- | ---- |
| CNY + EUR | 2 条 `FreeCurrencyAPI` 快照，事件发布，lease 释放 |
| CNY + TWD | 整批由 `exchangerate.fun` 写入，主源部分写入为 0 |
| EUR + ETH，历史完整 | 无新快照，`historicalAvailable=true` |
| EUR + ETH，历史不完整 | 无新快照，`historicalAvailable=false` |

### 5.2 Docker 结果

最终 ARM64 镜像基于 Ubuntu 24.04，包含 tzdata、libcurl/OpenSSL 和生产共享库，以 `pfh`/UID 1001 运行。运行时保持 request/background 角色边界、8/8 FORCE RLS、唯一 JSON Content-Type、ETag、TraceId、11/11 Outbox published、lease 释放和 SIGTERM exit 0。

---

## 6. 最终结论

- Phase 1 的代码、迁移、API、后台任务和文档与设计目标一致。
- Windows、Linux、PostgreSQL、真实 HTTPS 与 Docker 阻断门禁全部通过。
- 测试 API key 已轮换，仓库不含 key、Authorization、完整 query URL、响应正文、数据库凭据或原始日志。
- 完整加密货币定价源不在当前计划内；现有覆盖限制已作为能力边界记录。
- Phase 1 交付通过完整门禁并合并到 `main`。

完整路线见 [Phase 1 开发记录](Phase_1_Development_Record.md)，现行开发边界见 [Phase 1 开发计划](../Development_Plans/Phase_1/Phase_1_Development_Plan.md)。
