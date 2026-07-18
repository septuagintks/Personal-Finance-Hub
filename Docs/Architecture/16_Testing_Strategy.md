# Personal Finance Hub 测试策略

Version: 2.0
Backend: C++23
Architecture: Clean Architecture + GoogleTest
Status: Approved

---

## 1. 目标

PFH 测试优先证明金融正确性、事务一致性、租户隔离、API 契约和后台任务可恢复性。测试层次应同时提供快速反馈与真实运行环境证据，任何 In-Memory 或静态门禁都不能替代 PostgreSQL、Drogon 和 Linux production ON 验收。

Phase 1 的固定测试数量、环境和最终结果归档在 [S09-S12 交付摘要](../Archive/Phase_1_S09-S12_Delivery_Summary.md)，本文件只维护持续有效的测试规则。

---

## 2. 测试层次

| 层次 | 目的 | 外部依赖 |
| ---- | ---- | -------- |
| Unit | 验证值对象、实体、纯领域服务、解析器与调度状态机 | 无数据库、无网络、无 Drogon runtime |
| Use Case | 验证权限、事务编排、错误传播和事件登记 | 使用受控测试替身 |
| In-Memory Integration | 快速固定 Repository 与 Unit of Work 的业务语义 | In-Memory adapter |
| PostgreSQL Integration | 验证 SQL、事务、RLS、精度、锁与真实 Repository | PostgreSQL 16+ |
| API | 验证 DTO、路由、认证、状态码、响应头和 OpenAPI | framework-neutral 与真实 Drogon 两类 |
| Runtime | 验证 Provider、Scheduler、Outbox、迁移、Docker 和进程生命周期 | Linux production ON |

同一业务场景应尽量在 In-Memory 与 PostgreSQL adapter 间复用。前者用于快速定位，后者才是持久化语义的最终证据。

---

## 3. 金融与领域测试

必须覆盖：

1. `Decimal` 解析、Half-Even 舍入、溢出与 `NUMERIC(20,8/10)` 边界。
2. `Money` 仅允许同币种加减，跨币种计算必须显式使用汇率。
3. `ExchangeRate` 的直接、反向、USD 枢纽与历史时间点选择。
4. Transfer 的三种输入模式、双腿平衡和 Source、Target、ThirdParty 手续费。
5. Transfer 不计入收入或支出；signed Adjustment 正数计流入、负数计流出。
6. Income 与 Expense 分类 board 约束、root 分类回溯和软删除分类显示。
7. 账户余额允许负数，乐观锁与余额缓存重建保持一致。
8. Dashboard 按用户 IANA 时区计算自然月半开窗口。

Domain 测试不得访问 Repository、数据库、网络或系统时钟；时间与随机性通过接口注入。

---

## 4. PostgreSQL 与租户隔离

PostgreSQL Integration Test 必须从空库执行当前全部 Flyway migration，并验证 `migrate`、`info`、`validate` 和第二次 no-op。测试库不得含个人数据。

Repository fixture 至少覆盖：

- User、Preference、Account、Transaction、ExchangeRate、Category、Tag 和 AuditLog。
- Unit of Work、业务写入与 Outbox 同事务提交、失败回滚。
- `exchange_rates` append-only 与 `fetched_at <= target_time` 的历史查询。
- Account optimistic lock 和余额缓存 `source_version = MAX(version)` 语义。
- Dangerous Delete 的锁定、同步 AuditLog、完整关联清理与 Outbox。
- Outbox claim token、`SKIP LOCKED`、租约恢复、退避、dead letter 和 Handler receipt。
- V10 USER/OPERATOR、Operator retry command、TraceId、受限幂等清理函数与角色授权。
- `NUMERIC` round-trip、边界舍入和超界拒绝。

租户隔离测试必须使用 request/background 两个独立非 superuser 角色，并验证：

1. request role 无 `BYPASSRLS`。
2. background role 默认只读，只获得明确授权的跨租户查询。
3. 未设置用户上下文时 RLS fail closed。
4. 连接池复用不残留前一用户上下文。
5. 访问其他用户私有资源返回 404，避免资源枚举。

---

## 5. API 与安全

API 测试同时覆盖 framework-neutral 应用边界与真实 Drogon runtime。OpenAPI、路由表和 DTO 必须由静态门禁保持一致。

核心路径包括：

