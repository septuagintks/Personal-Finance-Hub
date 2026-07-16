# Phase 2 S05 账户生命周期交付摘要

Status: Implementation Complete（Target-Environment Validation Pending）
Branch: `feature/phase2-product-experience`

## 1. 当前结果

- 账户列表支持 active、archived 和 all 状态，账户详情返回完整生命周期字段与强版本 ETag，余额继续由独立快照接口提供。
- 创建支持可选 Asset/Liability 分类覆盖；修改使用 `If-Match` 乐观锁。账户仅在从未存在任何流水时可修改币种，软删除历史同样会冻结币种。
- 归档与恢复均执行所有权、状态和版本检查，并在同一 Unit of Work 写入 Account、Audit 与 Outbox；归档账户继续保留历史，但不能参与新流水或转账。
- PostgreSQL 账户行锁竞争稳定映射为 `409 Conflict`，其他数据库异常继续脱敏为基础设施错误。
- Web 端交付账户列表、状态与类型/币种筛选、创建、详情、余额、编辑、归档、恢复和危险删除；危险删除展示依赖影响，执行三阶段确认并校验账户名。
- Account Store 使用 AbortController 与 generation 隔离列表、详情、余额和写操作；Logout、session 撤销和用户切换会清除账户数据，迟到响应不能重新写回。

## 2. 持续规则

- Account DTO 与 Balance DTO 保持分离，前端不从账户实体推测余额。
- 所有修改、归档和恢复请求使用详情响应的强 ETag；`409` 保留用户输入并提供重新加载路径。
- 前端不提交初始余额，不复制币种冻结、所有权、归档写入禁令或危险删除级联规则。
- 危险删除的 UI 三阶段门槛不替代服务端 `confirmations=3`、租户归属、事务与审计约束。

## 3. 本机验证

- Windows Debug、PostgreSQL OFF：C++ 构建与全量 CTest 通过。
- OpenAPI JSON、Python 契约、PostgreSQL adapter structural contract 与生成类型 drift：PASS。
- Vitest/MSW：`28/28 PASS`。
- Playwright 本机 Edge：Desktop 1440x900 与 Mobile 390x844 账户切片 `4/4 PASS`；axe 零自动化违规，页面无横向溢出。
- ESLint 零告警、Prettier、TypeScript Strict 与 production build：PASS。

真实 Drogon/PostgreSQL 账户 fixture、RLS 连接复用、Linux/Docker 和 Chromium/Firefox/WebKit 复核按计划保留到 P2-S12；本机 mock-contract 浏览器结果不替代目标环境结论。
