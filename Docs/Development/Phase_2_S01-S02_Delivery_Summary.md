# Phase 2 S01-S02 交付摘要

Status: Complete
Branch: `feature/phase2-product-experience`
Product Name: `Candy's Ledger`（暂定）

## 1. 交付范围

- 固定 Node.js `24.15.0`、pnpm `10.14.0`、Vue 3 + TypeScript Strict 的前端工程边界。
- 将 OpenAPI 升级为 2.0 契约，所有 30 个 operation 具有唯一 `operationId`，并生成 `frontend/src/generated/api-types.ts`。
- 新增 `/api/v1/web/auth/*` 同源 Web 会话：Access Token 仅由前端内存持有，Refresh Token 使用 `HttpOnly`、`Secure`、`SameSite=Strict` Cookie。
- 为 Transaction、Transfer 写入建立租户内持久化幂等记录；同键重放原结果，指纹冲突返回 `409`，业务事实、Outbox 和响应快照同事务提交。
- 统一错误响应增加 `retryable` 与受控 `field_errors`；增加强 `If-Match` 版本 ETag 解析规则和游标分页公共 DTO。
- V7 迁移新增 `request_idempotency`，启用 FORCE RLS；容器角色初始化断言同步覆盖 9 张租户 RLS 表。

## 2. 持续约束

- 浏览器不得持久化 Access Token、Refresh Token、密码或 Cookie 内容。
- Web Cookie 写请求必须通过同源 `Origin` / Fetch Metadata 校验。
- 客户端主动重试同一金融写意图时复用原 `Idempotency-Key`，不得自动重试非幂等写请求。
- 金额、汇率和响应中的业务金额继续使用十进制字符串；完整加密货币定价不在范围内。

## 3. 验证结果

- Windows Debug/Release、PostgreSQL OFF：全量 C++/API/集成/静态测试 `358/358 PASS`，其中包含幂等、Web Cookie、结构化错误和 ETag 覆盖。
- OpenAPI Python 契约门禁：PASS。
- OpenAPI TypeScript 生成与 drift check：PASS。
- PostgreSQL adapter structural contracts：PASS。
- `git diff --check`：PASS。

Linux production ON、真实 PostgreSQL 和 Docker 结果仍按 Phase 2 交付矩阵在目标环境复核，不以本机 PostgreSQL OFF 结果替代。
