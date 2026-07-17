# Personal Finance Hub 前端设计

Version: 2.0
Product Name: `Candy's Ledger` (Tentative)
Frontend: Vue 3 + TypeScript Strict + Vite
Status: Approved

---

## 1. 定位与命名

前端把 Phase 1 后端能力组织为可日常使用的个人财务产品。`Personal Finance Hub (PFH)` 保持仓库、工程、架构、API 和内部命名；`Candy's Ledger` 是暂定的面向用户产品名。

- 产品名从单一品牌配置读取，用于公开落地页、认证页、浏览器标题和应用 Shell。
- 后续改名不得修改 API 路径、数据库对象、C++ namespace 或内部模块名。
- 首个可运行前端入口落地时，根目录 `README.md` 才注明产品名。
- 登录后直接进入用户默认工作页，不用营销内容阻断核心流程。

---

## 2. 技术基线

| 类别 | 固定选择 |
| ---- | -------- |
| Runtime | Node.js 24 LTS，版本以仓库 `.node-version` 为准 |
| Package Manager | `pnpm`，版本以根 `packageManager` 为准 |
| Framework | Vue 3 Composition API + `<script setup>` |
| Build | Vite |
| Language | TypeScript Strict |
| Router / State | Vue Router 4 / Pinia |
| UI / Icons | Element Plus / `@lucide/vue` |
| Charts | ECharts，按需加载 |
| Decimal | `decimal.js` |
| HTTP | Axios |
| Contract | OpenAPI 3.1 + `openapi-typescript` 生成类型 |
| Test | Vitest、Vue Test Utils、MSW、Playwright、axe-core |

只提交 `pnpm-lock.yaml`，不得混用 npm、Yarn 或第二份 lockfile。生成的 API 类型位于 `frontend/src/generated/`，不得手工编辑。

---

## 3. 运行拓扑

```text
Browser
   -> Same-origin Web Edge
       -> static Vue assets
       -> /api/* reverse proxy
           -> Drogon Presentation -> Application -> Domain
```

- 开发环境由 Vite 代理 `/api`；生产环境由 non-root Nginx 提供静态资源并反向代理 API。
- Web 与 API 默认同源并使用 HTTPS，不开放宽泛 CORS。
- 浏览器不直接访问 PostgreSQL、汇率 Provider、Outbox 或 Scheduler。
- 前端不复制金融、汇率、报表、权限或事务规则，后端响应是最终事实。

---

## 4. 代码边界

```text
View -> Feature Component -> Store/Composable -> API Service -> Generated Types
```

| 层次 | 职责 | 禁止事项 |
| ---- | ---- | -------- |
| View | 路由参数、页面级加载与功能编排 | 直接拼 API URL 或实现金融计算 |
| Feature Component | 一个业务交互及其可复用子组件 | 隐式访问全局状态、复制权限规则 |
| Store / Composable | 会话、偏好、公共元数据和跨组件流程 | 长期缓存用户财务明细 |
| API Service | 请求、取消、认证、TraceId 和错误映射 | 手写重复 DTO、吞掉服务端错误 |
| Generated Types | OpenAPI 的 TypeScript 投影 | 手工修改生成文件 |

`frontend/src` 使用以下稳定目录：

```text
app/          应用初始化、品牌和全局插件
router/       路由与访问守卫
views/        路由页面
features/     纵向业务切片
components/   无业务所有权的共享组件
stores/       Pinia stores
services/     HTTP 与 API adapters
generated/    OpenAPI 生成类型
i18n/         zh-CN / en-US 文案
styles/       tokens、reset 和全局样式
test/         test setup、MSW 与 fixtures
```

---

## 5. API 与会话

### 5.1 契约

- OpenAPI operation 必须有唯一 `operationId`。
- API DTO 从 OpenAPI 生成，生成结果漂移会使质量门禁失败。
- 列表使用稳定游标和确定性排序，不把全量流水加载到浏览器。
- 账户、分类、标签、金融创建/更正和 dead-letter 重试携带 `Idempotency-Key`。Store 以规范化请求负载标识用户意图：响应未确认时，同一意图重试必须复用原键；只有成功响应、负载改变或会话清理后才能释放该键。
- 非幂等写请求不会被 Axios 自动重试。
- `409` 表达幂等冲突或版本冲突，前端必须保留用户输入并显示可恢复动作。

