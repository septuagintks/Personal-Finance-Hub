# Phase 2 S08 转账工作流交付摘要

Status: Implementation Complete（Target-Environment Validation Pending）
Branch: `feature/phase2-product-experience`

## 1. 当前结果

- Transfer 聚合列表使用 `(occurred_at DESC, group_id DESC)` keyset cursor，支持账户和半开时间窗口；列表仅返回活动聚合，详情保留当前用户的已删除历史和更正双向链接。
- V9 新增 `transfer_corrections`。更正在同一 Unit of Work 追加替代组、软删除旧组双腿与全部 Adjustment、写入关系、失效余额缓存并提交 Audit、幂等响应和 `TransferCorrected` Outbox 事件。
- 聚合删除保留 `transfer_groups` 历史事实，原子软删除全部成员并提交 Audit 与 `TransferDeleted`；普通 Transaction 删除/更正继续拒绝任何聚合成员。
- 原组和成员先锁定，旧、新及手续费账户按 ID 升序锁定；创建、更正和删除失败均不留下单腿、孤立手续费、半个关系或部分缓存状态。
- Web 端交付转账列表/详情、三种 segmented 输入模式、Decimal 派生预览、Source/Target/ThirdParty 手续费、历史更正链和聚合删除确认。
- Transfer Store 使用 AbortController 与 generation 隔离用户切换；写成功后清除流水投影并使 Dashboard/Report 聚合版本失效。

## 2. 持续规则

- Transfer 双腿和 grouped Adjustment 只能由聚合用例写入；前端不得串联普通流水操作。
- outgoing、incoming 与 fee 对外均为正数 magnitude；数据库 outgoing leg 和手续费 Adjustment 保持负数存储语义。
- 同币种仅允许 BothAmounts 且双边相等；跨币种保持三种 authoritative 输入模式，最终 rate、金额和舍入以后端响应为准。
- 更正链允许继续追加，不原地修改既有金额、账户、时间或组元数据。

## 3. 本机验证

- Windows Debug、PostgreSQL OFF：C++ 构建通过，CTest `373/373 PASS`，其中 PostgreSQL adapter compile gate 通过；Transfer API 覆盖稳定分页、账户/范围筛选、租户隔离、聚合删除、三模式、三种手续费、幂等重放、原子更正及失败回滚。
- OpenAPI、迁移和 PostgreSQL adapter 静态门禁通过；前端 Vitest/MSW `50/50 PASS`，TypeScript Strict、ESLint 与生产构建通过。
- 本机 Edge Desktop 1440x900 与 Mobile 390x844 的 S08 mock-contract E2E `2/2 PASS`；axe 零自动化违规且无横向溢出。

V9 空库/legacy upgrade、真实 PostgreSQL RLS 与并发锁、NUMERIC round-trip、Linux/Docker 和 Chromium/Firefox/WebKit 矩阵按计划保留到 P2-S12；本机 In-Memory 与 mock-contract 结果不替代目标环境验证。
