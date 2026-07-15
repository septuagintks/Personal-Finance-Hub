# Personal Finance Hub Phase 2 开发计划

Version: 2.1
Backend: C++23
Frontend: Vue 3 + TypeScript
Product Name: `Candy's Ledger` (Tentative)
Baseline: `main@e457387f45cd182cf0d3383aebb6d6fd2d4afb04`
Branch: `feature/phase2-product-experience`
Status: Draft

---

## 1. 阶段目标

Phase 2 在 Phase 1 稳定后端之上交付可日常使用、可维护、可发布的个人财务 Web 产品。阶段重点是把已经验证的金融能力组织为完整用户工作流，并补齐产品界面必需的查询、更正、导出、审计和运行可见性。

### 1.1 用户结果

Phase 2 完成后，普通用户应当可以：

1. 通过安全的浏览器会话完成注册、登录、续期和退出。
2. 管理账户、分类、标签、偏好以及归档状态。
3. 查询、筛选、创建、更正和删除普通流水。
4. 创建、查询、更正和软删除完整转账聚合，包括跨币种和手续费场景。
5. 通过 Dashboard、趋势和维度报表理解净资产与现金流。
6. 导出自己的流水，并查看个人审计记录和可执行的维护动作。
7. 在汇率不可用、网络失败、并发冲突或后台任务异常时得到准确、可追踪且不泄密的反馈。

### 1.2 工程结果

- 建立 Vue 3、Vite、TypeScript Strict、Vue Router、Pinia、Element Plus 和 ECharts 前端。
- 使用 OpenAPI 生成前端 DTO 类型，避免手写一套漂移的 API 模型。
- 补齐列表、分页、详情、更正、报表和维护 API。
- 建立浏览器安全会话、写请求幂等和统一错误处理。
- 建立前端 unit/component、API contract、Playwright E2E、Accessibility 和性能门禁。
- 交付同源 Web Edge、Linux production ON 和 Docker 运行方案。

### 1.3 明确不在范围

- 银行、信用卡、开放银行和支付平台同步。
- CSV、Excel、PDF、OCR 和邮件账单导入；Phase 2 只提供数据导出。
- 真实支付或资金划转写操作。
- 完整加密货币实时定价和第三个汇率 Provider。
- 预算、共享账本、家庭协作和多组织权限模型。
- 原生移动端、离线优先、PWA 离线写入和后台同步队列。
- 通用 IAM/RBAC 管理台；本阶段只提供固定的 `USER` / `OPERATOR` 边界。
- WebSocket 实时协作、额外消息中间件和多节点调度重构。

完整加密货币定价只作为能力边界声明：当前实时路径覆盖 20 种法币与 BTC；其余 12 种加密货币只有在完整历史快照存在时可以降级，否则返回不可用。前端不得补默认汇率、隐藏缺口或把部分结果显示为完整结果。

---

## 2. Phase 1 基线

Phase 2 从最新 `main` 开始，不重新实现或削弱以下能力：

| 基线       | 已有结果                                                             | Phase 2 约束                                             |
| ---------- | -------------------------------------------------------------------- | -------------------------------------------------------- |
| 金融模型   | `Decimal`、`Money`、ExchangeRate、Transfer 聚合                      | 金额和汇率继续使用十进制字符串，不进入 JS `Number` 计算  |
| 数据与租户 | PostgreSQL 16+、Flyway V1-V6、FORCE RLS、双数据库角色                | 新表、新查询和运维端点必须保持 fail-closed 隔离          |
| 核心 API   | 认证、账户、分类、标签、偏好、流水写入、转账和三类报表               | Phase 2 以扩展契约为主，不绕过 Application 或 Repository |
| 一致性     | Unit of Work、Transactional Outbox、审计                             | 新业务事实与事件继续同事务提交                           |
| 后台运行   | Outbox Publisher、Scheduler、汇率刷新、认证清理                      | UI 不直接操作数据库租约或 Provider                       |
| 交付       | Windows 349/349、Linux production ON 351/351、PostgreSQL/Docker PASS | 现有门禁不得减少或被前端测试替代                         |

Phase 2 的第一项代码变更前，必须确认上述基线在 Phase 2 分支仍可重复。测试数量可以随新增覆盖增加，不能无说明减少。

---

## 3. 架构与产品边界

### 3.1 运行拓扑

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

- 开发环境由 Vite 同源代理 `/api`，生产环境使用 non-root Nginx 提供静态资源并反向代理 API。
- 浏览器不直接访问 PostgreSQL、Provider、Outbox 或 Scheduler。
- 生产环境不开放宽泛 CORS；Web 与 API 默认使用同一站点和 HTTPS。
- 后端仍是金额、汇率、权限、事务、报表和维护结果的唯一事实来源。

### 3.2 前端分层

