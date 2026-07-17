# Phase 2 S06 分类、标签与偏好交付摘要

Status: Implementation Complete（Target-Environment Validation Pending）
Branch: `feature/phase2-product-experience`

## 1. 当前结果

- 分类 API 支持状态筛选、重命名、同 board 排序、软删除和恢复；更新不接受 board、父级、source 或 template 身份变更。
- 分类创建在事务内按 template 身份或同 board/同父级/同名身份加锁。命中软删除记录时恢复原 ID，活动重复项稳定返回 `409`。
- 标签支持状态筛选、重命名、软删除和恢复；删除后重用同名标签会恢复原 ID，历史 Transaction relation 继续保留并可解析删除状态。
- 分类与标签写路径同步记录 Audit 和强类型 Outbox 事件；PostgreSQL 与 In-Memory adapter 使用一致的 active-only 和 including-deleted 锁语义。
- 偏好覆盖基准币种、`zh-CN`/`en-US`、IANA timezone、日期/数字格式、theme、默认首页和默认报表周期；不支持的 locale/timezone 在事务前拒绝，原设置保持不变。
- Web Settings 提供分类、标签和偏好三个工作面；主题与语言保存后立即应用，聚合 revision 使 Dashboard 及后续 Reports 不复用旧基准币种结果。
- Metadata、Preference、Currency 仅保存在当前会话内存；Logout、会话失效和用户切换会中止请求并清空数据，迟到响应不能回写下一用户。

## 2. 持续规则

- 软删除是历史可解释性边界，不等同于物理删除；重新启用同一业务身份必须恢复旧实体。
- 分类 board 和层级身份不可通过重命名接口修改；跨 board 调整应新建分类并保留旧历史。
- 标签不参与金额、余额或报表金融计算。
- 偏好写入成功后必须使依赖 locale、timezone 或 base currency 的服务端投影失效，前端不自行换算旧聚合结果。

## 3. 本机验证

- Windows Debug、PostgreSQL OFF：C++ 构建与全量 CTest `369/369 PASS`。
- OpenAPI 3.1 路由、生成类型 drift、PostgreSQL adapter compile gate：PASS。
- Vitest/MSW：`40/40 PASS`。
- Playwright 本机 Edge：Desktop 1440x900 与 Mobile 390x844 的 S06 工作流 `2/2 PASS`；axe 零自动化违规，页面无横向溢出。
- ESLint、Prettier、TypeScript Strict 与 production build：PASS。

真实 Drogon/PostgreSQL 行锁、RLS pooled-connection、Linux/Docker 和 Chromium/Firefox/WebKit 复核按计划留到 P2-S12。
