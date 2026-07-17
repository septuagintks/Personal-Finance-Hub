# Phase 2 S07 流水工作流交付摘要

Status: Implementation Complete（Target-Environment Validation Pending）
Branch: `feature/phase2-product-experience`

## 1. 当前结果

- 流水列表使用 `(occurred_at DESC, id DESC)` keyset cursor，支持账户、类型、分类、标签、半开时间窗口与 description 关键字组合筛选；page size 限制为 1-200，显式时间窗口最长 366 天。
- 列表和详情返回后端权威金额口径、Transfer group、历史分类名、历史标签及更正双向链接；软删除分类和标签不破坏旧流水可读性。
- V8 新增 `transaction_corrections`。普通更正在同一 Unit of Work 创建替代流水、软删除原流水、写入关系、替换标签、失效相关余额缓存并提交 Audit、幂等响应与 `TransactionCorrected` Outbox 事件。
- 普通删除和更正均拒绝 Transfer leg 与 grouped Adjustment；更正失败会回滚替代流水、原流水删除、关系、标签、审计、幂等记录和事件。
- 普通流水创建现在可在同一事务绑定标签；创建和更正都锁定单次前端意图并携带稳定 `Idempotency-Key`。
- Web 端交付 URL 驱动筛选、桌面表格/移动列表、游标加载、Income/Expense/Adjustment 创建、详情、历史更正链、软删除及账户/分类/标签选择。
- Transaction Store 使用 AbortController 与 generation 隔离列表、详情和写操作；logout 或用户切换会清除流水状态，写成功后统一使 Dashboard/Report 聚合版本失效。

## 2. 持续规则

- Transaction 保持 append-only；更正不允许 UPDATE 原金额、账户、分类或业务时间。
- Income/Expense 对外返回正 magnitude，Adjustment 保持 signed，Transfer 通过方向金额和 `transferGroupId` 明确表达。
- cursor 是不透明定位值，Repository 只接收解码后的时间和 ID；分页 SQL 使用参数绑定，关键字按字面子串匹配。
- 详情可读取当前用户的已删除流水；列表只返回活动流水。跨用户资源统一表现为 `404`。
- Transfer 的聚合级删除和更正属于 P2-S08，前端不得串联普通流水操作模拟。

## 3. 本机验证

- Windows Debug、PostgreSQL OFF：C++ 构建通过，CTest `371/371 PASS`，其中 PostgreSQL adapter compile gate、迁移/OpenAPI/SQL 静态门禁均通过。
- S07 API 覆盖稳定分页、组合筛选、历史元数据、租户隔离、范围/cursor 边界、原子更正、幂等重放、失败回滚和 Transfer 成员拒绝。
- Vitest/MSW `45/45 PASS`；TypeScript Strict、ESLint、Prettier 与 production build 通过。
- 本机 Edge Desktop 1440x900 与 Mobile 390x844 的 S07 mock-contract E2E `2/2 PASS`；axe 零自动化违规且无横向溢出。

V8 空库/legacy upgrade、真实 PostgreSQL keyset 查询、RLS、并发锁、NUMERIC round-trip、100,000 行基准和多浏览器矩阵按计划保留到 P2-S12；本机 In-Memory 与 mock-contract 结果不替代目标环境验证。