```text
View -> Feature Component -> Store/Composable -> API Service -> Generated API Types
```

- View 负责页面编排、路由参数和页面级状态。
- Feature Component 负责可复用交互，不直接拼接 API URL。
- Pinia 只保存跨页面会话、偏好和轻量元数据；服务端事实通过明确加载和失效策略管理。
- API Service 统一处理认证、TraceId、取消、超时和错误映射。
- OpenAPI 生成类型是 API DTO 的前端事实来源，生成文件不手工编辑。
- Domain 规则不得复制到前端；前端推导只用于输入预览，提交结果以后端为准。

### 3.3 浏览器会话

Phase 2 为 Web 前端增加独立的同源会话端点，不破坏 Phase 1 已发布的非浏览器 Token 契约：

- Access Token 只保存在内存中，不写入 `localStorage`、`sessionStorage` 或 IndexedDB。
- Refresh Token 由后端通过 `HttpOnly`、`Secure`、`SameSite=Strict` Cookie 管理，前端 JavaScript 不可读取。
- Web 会话端点统一位于 `/api/v1/web/auth/*`；Cookie 的 `Path` 固定为 `/api/v1/web/auth` 并设置明确有效期。开发环境使用本地 TLS，不为方便调试降低生产 Cookie 属性。
- 页面恢复通过单次静默 refresh 建立会话；单标签页并发 401 复用同一个 refresh，跨标签页通过不含秘密的互斥与状态广播串行轮换。
- Refresh rotation、旧 Token 重用检测、logout 撤销和短期 Access Token 规则保持不变。
- Cookie 认证端点校验同源 `Origin` / Fetch Metadata，并拒绝不受信跨站请求。
- 登录、刷新、退出、错误日志和客户端遥测都不得记录 Token、Cookie 或密码。
- 现有 `/api/v1/auth/*` JSON Token 端点只作为非浏览器兼容契约保留；Web 前端不得调用或把返回的 Refresh Token 写入浏览器存储。

### 3.4 金额、汇率与时间

- API 金额与汇率始终是十进制字符串。
- 表单规范化和展示使用 `decimal.js` 或等价十进制库；禁止 `parseFloat` 后参与业务计算。
- 图表可以把已经由后端聚合的值转换为有限数值用于像素映射，但 Tooltip、表格和导出继续使用原始字符串。
- 日期显示遵循用户 `locale`、`timezone` 和 `dateFormat`；禁止手写固定 UTC offset。
- 报表月份继续由后端按用户 IANA 时区计算半开窗口。
- 缺失汇率不显示为零、不沿用前端缓存中的旧总计，也不参与部分聚合。

### 3.5 API 与数据获取

- 新 API 先更新 `10_REST_API_OpenAPI.json`、DTO 和错误语义，再实现后端与前端。
- 列表接口使用稳定游标分页和确定性排序；不得把全量流水一次加载到浏览器。
- 筛选条件可分享时写入 URL query，分页游标和临时表单状态不进入长期缓存。
- 金融写请求使用用户域内的幂等键；同键同请求返回原结果，同键不同请求返回冲突。
- 前端不自动重试非幂等写请求；用户主动重试时复用同一操作意图的幂等键。
- 用户切换、logout 和 session 撤销时清除全部用户级内存缓存。浏览器持久化只允许无敏感信息的公共货币元数据和界面偏好。

### 3.6 用户面与运维面

- 普通用户只能查询自己的财务事实、审计记录和账户维护状态。
- 用户审计 Query 的 `operator_user_id` 只能来自认证上下文，SQL 必须强制限定该值；API 不接受任意用户 ID 或通用系统审计过滤。
- 余额重建是用户限定的 Application Use Case，不接受任意 `userId`，并写入审计。
- Outbox、Scheduler 和 dead letter 属于运维面，必须经过独立 `OPERATOR` 授权。
- 公共注册只能获得 `USER`；`OPERATOR` 只能由仓库外部署流程授予，不提供前端自助提权。
- 运维响应只返回状态、计数、时间和脱敏标识，不返回事件 payload、凭据或个人财务正文。
- 后台数据库角色不得进入 HTTP 请求路径；运维 API 仍使用受限 request role 和 Application 边界。

### 3.7 交互与可访问性

- `Personal Finance Hub (PFH)` 保持仓库、工程、架构、API 和内部命名；面向用户的前端产品名暂定为 `Candy's Ledger`。
- 产品名集中定义并用于公开落地页、认证页、浏览器标题和应用 Shell，不在组件中散落硬编码；后续改名不得牵动后端命名或数据契约。
- 公开落地页使用产品名传达产品身份；登录后的第一屏仍是实际 Dashboard，不让营销内容阻断核心工作流。
- 根目录 `README.md` 在首个可运行前端入口落地的同一提交中注明产品名；在此之前继续只描述 PFH 项目，不提前宣称前端已经交付。
- 桌面端优先支持高频扫描和快速录入，移动端保证核心记账、转账和查询可完成。
- 工具操作使用图标与 Tooltip，模式选择使用 segmented control，二元设置使用 toggle/checkbox。
- 危险删除、聚合删除和 dead-letter 重试必须显示影响范围并要求明确确认。
- 所有关键流程可仅用键盘完成，并满足 WCAG 2.2 AA 的可感知、焦点和对比度要求。
- 图表提供同数据表格或可访问摘要，不能让颜色成为唯一信息载体。