### 5.2 Web 会话

- Web 前端只调用 `/api/v1/web/auth/*`。
- Access Token 只存在于当前页面内存，不写入 `localStorage`、`sessionStorage` 或 IndexedDB。
- Refresh Token 只存在于 `HttpOnly`、`Secure`、`SameSite=Strict` Cookie，JavaScript 不可读取。
- 应用启动执行一次静默 refresh；单标签页并发 401 共享同一 Promise。
- 跨标签页使用 Web Locks（不可用时使用租约式 localStorage mutex）串行 refresh，并通过 `BroadcastChannel` 只广播会话状态，不广播 Token。
- Session Store 只在内存保存服务端返回的唯一 `USER` 或 `OPERATOR` role；自动 refresh 只有通过当前会话 generation 校验后，才能同时接纳新 Access Token 和 role，退出或新认证开始前发出的迟到 refresh 不得更新权限投影。
- Logout、reuse detection 或 session 撤销会清除内存 Token、用户 Store 和路由状态。
- 现有 `/api/v1/auth/*` JSON Token API 只服务非浏览器调用方，Web 前端不得使用。

### 5.3 错误

API Client 将错误统一投影为：

```typescript
interface ApiError {
  errorCode: string;
  message: string;
  traceId: string;
  fieldErrors: Record<string, string>;
  retryable: boolean;
}
```

- 表单字段错误显示在对应输入处，页面级错误保留 TraceId。
- `401` 先进入 single-flight refresh；refresh 失败才退出会话。
- `403` 不改写为 404；`404` 用于资源不存在或不属于当前用户。
- `500` / `502` 只展示脱敏消息，可重试性以后端字段为准。

---

## 6. 金额、汇率与时间

- 金额和汇率在 API、Store 与组件属性中始终保持十进制字符串。
- 业务预览使用 `decimal.js`；禁止使用 `Number`、`parseFloat` 或隐式数值转换进行财务计算。
- 图表可以把后端已聚合值转换为有限数值用于像素映射，Tooltip、表格和导出继续使用原始字符串。
- 转账派生值仅作预览，最终金额、汇率和 Half-Even 舍入以后端响应为准。
- 日期按用户 `locale`、`timezone` 和 `dateFormat` 显示，不手写固定 UTC offset。
- Dashboard 月份、报表窗口和历史汇率选择全部由后端确定。
- 汇率不可用时不显示零、不沿用旧总计，也不拼装部分报表。

---

## 7. 状态与缓存

### 7.1 可持久化

浏览器持久化仅允许：

- 公共货币元数据及 ETag。
- 不含用户身份的界面选择，例如用户主动关闭的介绍提示。
- 跨标签页 refresh mutex 的短期租约元数据，不含 Token。

### 7.2 仅内存

- Access Token 和认证用户身份。
- 用户偏好、账户、分类、标签和报表。
- 表单草稿、分页游标和错误对象。

登录、Logout、session 撤销和用户切换必须清空全部用户级状态。写操作成功后只失效受影响资源；偏好中的基准币种或时区变化会使所有报表缓存失效。

---

## 8. 页面与交互

| 页面 | 核心职责 |
| ---- | -------- |
| Public Landing | 使用产品名建立身份并进入注册/登录，不伪装已交付能力 |
| Login / Register | 安全会话、字段错误和密码管理器兼容 |
| Dashboard | 净资产、当月收支、账户分布和 Top 分类 |
| Accounts | 账户生命周期、余额、归档、恢复和危险删除 |
| Transactions | 游标分页、筛选、创建、详情、更正和软删除 |
| Transfers | 三种模式、手续费、聚合详情、更正和软删除 |
| Reports | 趋势、分类/账户/标签维度和服务端导出 |
| Settings | 分类、标签和偏好 |
| Maintenance | 当前用户审计和余额维护 |
| Operations | `OPERATOR` 任务、Outbox 和 dead letter 状态 |