- 注册、登录、Refresh Token rotation/reuse detection 和登出。
- Web Cookie 注册、登录、刷新、同源保护、Cookie 属性、重用检测和登出清理。
- 账户、分类、标签、偏好、流水和转账。
- Balance、Net Worth、Cash Flow 与 Dashboard Summary。
- 币种目录、ETag、TraceId 和统一错误响应。
- 幂等键同请求重放、不同指纹冲突、租户隔离、过期回收和业务/Outbox 原子性。
- 当前用户 Audit、余额缓存重建、liveness/readiness、Operator summary/Metrics、脱敏 dead letter 与并发安全重试。

持续规则：

- 金额和汇率只以十进制字符串进入或离开 API，JSON number 必须被拒绝。
- RFC 3339 时间必须带时区并规范化为 UTC。
- 未知字段、错误类型、越界 ID、非法枚举和超长字符串必须返回稳定错误。
- 401、403、404、409、422、500 与 502 的映射保持稳定。
- 生产响应和日志不得暴露 SQL、文件路径、Token、密码、Provider key 或底层异常正文。
- 结构化错误必须包含 `retryable` 与受控 `field_errors`；强 `If-Match` 只接受单一正版本 ETag。
- Refresh Token 只存 hash；旧 Token 复用撤销整个 session；Logout 后当前 Access Token 立即失效。
- 公共注册不能授予 Operator；登录/refresh 使用当前持久化角色，JWT role 伪造或角色变更后的旧 Token 必须被拒绝。

---

## 6. Provider、Outbox 与 Scheduler

汇率测试必须覆盖 FreeCurrencyAPI 主源、exchangerate.fun 整批备用、双源失败和历史降级：

- 主源成功时不调用备用源。
- 主源 transport、HTTP 或响应校验失败时，原批次整体切换备用源。
- 成功批次不拆分、不混用 source。
- 数字 token 不经 `float` 或 `double`。
- 未覆盖币种只有在完整历史快照存在时才声明降级可用。
- 测试凭据从仓库外注入，结果只记录脱敏断言。

后台运行时测试必须覆盖：

- Event Loop callback 只做本机防重入与有界入队。
- worker 队列满、停止中、异常和软超时不会卡死后续调度。
- scheduled lease 使用数据库时钟和 token-guarded release。
- Outbox 多 worker claim、失败重试、dead letter 与幂等副作用。
- 认证清理只删除已过期记录。
- 幂等清理使用数据库时钟、有界批次和并发跳锁，只删除已过期记录且不扩大 request role 的租户访问能力。
- SIGTERM 依次停止 timer、drain 已接收任务并正常退出。

---

## 7. 平台与交付门禁

Phase 分支合并前，根据改动范围执行以下门禁：

1. Windows 或开发机 PostgreSQL OFF 的 Debug/Release 快速回归。
2. Linux Debug/Release production ON 构建与完整 CTest。
3. PostgreSQL 空库迁移、Repository fixture、RLS 与真实 API smoke。
4. Docker 冷构建、healthcheck、non-root、双角色、后台任务与优雅停止。
5. 前端 generated-type drift、TypeScript、lint、format、unit/component、production build 与当前纵向切片 E2E。
6. Release Candidate 的 Chromium、Firefox、WebKit、Accessibility 与固定数据集性能矩阵。
7. Markdown 链接、格式、旧路径和秘密模式扫描。

Phase 2 发布门禁还必须执行 `frontend` 的 bundle、直接依赖许可证、production source map 和秘密扫描。联网环境执行 `pnpm audit --prod --audit-level high`；离线结构检查不能替代漏洞数据库结果。

代码改动至少运行仓库根目录的 `quality_check.ps1` 或平台等价命令。涉及 migration、PostgreSQL adapter、Drogon、Provider、Scheduler、Outbox、Docker 或生产装配的改动，必须补对应真实环境门禁，不能只依赖 PostgreSQL OFF 结果。

---

## 8. 证据与合并规则

交付摘要只记录有决策价值的证据：commit、操作系统与关键依赖版本、命令类别、测试数量和 PASS/FAIL 结论。原始日志、认证材料、完整外部响应和本机私有路径不得提交。

以下情况阻断合并：

1. 任一必需门禁失败或未执行且无明确豁免。
2. 测试数量意外减少且没有说明。
3. 金融、事务、RLS、认证、Dangerous Delete、Transfer、ExchangeRate 或 Outbox 的回归。
4. 新增 API 缺少成功路径、至少一个错误路径或 OpenAPI 更新。
5. 真实环境证据与待合并 commit 不一致。
6. 文档链接失效、能力声明与代码不一致或秘密扫描失败。