---

## 4. 纵向切片规则

每个业务 Step 按固定顺序交付：

1. 在 OpenAPI 和架构文档中固定路由、DTO、权限、错误和能力边界。
2. 实现 Application、Repository/Query、Presentation 与必要 migration。
3. 完成 Unit、Use Case、PostgreSQL 和 API contract 测试。
4. 重新生成前端 API 类型，并通过契约漂移门禁。
5. 实现 Store/Composable、页面、组件和交互状态。
6. 完成 component 与真实后端 E2E，再更新交付摘要。

一个 Step 只有在代码、测试、OpenAPI 和用户可见行为同时完成后才结束。不得先用前端 mock 宣称业务功能交付，也不得把跨 Step 的临时兼容层长期保留。

---

## 5. 页面与契约增量

### 5.1 页面地图

| 路由                                 | 页面                        | 访问边界   |
| ------------------------------------ | --------------------------- | ---------- |
| `/login`、`/register`                | 认证                        | 未登录用户 |
| `/dashboard`                         | 当月摘要和净资产            | `USER`     |
| `/accounts`、`/accounts/:id`         | 账户、余额和归档            | `USER`     |
| `/transactions`、`/transactions/:id` | 流水列表、详情和更正        | `USER`     |
| `/transfers/new`、`/transfers/:id`   | 转账创建和聚合详情          | `USER`     |
| `/reports`                           | 趋势与维度报表              | `USER`     |
| `/settings/categories`               | 收入/支出分类树             | `USER`     |
| `/settings/tags`                     | 标签                        | `USER`     |
| `/settings/preferences`              | 语言、时区、币种和界面偏好  | `USER`     |
| `/maintenance`                       | 个人审计和余额维护          | `USER`     |
| `/operations`                        | 任务、Outbox 和 dead letter | `OPERATOR` |

### 5.2 计划新增或扩展的契约

最终字段以 OpenAPI 为准，Phase 2 至少需要覆盖：

| 资源         | 契约增量                                                                           |
| ------------ | ---------------------------------------------------------------------------------- |
| Auth         | 新增 `/api/v1/web/auth/*` Cookie 会话、会话恢复和同源保护；保留非浏览器 Token 契约 |
| Accounts     | 详情、归档筛选、元数据修改、取消归档、余额重建                                     |
| Categories   | 名称/排序修改、同 board 重排和软删除恢复                                           |
| Tags         | 重命名、软删除恢复和稳定冲突处理                                                   |
| Transactions | 游标分页列表、详情、组合筛选和原子更正                                             |
| Transfers    | 游标分页列表、聚合级软删除和原子更正                                               |
| Reports      | 净资产趋势、分类/账户/标签维度和估值状态                                           |
| Export       | 与流水筛选一致的服务端 CSV 流式导出                                                |
| Audit        | 当前用户审计日志分页查询                                                           |
| Operations   | readiness、任务运行摘要、Outbox/dead-letter 状态和受控重试                         |

普通 Transaction 继续采用追加与软删除，不提供无审计的原地金额更新。更正必须在一个 Unit of Work 内创建替代流水、软删除原流水、关联更正关系、失效余额缓存并登记 Audit/Outbox。Transfer 不允许通过普通 Transaction 端点拆腿修改。

---

## 6. 开发路线总览

| Step   | 工作包                       | 主要出口                                     |
| ------ | ---------------------------- | -------------------------------------------- |
| P2-S01 | 阶段基线与设计冻结           | 分支、差距矩阵、技术与安全约束               |
| P2-S02 | API 契约与会话基础           | OpenAPI、浏览器会话、分页/幂等公共能力       |
| P2-S03 | 前端工程骨架                 | 可构建、可测试、可部署的应用壳               |
| P2-S04 | 认证与会话体验               | 注册、登录、恢复、刷新、退出 E2E             |
| P2-S05 | 账户管理                     | 账户详情、余额、归档、恢复和危险删除         |
| P2-S06 | 分类、标签与偏好             | 记账元数据和个性化设置闭环                   |
| P2-S07 | 流水工作流                   | 列表、筛选、创建、详情、更正和删除           |
| P2-S08 | 转账工作流                   | 三种模式、手续费、查询、原子更正和聚合软删除 |
| P2-S09 | Dashboard、报表与导出        | 趋势、维度分析、估值状态和 CSV               |
| P2-S10 | 审计、维护与运行可见性       | 用户维护面和受控运维面                       |
| P2-S11 | 产品硬化与 Release Candidate | 可访问性、安全、性能和全量回归               |
| P2-S12 | 跨平台交付与 Phase 签署      | Linux/Docker 门禁、归档和合并条件            |