- Desktop 优先高频扫描和录入，Mobile 保证核心记账、转账和查询可完成。
- 模式选择使用 segmented control，二元设置使用 toggle/checkbox，工具按钮使用 Lucide icon 与 Tooltip。
- 危险操作显示影响范围并要求明确确认，不能只依赖按钮颜色。
- 图表提供同数据表格或可访问摘要。
- 所有关键路径支持键盘操作并满足 WCAG 2.2 AA。
- Maintenance 对所有已认证用户开放且只消费当前用户投影；Operations 使用独立 `OPERATOR` route guard。隐藏导航不是授权控制，直接访问无权路由进入稳定 403 页面，服务端仍是最终权限边界。
- 只读宽表在 Desktop/Tablet 提供可聚焦滚动区，在窄屏转换为字段标签明确的纵向行，不依赖页面级横向滚动。

---

## 9. 威胁模型

| 威胁 | 主要控制 | 验收证据 |
| ---- | -------- | -------- |
| XSS 读取 Token | Access Token 仅内存、Refresh HttpOnly、严格 CSP、无 `v-html` 用户输入 | 浏览器存储与 CSP E2E |
| CSRF | SameSite=Strict、同源 Origin / Fetch Metadata、Web auth 独立路径 | 跨站 POST 拒绝测试 |
| Token rotation 竞态 | 单页 single-flight、跨标签页 refresh mutex | 并发 refresh E2E |
| 横向越权 | JWT UserId、Application ownership、参数化 SQL、FORCE RLS | 两用户 API/PostgreSQL 测试 |
| 重复财务写入 | 持久化 `Idempotency-Key`、同事务业务结果 | 超时与并发重试测试 |
| 浮点精度损失 | Decimal string + `decimal.js`，后端最终计算 | 大数与边界测试 |
| CSV formula injection | 文本字段转义，金额按验证后的 Decimal string 导出 | 导出 fixture |
| 运维提权 | 服务端持久化角色、Operator Filter、部署网络保护探针 | 伪造 JWT role 测试 |
| 敏感信息泄漏 | 脱敏错误、TraceId、无 Token 日志、构建产物扫描 | 静态与 runtime 扫描 |

---

## 10. 浏览器与性能预算

### 10.1 浏览器矩阵

- Playwright 随锁文件固定的 Chromium、Firefox 和 WebKit。
- Desktop 视口至少覆盖 1440x900 与 1280x720。
- Mobile 至少覆盖 390x844 与 360x800。
- 目标是当前两个稳定大版本；不支持 Internet Explorer 或旧版 EdgeHTML。

### 10.2 参考数据集

- Daily：10,000 条流水、20 个账户、200 个分类/标签组合。
- Stress：100,000 条流水、50 个账户、500 个分类/标签组合。
- 两套数据都包含跨币种、Transfer、signed Adjustment、DST 和缺失汇率场景。

### 10.3 初始预算

| 指标 | Daily | Stress |
| ---- | ----- | ------ |
| 流水第一页 API p95 | <= 300 ms | <= 500 ms |
| Dashboard API p95 | <= 500 ms | <= 800 ms |
| 筛选后首屏可交互 | <= 1.0 s | <= 1.5 s |
| CSV 首字节 | <= 2 s | <= 5 s |
| Public Landing LCP | <= 2.5 s | 不适用 |
| INP / CLS | <= 200 ms / <= 0.1 | <= 300 ms / <= 0.1 |
| 初始 JS gzip | <= 250 KiB | 同左 |

预算在 P2-S01 固定，在 P2-S11 以目标 Linux/Docker 环境复核。调整预算必须保留测量条件、用户影响和明确理由。

---

## 11. 持续验收

- `pnpm install --frozen-lockfile`、typecheck、lint、unit、component 和 production build。
- OpenAPI 生成文件无漂移。
- Playwright 当前切片 E2E；Release Candidate 执行三浏览器全量。
- axe 自动检查加键盘、焦点、名称与对比度人工检查。
- 金额字符串、时区、用户切换、401、409、422、500、离线和取消状态。
- 构建产物、浏览器存储、日志和 source map 无 Token、Cookie、密码或私有配置。

测试矩阵与合并门禁见 [测试策略](16_Testing_Strategy.md)，开发顺序见 [Phase 2 开发计划](../Development_Plans/Phase_2/Phase_2_Development_Plan.md)。
