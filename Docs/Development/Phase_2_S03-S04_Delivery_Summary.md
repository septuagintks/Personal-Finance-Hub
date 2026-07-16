# Phase 2 S03-S04 前端与会话交付摘要

Status: Implementation Complete（S03 Complete；S04 Target-Environment Validation Pending）
Branch: `feature/phase2-product-experience`
Product Name: `Candy's Ledger`（暂定）

## 1. 当前结果

- 建立 Vue 3、Vite、TypeScript Strict、Vue Router、Pinia、Element Plus、ECharts、`decimal.js` 与 `@lucide/vue` 的锁定工程基线；当前路由按页懒加载，Element Plus 与 ECharts 不进入首屏 bundle。
- 交付公开产品入口、注册/登录页、响应式应用 Shell 和 Dashboard，产品名由单一配置投影，PFH 继续作为仓库、后端与 API 名称。
- 建立 HTTPS Vite `/api` 代理、production build、non-root Nginx 静态服务和同源 API 反向代理配置。
- 建立内存 Access Token、HttpOnly Refresh Cookie、启动恢复、单页 single-flight、跨标签页 Web Locks/租约互斥、状态广播与安全请求重放边界。
- 会话失效会清除内存状态并退出受保护路由；非幂等写请求只有显式允许或携带 `Idempotency-Key` 时才会在 refresh 后重放。
- 认证就绪前并行预载用户偏好与公共货币元数据；退出、重登、用户切换和迟到响应由 `AbortController` 与 generation 隔离，预载失败不会留下伪认证状态。
- logout 会使在途 refresh 结果失效，跨标签页只广播 `authenticated` / `anonymous` 状态，不传递 Token；尚未落地的用户默认首页稳定回退 `/dashboard`。
- 建立 OpenAPI 生成类型、统一错误投影、十进制/时区/枚举/图表共享适配器和 MSW 契约测试。

## 2. 本机验证

- 冻结依赖安装、OpenAPI drift、ESLint 零告警、Prettier、TypeScript、production build：PASS。
- Vitest / Vue Test Utils / MSW：`20/20 PASS`，覆盖成功与错误投影、十进制/时区适配、上下文原子加载、用户切换竞态、预载失败和迟到 refresh 隔离。
- Playwright（本机 Edge，Desktop 1440x900、Mobile 390x844）：`17/17 PASS`，覆盖注册、登录、reload 恢复、401 refresh 与安全 GET 重放、上下文预载失败和双标签页退出。
- axe：公开入口、认证页与 Dashboard 在 Desktop/Mobile 均为零自动化违规；页面无横向溢出。
- 浏览器 `localStorage`、`sessionStorage` 与 IndexedDB 不含 Access Token 或 Refresh Token。
- 既有 C++ API contract 已覆盖 Web Cookie rotation/reuse revocation、Origin/Fetch Metadata 拒绝、logout Cookie 清理和 Access Token 撤销。

## 3. 目标环境验收

- 对真实 Drogon 后端执行注册、登录、reload 恢复、refresh rotation/reuse detection 与 logout E2E。
- 验证两个真实标签页并发恢复、刷新和退出不会产生错误 reuse，且不广播 Token。
- 在真实服务故障与恢复条件下验证网络中断和明确重试体验。
- 在 Linux/Docker 同源拓扑执行 Chromium、Firefox 与 WebKit 全量复核。

上述结果保留到 P2-S12；当前 mock contract E2E 只证明浏览器客户端行为，不替代真实 Cookie、PostgreSQL、Drogon 或跨浏览器结论。