---

## 7. 详细开发顺序

### 7.1 P2-S01 阶段基线与设计冻结

目标：把 Phase 2 的实现前提变成可验证事实，避免前端开始后再反向修改核心契约。

开发内容：

1. 从 `main@e457387` 创建并只在 `feature/phase2-product-experience` 推进 Phase 2。
2. 复跑 Phase 1 的开发机 Debug/Release PostgreSQL OFF 基线，记录测试数量和工具链。
3. 逐页对照 OpenAPI，确认第 5.2 节的后端差距、migration 需求和 DTO 依赖顺序。
4. 更新前端架构与测试策略，固定会话存储、缓存、时区、金额、代码生成和 E2E 规则。
5. 固定 Node LTS、`pnpm`、lockfile、浏览器矩阵和依赖升级策略，并写入版本文件。
6. 固定目录：`frontend/` 放置 Web 工程，后端现有分层目录保持不变。
7. 建立 Phase 2 威胁模型：XSS、CSRF、Token 泄漏、越权、重复提交、CSV 注入和运维提权。
8. 定义参考数据集和性能预算，包括 10,000 条日常流水与 100,000 条压力数据集。

验收：

- Phase 1 基线可重复，测试没有无说明减少。
- 页面、API、权限、migration 和测试之间有一一对应关系。
- 关键技术决策已写入当前架构文档，不依赖聊天记录或临时清单。
- 本 Step 不引入未经契约约束的业务实现。

### 7.2 P2-S02 API 契约与会话基础

目标：先建立所有页面共享的安全和数据访问基础。

开发内容：

1. 为现有和新增 OpenAPI operation 添加稳定 `operationId`，建立前端类型生成和 drift check。
2. 定义游标分页公共 DTO：稳定排序键、`nextCursor`、默认/最大 page size 和非法游标错误。
3. 扩展统一错误 DTO，支持受控字段错误、`trace_id` 和可重试语义；禁止透传底层异常。
4. 新增 `/api/v1/web/auth/*` 安全 Cookie 会话端点，复用现有 Auth Application 能力；现有 JSON Token 端点保持兼容但不供 Web 前端使用。
5. 实现静默 refresh、rotation、reuse detection、logout 清除 Cookie、同源请求校验和跨标签页 refresh 串行化。
6. 为 Transaction、Transfer 和其他不可安全重复的创建动作建立持久化幂等记录与清理策略。
7. 定义 `ETag` / `If-Match` 或 `expectedVersion` 的并发更新规则，稳定映射为 `409 Conflict`。
8. 建立生成 DTO、路由表和实际 Controller 的静态一致性门禁。

验收：

- 浏览器存储中不存在 Access/Refresh Token。
- 同时触发多次 401 时只发生一次 refresh；refresh 失败后所有等待请求一致退出会话。
- 两个标签页同时恢复或刷新时不会把正常并发误判为旧 Token 重用，也不会削弱真正的 reuse detection。
- 同幂等键同请求只产生一个业务事实，不同请求返回冲突。
- Cookie、Origin、错误脱敏和两用户隔离测试通过。
- OpenAPI 生成无未提交差异，前后端均不手写重复 DTO。

### 7.3 P2-S03 前端工程骨架

目标：交付一个可持续扩展的真实应用壳，不创建营销页或静态演示页。

开发内容：

1. 建立 Vue 3、Vite、TypeScript Strict、Vue Router、Pinia、Element Plus、ECharts 和 `decimal.js`。
2. 配置 ESLint、格式化、`vue-tsc`、Vitest、Vue Test Utils、MSW 和 Playwright。
3. 建立 `app`、`router`、`views`、`features`、`components`、`stores`、`services`、`generated` 和 `test` 边界。
4. 建立响应式公开落地页与应用 Shell、侧栏/移动导航、页面标题、全局通知、确认对话框和空状态；品牌配置统一使用暂定产品名 `Candy's Ledger`。
5. 建立 API Client：base URL、Bearer 注入、single-flight refresh、AbortSignal、TraceId 和错误映射。
6. 建立金额、日期、时区、枚举和可访问图表的共享适配器。
7. 路由级 lazy loading，按需引入 Element Plus 和 ECharts 模块。
8. 建立 Vite `/api` 代理与 Nginx production 配置，环境变量只允许公开构建配置。
9. 将前端 typecheck、lint、test、build 接入根目录质量入口。
10. 在首个可运行前端入口完成时同步更新根目录 `README.md`，说明 PFH 与前端产品名的关系。

