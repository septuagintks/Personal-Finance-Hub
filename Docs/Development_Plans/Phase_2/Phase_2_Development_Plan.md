# Personal Finance Hub Phase 2 开发计划

Version: 3.0
Backend: C++23
Frontend: Vue 3 + TypeScript
Product Name: `Candy's Ledger`（暂定）
Baseline: `main@e457387f45cd182cf0d3383aebb6d6fd2d4afb04`
Branch: `feature/phase2-product-experience`
Status: Complete

---

## 1. 阶段目标

Phase 2 在 Phase 1 稳定后端之上交付可日常使用、可维护、可发布的个人财务 Web 产品。阶段结果把认证、账户、流水、转账、报表和维护能力组织为完整用户工作流，并建立同源部署、可访问性、性能、安全和跨平台交付门禁。

### 1.1 范围

- Vue 3、Vite、TypeScript Strict、Vue Router、Pinia 和 OpenAPI 生成类型。
- 同源浏览器会话、统一 API Client、结构化错误和持久化幂等。
- 账户、分类、标签、偏好、流水和 Transfer 聚合完整工作流。
- Dashboard、净资产趋势、维度报表和服务端 CSV 导出。
- 用户审计、余额维护、`USER` / `OPERATOR` 授权与运行可见性。
- Web Edge、Linux production ON、PostgreSQL、Docker、三浏览器和恢复门禁。

### 1.2 不在范围

- 银行、信用卡、开放银行和支付平台同步。
- CSV、Excel、PDF、OCR、邮件或其他外部账单导入；本阶段只交付数据导出。
- 真实支付或资金划转写操作。
- 完整加密货币实时定价和第三个汇率 Provider。
- 预算、共享账本、家庭协作和多组织权限模型。
- 原生移动端、离线优先和 PWA 后台写入。

当前实时汇率路径覆盖 20 种法币与 BTC。其余 12 种加密货币只有在完整历史快照存在时可以降级，否则返回不可用；前端不得补默认汇率或把部分结果呈现为完整结果。

---

## 2. 架构与产品边界

### 2.1 运行拓扑

```text
Browser
   -> Same-origin Web Edge
       -> static Vue assets
       -> /api/* reverse proxy
           -> Drogon Presentation
               -> Application -> Domain
                    ^              ^
              Infrastructure -----+
```

- 生产 Web Edge 使用 non-root Nginx，Backend 不发布宿主端口。
- 浏览器不直接访问 PostgreSQL、Provider、Outbox 或 Scheduler。
- OpenAPI 生成类型是前端 DTO 的事实来源；前端不复制金融、权限或事务规则。
- API 金额和汇率保持十进制字符串，日期按用户 IANA 时区展示。
- `Personal Finance Hub (PFH)` 保持仓库、后端和 API 名称；`Candy's Ledger` 是暂定的面向用户产品名。

### 2.2 浏览器会话

- Access Token 只保存在内存中。
- Refresh Token 使用 `HttpOnly`、`Secure`、`SameSite=Strict` Cookie。
- 同源端点校验 Origin、Fetch Metadata、公开 Host 和受信代理 scheme。
- 单标签页并发 401 复用一次 refresh，跨标签页只同步互斥和会话状态，不传递 Token。
- Logout、reuse detection 或服务端撤销会清除用户缓存并使当前会话失效。

### 2.3 用户面与运维面

- 普通用户只能访问自己的财务事实、审计记录和余额维护动作。
- 公共注册只能获得 `USER`；`OPERATOR` 只能由仓库外部署流程授予。
- Operations API 返回状态、计数、时间和脱敏标识，不返回事件 payload、凭据或财务正文。
- HTTP 请求始终使用受限 request role；background role 只执行批准的跨租户后台查询。

---

## 3. 交付路线

### 3.1 S01-S04 工程、契约与会话

| Step | 结果 |
| ---- | ---- |
| S01 | 冻结前端技术栈、威胁模型、性能预算和阶段边界 |
| S02 | 扩展 OpenAPI、Web Cookie 会话、分页、错误与幂等基础 |
| S03 | 建立可构建、可测试、可部署的 Vue 应用和同源 Web Edge |
| S04 | 完成注册、登录、恢复、刷新、退出和多标签页会话闭环 |

验收重点：生成类型无漂移；认证材料不进入浏览器持久化；同源保护 fail closed；公开入口、认证页和应用 Shell 可在 Desktop/Mobile 使用。

### 3.2 S05-S08 日常记账工作流

| Step | 结果 |
| ---- | ---- |
| S05 | 账户详情、修改、归档、恢复和危险删除 |
| S06 | 分类、标签、偏好和记账元数据闭环 |
| S07 | 流水筛选、游标分页、创建、详情、更正和软删除 |
| S08 | Transfer 三模式、手续费、聚合查询、更正和软删除 |

