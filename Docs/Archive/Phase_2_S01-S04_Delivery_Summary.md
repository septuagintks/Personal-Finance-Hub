# Phase 2 S01-S04 工程、契约与会话交付摘要

Version: 1.0
Status: Complete

---

## 1. 交付范围

P2-S01 至 P2-S04 建立 Web 产品基础、前后端契约、浏览器会话和认证闭环。

| Step | 交付内容 |
| ---- | -------- |
| S01 | 技术栈、目录、威胁模型、性能预算和质量门禁 |
| S02 | OpenAPI、Web Cookie 会话、分页、错误和幂等基础 |
| S03 | Vue 应用、API Client、应用 Shell 和同源 Web Edge |
| S04 | 注册、登录、恢复、刷新、退出和多标签页会话 |

---

## 2. 工程与契约

### 2.1 前端工程

- Vue 3、Vite、TypeScript Strict、Vue Router、Pinia 和 pnpm lockfile。
- `frontend/src/generated/api-types.ts` 由 OpenAPI 生成，drift check 禁止手工 DTO 漂移。
- Vitest/MSW 覆盖 Store、Service 和组件；Playwright/axe 覆盖真实用户工作流与 Accessibility。
- 路由按需加载，ECharts 不进入初始入口；bundle、许可证、source map 和 secret 门禁进入发布流程。

### 2.2 产品入口

- PFH 保持仓库、后端和 API 工程名。
- 暂定产品名 `Candy's Ledger` 集中定义，并用于公开入口、认证页、浏览器标题和应用 Shell。
- 登录后的主入口是实际 Dashboard；用户偏好的默认首页可以选择 Dashboard、流水、报表或账户。

### 2.3 API 公共边界

- OpenAPI 3.1 固定 operationId、DTO、错误、认证和响应头。
- 列表使用稳定 cursor、明确 page size 和非法 cursor 错误。
- 结构化错误提供稳定 code、TraceId、可重试语义和受控字段错误。
- 金融写请求使用持久化幂等键，同键同请求重放原结果，同键异请求返回冲突。

---

## 3. 浏览器会话

### 3.1 Token 边界

- Access Token 只在前端内存中存在，不写入 `localStorage`、`sessionStorage` 或 IndexedDB。
- Refresh Token 由 `HttpOnly`、`Secure`、`SameSite=Strict` Cookie 承载，JavaScript 不可读取。
- Web 前端只使用 `/api/v1/web/auth/*`；非浏览器 JSON Token API 保持兼容但不供 Web 使用。

### 3.2 生命周期

- 应用启动执行一次静默恢复，并在认证状态稳定后加载偏好和公共货币元数据。
- 同标签页并发 401 复用一个 refresh；跨标签页使用不含秘密的互斥与状态广播串行 rotation。
- Logout 与 refresh 共享会话串行边界；reuse detection 撤销完整 session。
- `AbortController` 与 generation 阻止退出、重登或用户切换后的迟到响应污染新会话。

### 3.3 同源安全

- Cookie 端点校验 Origin、Fetch Metadata、公开 Host 和精确 `http` / `https` scheme。
- 外部 TLS 终止只保留受信代理传入的精确 `https`，非法值回落并被 Origin 校验拒绝。
- 认证响应使用 `Cache-Control: no-store`；Token、Cookie、密码和认证正文不进入日志。

---

## 4. 部署基础

- 开发环境由 Vite HTTPS 同源代理 `/api`。
- 生产环境由 non-root Nginx 提供静态资源并反向代理 Backend。
- Backend 只在 Compose 内网暴露端口；Web 是唯一宿主入口。
- Web/Backend 使用只读根文件系统、`cap_drop=ALL` 和 `no-new-privileges`。
- CSP、frame denial、MIME sniffing denial、Referrer Policy、Permissions Policy 和 COOP 由 Web Edge 统一设置。

---

## 5. 验收结论

- OpenAPI 生成类型、路由和 DTO drift 门禁通过。
- Web 注册、登录、refresh rotation/reuse、退出和同源拒绝在真实 Drogon/PostgreSQL 拓扑通过。
- Vitest/MSW 最终 63/63，Windows Edge 37/37，Chromium/Firefox/WebKit 111/111。
- clean install、TypeScript、ESLint、Prettier、production build 和发布安全门禁通过。
- 会话材料未进入浏览器持久化、构建产物、运行日志或仓库。

后续业务工作流见 [S05-S08 交付摘要](Phase_2_S05-S08_Delivery_Summary.md)。