验收：

- 干净环境可使用锁定依赖执行 install、typecheck、lint、unit test 和 production build。
- Desktop 与 Mobile Shell 无重叠、无横向溢出，键盘焦点顺序稳定。
- 产品名在落地页、认证页、浏览器标题和应用 Shell 中一致，仓库、API 和后端标识仍保持 PFH。
- API Client 可在 mock contract 下展示成功、401、409、422、500 和离线状态。
- 初始路由不加载 ECharts 和非当前页面业务 chunk。

### 7.4 P2-S04 认证与会话体验

目标：用真实后端完成浏览器认证生命周期。

开发内容：

1. 实现注册、登录、加载中、字段错误和密码可见性控制。
2. 应用启动时只执行一次会话恢复；未认证路由跳转登录，已认证用户返回原目标页。
3. 登录后加载用户偏好和公共货币元数据，再进入用户默认首页。
4. Access Token 到期时透明刷新并重放可安全重放的请求；写请求遵守幂等和显式重试规则。
5. logout、reuse detection 和服务端撤销后立即清除内存状态、用户缓存和受保护路由。
6. 使用 `BroadcastChannel` 或等价机制同步多标签页 logout，不传递 Token 内容。
7. 登录表单使用正确的 password manager autocomplete，不在遥测或错误对象中保留密码。

验收：

- 注册、登录、reload 恢复、过期刷新、旧 refresh 重用、logout 和多标签页退出 E2E 全部通过。
- 用户 A 退出后，用户 B 无法看到 A 的分类、偏好、账户名称或报表缓存。
- 网络中断时不伪造已登录状态，服务恢复后可以明确重试。
- 认证页面和错误提示满足键盘与屏幕阅读器基础门禁。

### 7.5 P2-S05 账户管理

目标：交付账户从创建到归档、恢复和危险删除的完整生命周期。

后端顺序：

1. 扩展账户列表以查询 active、archived 或 all，并添加账户详情接口。
2. 添加受版本保护的账户元数据修改；账户币种在存在流水后保持不可变。
3. 添加取消归档用例；归档和恢复都写 Audit/Outbox。
4. 保留危险删除的完整关联清理、确认计数和同事务审计规则。
5. 对列表、详情、版本冲突、跨用户 404 和 RLS 连接复用补真实 PostgreSQL/API 测试。

前端顺序：

1. 账户列表支持 active/archived 切换、类型和币种扫描，以及稳定空状态。
2. 账户详情展示余额、更新时间、归档状态和关联操作。
3. 创建/修改表单只提交字符串和枚举契约，不允许设置初始余额。
4. 归档说明其对新流水和转账的影响；恢复使用独立命令。
5. 危险删除显示关联数据影响，要求分步确认并输入账户名称，最终仍满足后端确认契约。

验收：

- 创建、详情、修改、归档、恢复和危险删除真实 E2E 通过。
- 并发修改返回可理解的 `409`，不会静默覆盖较新版本。
- 归档账户不能新增流水或参与转账，历史查询仍按契约显示。
- 危险删除后的 Transaction、Transfer、Tag relation、Cache、Audit 和 Outbox 满足数据库 fixture。

### 7.6 P2-S06 分类、标签与偏好

目标：完成所有记账表单共享元数据和个性化设置。

开发内容：

1. 为分类增加重命名、同 board 排序和恢复接口，禁止跨 income/expense board 移动。
2. 保持系统模板与用户副本边界；软删除分类继续解析历史流水，重新启用同一模板或同层级名称时恢复既有用户分类，不制造重复历史实体。
3. 为标签增加重命名和恢复接口；删除后重用同名标签时恢复原标签，活动标签重名返回稳定冲突。标签不参与金额计算。
4. 实现分类树、标签列表和偏好页面，写操作成功后精确失效对应 Store。
5. 偏好覆盖基准币种、locale、timezone、日期/数字格式、theme、默认首页和默认报表周期。
6. 初始至少交付 `zh-CN` 与 `en-US` 文案资源；未知 locale/timezone 由后端拒绝并保持原设置。
7. 公共 currencies 使用 ETag；分类、标签和偏好只保存在当前用户内存状态。
8. 偏好变化后刷新 Dashboard/Report，不复用旧基准币种聚合结果。

验收：

- 分类新增、启用模板、重命名、排序、软删除和恢复 E2E 通过。
- 标签新增、重命名、绑定、软删除和恢复 E2E 通过，历史 Transaction relation 保持可解释。
- 分类 board、父子归属、跨用户访问和重复标签约束有后端测试。
- 用户切换不会复用前一用户元数据。
- 时区、主题、语言和默认首页在 reload 后与服务端偏好一致。