验收重点：所有写入保持租户隔离、版本保护、幂等、Audit、Outbox 和缓存失效原子性；Transaction 与 Transfer 更正采用追加模型，不原地改写历史事实。

### 3.3 S09-S12 分析、维护与交付

| Step | 结果 |
| ---- | ---- |
| S09 | Dashboard、净资产趋势、维度分析和服务端 CSV |
| S10 | 用户审计、余额维护、持久化角色、健康与运维可见性 |
| S11 | 响应式、Accessibility、安全、依赖、性能和发布硬化 |
| S12 | Windows/Linux/PostgreSQL/Drogon/Docker、恢复与回滚签署 |

验收重点：报表由后端权威计算；用户与 Operator 权限分离；三浏览器和 WCAG 关键路径通过；真实数据库、Provider、后台任务、备份恢复和镜像回滚均有目标环境证据。

---

## 4. 最终交付结果

### 4.1 用户工作流

- 注册、登录、静默恢复、Token rotation/reuse detection 和退出。
- 账户、分类、标签、偏好及归档状态管理。
- 普通流水的组合筛选、稳定游标分页、创建、历史详情、原子更正和软删除。
- Transfer 聚合的三种输入模式、三种手续费来源、历史详情、原子更正和聚合软删除。
- Dashboard、净资产趋势、root 分类/账户/标签维度和有界 CSV 导出。
- 当前用户审计、余额缓存重建和受控 Operator 运行面。

### 4.2 持续规则

- 普通 Transaction 和 Transfer 更正只追加替代事实，不原地覆盖金额、账户或时间。
- Transfer 双腿和 grouped Adjustment 只能由聚合用例写入。
- 金融写入、幂等结果、Audit、Outbox 和缓存失效共享 Unit of Work。
- 列表使用确定排序和不透明 cursor；跨用户私有资源统一返回 404。
- CSV 使用 RFC 4180、UTF-8、公式注入防护、366 天和 10,000 行上限。
- liveness 不依赖应用队列；readiness 检查必要依赖并保持响应脱敏。

---

## 5. 最终质量门禁

| 门禁 | 结果 |
| ---- | ---- |
| Windows Debug / PostgreSQL OFF | 382/382 PASS |
| Windows Release / PostgreSQL OFF | 382/382 PASS |
| Linux Debug / PostgreSQL ON | 384/384 PASS |
| Linux Release / PostgreSQL ON | 384/384 PASS |
| Linux PostgreSQL OFF | 382/382 PASS |
| Flyway / PostgreSQL | V1-V10 migrate/info/validate/no-op、RLS、角色、Repository PASS |
| Frontend unit | Vitest/MSW 63/63 PASS |
| Windows browser | Edge 37/37 PASS |
| Release browsers | Chromium/Firefox/WebKit 111/111 PASS |
| Accessibility | 键盘、焦点、对比度、200% zoom、reduced motion、axe PASS |
| Performance | Daily 10,000 与 Stress 100,000 基准预算 PASS |
| Runtime | 同源 API、Provider、Outbox/Scheduler、restart、SIGTERM PASS |
| Recovery | PostgreSQL restore、forward fix 与 immutable image rollback PASS |

门禁还覆盖 OpenAPI drift、TypeScript、ESLint、Prettier、bundle、许可证、漏洞、source map、秘密扫描、双向数据库角色成员关系拒绝、外部 TLS scheme 透传和运行日志脱敏。

---

## 6. 最终能力边界

- 完整加密货币实时定价未交付；系统只使用现有主备 Provider 与完整历史降级语义。
- 外部账单导入、银行和支付平台接入保留到 Phase 3。
- Web 是响应式产品，不承诺原生移动端、离线写入或多设备实时协作。
- `USER` / `OPERATOR` 是固定部署角色，不是通用 IAM 或组织权限系统。
- 多节点调度和额外消息中间件不属于本阶段。

---

## 7. 交付入口

- [Phase 2 开发记录](../../Archive/Phase_2_Development_Record.md)
- [S01-S04 交付摘要](../../Archive/Phase_2_S01-S04_Delivery_Summary.md)
- [S05-S08 交付摘要](../../Archive/Phase_2_S05-S08_Delivery_Summary.md)
- [S09-S12 交付摘要](../../Archive/Phase_2_S09-S12_Delivery_Summary.md)
- [总体开发计划](../Overall_Development_Plan.md)
- [前端设计](../../Architecture/13_Frontend_Design.md)
- [测试策略](../../Architecture/16_Testing_Strategy.md)
