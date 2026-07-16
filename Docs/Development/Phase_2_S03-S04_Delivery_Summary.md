# Phase 2 S03-S04 前端与会话交付摘要

Status: In Progress（S03 Complete；S04 本机契约切片完成）
Branch: `feature/phase2-product-experience`
Product Name: `Candy's Ledger`（暂定）

## 1. 当前结果

- 建立 Vue 3、Vite、TypeScript Strict、Vue Router、Pinia、Element Plus、ECharts、`decimal.js` 与 `@lucide/vue` 的锁定工程基线；当前路由按页懒加载，Element Plus 与 ECharts 不进入首屏 bundle。
- 交付公开产品入口、注册/登录页、响应式应用 Shell 和 Dashboard，产品名由单一配置投影，PFH 继续作为仓库、后端与 API 名称。
- 建立 HTTPS Vite `/api` 代理、production build、non-root Nginx 静态服务和同源 API 反向代理配置。
- 建立内存 Access Token、HttpOnly Refresh Cookie、启动恢复、单页 single-flight、跨标签页 Web Locks/租约互斥、状态广播与安全请求重放边界。
- 会话失效会清除内存状态并退出受保护路由；非幂等写请求只有显式允许或携带 `Idempotency-Key` 时才会在 refresh 后重放。
- 建立 OpenAPI 生成类型、统一错误投影、十进制/时区/枚举/图表共享适配器和 MSW 契约测试。

## 2. 本机验证

- 冻结依赖安装、OpenAPI drift、ESLint 零告警、Prettier、TypeScript、production build：PASS。
- Vitest / Vue Test Utils / MSW：`13/13 PASS`，覆盖成功、401、409、422、500、离线错误投影、十进制字符串和 IANA 时区适配。
- Playwright（本机 Edge，Desktop 1440x900、Mobile 390x844）：`8/8 PASS`。
- axe：公开入口、认证页与 Dashboard 在 Desktop/Mobile 均为零自动化违规；页面无横向溢出。
- 浏览器 `localStorage`、`sessionStorage` 与 IndexedDB 不含 Access Token 或 Refresh Token。

## 3. S04 剩余验收

- 对真实 Drogon 后端执行注册、登录、reload 恢复、refresh rotation/reuse detection 与 logout E2E。
- 验证两个真实标签页并发恢复、刷新和退出不会产生错误 reuse，且不广播 Token。
- 登录后加载用户偏好与公共货币元数据，并验证用户切换时清除全部用户级缓存。
- 在真实服务故障与恢复条件下验证网络中断和明确重试体验。

Linux、Docker、三浏览器和真实后端结果仍按 Phase 2 持续测试矩阵完成，不以当前 mock contract E2E 替代。