### 7.7 P2-S07 流水工作流

目标：提供适合日常使用的流水账本，而不破坏追加式账务语义。

后端顺序：

1. 添加流水游标分页列表和详情 Query，稳定排序为 `occurred_at DESC, id DESC`。
2. 支持账户、类型、分类、标签、时间范围和关键字的组合筛选，并设定 page size 与范围上限。
3. 列表 DTO 明确 Income/Expense 正 magnitude、Adjustment signed 和 Transfer group 信息。
4. 实现原子更正用例：替代流水创建、原流水软删除、更正关系、缓存失效、Audit/Outbox 同事务。
5. 普通删除继续拒绝 Transfer leg 和 transfer-group Adjustment。
6. 为分页稳定性、并发写入、历史分类、标签、时区边界、RLS 和 NUMERIC 边界补测试。

前端顺序：

1. 实现 URL 驱动筛选、分页加载、表格/移动列表和详情页。
2. 实现 Income、Expense、Adjustment 创建表单；根据类型隔离分类 board。
3. 金额输入保留用户字符串，客户端只做格式预检，最终规范化以后端响应为准。
4. 实现标签绑定、软删除和更正流程；更正界面清楚展示原记录与替代记录。
5. 写操作提交时锁定当前意图并使用幂等键，不因超时自动创建第二笔流水。

验收：

- 收入、支出、正/负 Adjustment、标签、筛选、详情、更正和删除 E2E 通过。
- 月初/月末、RFC 3339 offset 和用户时区显示正确。
- 更正失败时原流水保持有效，不存在半个更正关系。
- 100,000 条参考数据下分页不重复、不漏项，浏览器不加载全量数据。

### 7.8 P2-S08 转账工作流

目标：让复杂转账具备准确、可理解且可恢复的产品体验。

后端顺序：

1. 添加转账聚合列表 Query，返回组级摘要而不是让前端拼接两条 leg。
2. 保持三种输入模式、同币种规则和 Source/Target/ThirdParty 手续费边界。
3. 实现聚合级软删除用例：保留 `transfer_groups` 审计事实，在一个 Unit of Work 内软删除两端与全部 Adjustment，并处理缓存、Audit 和 Outbox。
4. 实现聚合级原子更正用例：按确定顺序锁定旧聚合与账户，在同一 Unit of Work 内创建替代聚合、软删除旧聚合、写入更正关联并处理缓存、Audit 和 Outbox。
5. 不提供单 leg 删除或普通 Transaction 更正，也不允许前端串联“先删除、再创建”模拟转账更正。
6. 补同币种、跨币种、三种 mode、三种 fee source、并发锁序、原子更正和跨用户测试。

前端顺序：

1. 使用 segmented control 选择三种输入模式，只启用该模式的两个 authoritative 字段。
2. 使用十进制库显示派生预览，但明确最终金额、rate 和舍入以后端响应为准。
3. 手续费选择账户时只展示合法候选，并显示实际扣费币种。
4. 聚合详情同时展示源账户、目标账户、双边金额、rate、手续费和关联流水。
5. 聚合更正同时展示旧值与替代值；聚合软删除显示所有受影响账户和 Adjustment，并要求明确确认。

验收：

- 三种 mode、同/跨币种和三种手续费来源真实 E2E 通过。
- Transfer 始终不进入收入或支出，负手续费 Adjustment 进入支出。
- 幂等重试只产生一个 transfer group；失败不留下单腿或孤立手续费。
- 原子更正失败时旧聚合仍然有效，成功时新旧关联可追溯。
- 聚合软删除后两端余额、缓存、报表、审计和事件保持一致。

### 7.9 P2-S09 Dashboard、报表与导出

目标：提供后端权威计算、前端可解释呈现的分析体验。

后端顺序：

1. 保留现有 Dashboard、Net Worth 和 Cash Flow DTO 兼容语义。
2. 新增净资产趋势，以及 root 分类、账户和标签维度查询。
3. 为报表响应定义估值时刻、基准币种和可证明的汇率状态；不可用时返回稳定错误，不返回部分总计。
4. 评估 Application 聚合、SQL read model、索引或物化策略，以真实基准决定优化，不改变金融语义。
5. 实现与流水筛选一致的服务端 CSV 流式导出，使用用户时区和业务 magnitude。
6. CSV 使用 RFC 4180、UTF-8；文本字段防御以 `=`、`+`、`-`、`@` 开头的公式注入，金额字段按已验证的 Decimal string 原样导出；设置行数和日期范围上限。
7. 对 Transfer 排除、signed Adjustment、历史汇率、DST、root 分类和两用户隔离补测试。

前端顺序：

1. Dashboard 展示净资产、当月收入/支出、账户分布和 Top 支出分类。
2. Reports 使用 URL 保存周期、维度和筛选，切换基准币种后重新请求服务端。
3. ECharts 只负责视觉映射，Tooltip 和可访问表格保留后端字符串值。
4. 明确区分 loading、empty、current、historical fallback、unavailable 和 error 状态。
5. 导出使用后端响应文件名和内容，不从当前分页表格拼 CSV。

验收：

- 报表数值与 PostgreSQL fixture 的权威结果一致，前端没有二次金融聚合。
- Transfer、Adjustment、历史汇率、用户时区和 DST 场景 E2E 通过。
- 缺失汇率时不会显示旧缓存或貌似完整的图表。
- 参考数据集的 Dashboard、分页报表和 CSV 达到 P2-S01 固定的预算。

### 7.10 P2-S10 审计、维护与运行可见性

目标：让用户可以解释自己的高风险操作，让运维方可以定位后台异常。

用户维护面：

1. 添加当前用户审计日志分页 Query，认证用户 ID 是不可覆盖的查询条件，仅允许 action、resource 和时间筛选；用户面只返回 `actor_type=user` 且 `operator_user_id` 为当前用户的事实。
2. 对 before/after/metadata 使用批准字段白名单，避免返回 hash、Token、Provider 数据或内部 SQL。
3. 添加单账户和当前用户全部账户的余额缓存重建用例；重建校验 `source_version` 并记录审计。
4. 维护页面显示动作、时间、资源、结果和 TraceId，不展示数据库内部结构。

运维面：

1. 增加持久化 `USER` / `OPERATOR` 角色来源和 Operator 授权 Filter；签发 JWT 前从服务端事实加载角色，公共注册永远不能授予 Operator。
2. 增加数据最小化的 liveness/readiness 供编排器探测；探针不依赖用户 JWT，由部署网络限制访问。readiness 检查 request DB、必要后台组件和 migration 版本，响应保持脱敏。
3. 增加 Scheduler 最近运行结果、lease、Outbox pending/retry/dead-letter 数量和 Handler receipt 摘要。
4. dead-letter 列表不返回 payload；受控重试使用幂等命令、并发保护和 Operator 审计。
5. 暴露适合采集的有界 metrics，并通过部署网络或独立采集认证保护；生产 Web 页面只消费 Operator 授权 API，不解析日志文本。
6. Operator UI 使用独立路由守卫；普通用户直接访问返回 403，不依赖隐藏菜单实现权限。

验收：

- 两用户审计完全隔离，余额重建不改变权威余额。
- `USER` 无法访问任何 operations API；客户端伪造 JWT role 无效。
- dead-letter 重试不会重复副作用，状态转换和审计可追踪。
- readiness 能区分未就绪与存活，不泄露角色名、连接串、事件 payload 或密钥。
- Scheduler/Outbox 正常、重试、dead letter 和恢复场景在真实 PostgreSQL 上通过。

### 7.11 P2-S11 产品硬化与 Release Candidate

目标：冻结功能后完成跨功能质量、性能和安全收敛。

开发内容：

1. 完成 Desktop、Tablet 和 Mobile 响应式检查，覆盖常见短屏、窄屏和长文本。
2. 执行 WCAG 2.2 AA 自动与人工检查：键盘、焦点、名称、错误关联、对比度和 reduced motion。
3. 完成 `zh-CN` / `en-US`、IANA 时区、DST、长货币符号和超长分类/账户名检查。
4. 建立路由 chunk 和静态资源预算，优化 ECharts/Element Plus 按需加载与长列表渲染。
5. 在 10,000 / 100,000 条参考数据下记录 API p50/p95、页面加载、筛选和导出结果。
6. 执行 XSS、CSRF、Clickjacking、CSP、CSV 注入、会话固定、越权和重复提交测试。
7. 执行数据库超时、Provider 不可用、refresh 失败、Outbox 积压和进程重启故障注入。
8. 检查前后端依赖锁定、许可证、已知漏洞、秘密模式和 production source map 策略。
9. 冻结 Release Candidate commit，后续只接受可复现阻断缺陷修复。

验收：

- unit/component、API contract、Playwright Chromium/Firefox/WebKit 和 Accessibility 门禁通过。
- Phase 1 后端回归、Phase 2 新测试和 OpenAPI drift check 全部通过。
- 性能预算达标；不能达标的项目必须有测量依据、用户影响和明确处理结论。
- 浏览器存储、构建产物、日志和 source map 中没有 Token、Cookie、密码或私有配置。

### 7.12 P2-S12 跨平台交付与 Phase 签署

目标：在目标环境证明 Phase 2 是完整产品，而不是仅在开发服务器可运行。

执行顺序：

1. Windows 或主开发机执行前后端 Debug/Release、完整 unit/component 和 Chromium E2E。
2. Linux 执行 production ON Debug/Release、完整 CTest、前端 clean install/build 和三浏览器 E2E。
3. PostgreSQL 16+ 空库执行全部 migration、validate、第二次 no-op、Repository fixture 和 RLS 回归。
4. 从干净 checkout 冷构建 Web Edge 与 Backend Docker 镜像，确认同源代理、health、non-root 和只读静态文件系统。
5. 复跑认证、账户、流水、更正、转账、报表、导出、维护和 Operator 最小生产闭环。
6. 验证 CSP、Cookie、TraceId、日志脱敏、Provider 主备、Outbox/Scheduler、SIGTERM 和升级重启。
7. 演练数据库备份恢复、上一镜像回滚和 migration 前向修复流程，不改写已发布 migration。
8. 更新 OpenAPI、架构、README、部署指南和 Phase 2 交付摘要，只保留最终结果与持续约束。
9. 将计划状态改为 Complete，确认全部门禁后才把 Phase 2 分支合并到 `main`。

验收：

- Web 与 API 在 Linux/Docker 同源拓扑下完成真实端到端验收。
- 所有 migration、RLS、金融、认证、Outbox、Scheduler 和 Provider 基线没有回归。
- 交付 commit、测试证据和构建产物可追溯，两个工作区无秘密和临时材料。
- 未完成能力只以明确边界记录，不使用 mock 或开发机结果替代目标环境结论。

---

## 8. 持续测试矩阵

| 门禁                              | 每个 Step              | Release Candidate | Phase 签署             |
| --------------------------------- | ---------------------- | ----------------- | ---------------------- |
| Backend Unit / Use Case           | 受影响目标             | 全量              | Windows/Linux 全量     |
| In-Memory Integration             | 受影响场景             | 全量              | 全量                   |
| PostgreSQL Integration            | 涉及 SQL/Repository 时 | 全量              | PostgreSQL 16+ 全量    |
| OpenAPI / Route / Generated Types | 必须                   | 必须              | 必须                   |
| Frontend typecheck / lint / unit  | 必须                   | 全量              | clean install 全量     |
| Component / Accessibility         | 受影响页面             | 全量              | 全量                   |
| Playwright E2E                    | 当前纵向切片           | 三浏览器全量      | 真实 Docker 拓扑全量   |
| Security / Secret Scan            | 必须                   | 全量              | 构建产物和运行日志全量 |
| Performance                       | 关键读路径             | 固定数据集        | 目标环境复核           |
| Linux production ON / Docker      | 影响生产装配时         | 必须              | 必须                   |

新增前端测试不能替代 Phase 1 的 C++、PostgreSQL、Drogon 或 Docker 测试。涉及 migration、认证、RLS、报表、Outbox、Scheduler、Provider 或生产装配的 Step，必须在合并前补对应真实环境证据。

---

## 9. Phase 2 完成定义

Phase 2 只有在以下条件全部满足后才可标记 Complete：

1. 普通用户可以从浏览器完成认证、账户、流水、转账、报表、导出和个人维护闭环。
2. 浏览器会话符合内存 Access Token + HttpOnly Refresh Cookie 边界，未发现认证材料持久化或泄漏。
3. 前端不独立实现后端金融、汇率、报表和权限规则。
4. 所有金额、汇率、月份和时区路径保持 Phase 1 正确性。
5. 高风险用户操作和 Operator 操作具备权限、确认、幂等和审计。
6. 前后端契约由 OpenAPI 和生成类型约束，路由、DTO 与实现一致。
7. Desktop/Mobile、`zh-CN` / `en-US`、Accessibility 和三浏览器 E2E 通过。
8. Windows、Linux production ON、PostgreSQL、真实 Drogon 和 Docker 门禁通过。
9. 性能、安全、依赖、秘密扫描、备份恢复和回滚检查完成。
10. 架构、API、指南和交付摘要与最终代码一致，过程性文档已清理或归档。
11. 完整加密货币定价、外部账单和支付平台仍被准确描述为未交付能力。
12. Phase 2 分支已完整签署，满足合并 `main` 的全部条件。

---

## 10. 依赖入口

- [总体开发计划](../Overall_Development_Plan.md)
- [Phase 1 开发计划](../Phase_1/Phase_1_Development_Plan.md)
- [技术架构](../../Architecture/01_Technical_Architecture.md)
- [服务与用例设计](../../Architecture/06_Service_and_Use_Case_Design.md)
- [报表与分析设计](../../Architecture/09_Reporting_and_Analytics_Design.md)
- [REST API 设计](../../Architecture/10_REST_API_Design.md)
- [OpenAPI 3.1](../../Architecture/10_REST_API_OpenAPI.json)
- [前端设计](../../Architecture/13_Frontend_Design.md)
- [错误处理设计](../../Architecture/15_Error_Handling_Design.md)
- [测试策略](../../Architecture/16_Testing_Strategy.md)
