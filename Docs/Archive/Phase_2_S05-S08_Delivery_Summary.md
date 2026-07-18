# Phase 2 S05-S08 日常记账工作流交付摘要

Version: 1.0
Status: Complete

---

## 1. 交付范围

P2-S05 至 P2-S08 完成账户、记账元数据、普通流水和 Transfer 聚合的日常用户闭环。

| Step | 交付内容 |
| ---- | -------- |
| S05 | 账户详情、修改、归档、恢复和危险删除 |
| S06 | 分类、标签、偏好和共享记账元数据 |
| S07 | 普通流水查询、创建、更正和软删除 |
| S08 | Transfer 聚合查询、创建、更正和软删除 |

---

## 2. 账户与元数据

### 2.1 账户生命周期

- 账户列表支持 active、archived 和 all，详情返回余额、版本和归档状态。
- 元数据修改使用强 ETag/版本保护；存在流水后币种不可变。
- 归档账户不能新增流水或参与 Transfer，历史仍可查询；恢复使用独立命令。
- 危险删除要求三阶段确认并在同一事务清理关联事实、Audit 和 Outbox。

### 2.2 分类与标签

- 分类支持同 board 排序、重命名、软删除和恢复；board、父级、source 和 template 身份不可通过更新改变。
- 标签支持重命名、软删除和恢复；历史 Transaction relation 保留可解释名称和删除状态。
- 重建同一业务身份时恢复原 ID，不创建破坏历史解释的新实体。

### 2.3 用户偏好

- 偏好覆盖基准币种、`zh-CN` / `en-US`、IANA timezone、日期/数字格式、theme、默认首页和默认报表周期。
- locale、timezone 或 base currency 变更使相关投影失效；前端不自行换算旧结果。
- 用户切换、退出和会话失效会清除账户、元数据和偏好缓存。

---

## 3. 普通流水

- 列表按 `(occurred_at DESC, id DESC)` keyset cursor 排序，支持账户、类型、分类、标签、半开时间窗口和 description 组合筛选。
- 详情保留历史分类、标签、Transfer group 和更正双向链接；跨用户资源统一返回 404。
- V8 建立追加式更正关系。更正在一个 Unit of Work 中创建替代流水、软删除原流水、替换标签并登记 Audit、幂等结果和 Outbox。
- 普通删除和更正拒绝 Transfer leg 与 grouped Adjustment；失败不留下替代事实或部分缓存状态。
- Income/Expense 对外使用正 magnitude，Adjustment 保持 signed 语义。

---

## 4. Transfer 聚合

- 列表按 `(occurred_at DESC, group_id DESC)` keyset cursor 排序，详情保留已删除聚合和更正链。
- 创建支持 SourceAmount+Rate、TargetAmount+Rate、SourceAmount+TargetAmount 三种 authoritative 模式。
- 手续费支持 Source、Target 和 ThirdParty 账户，并作为同组负 Adjustment 持久化。
- V9 建立聚合级追加式更正关系；旧组、成员和相关账户按确定顺序锁定。
- 聚合删除原子软删除两腿与全部 Adjustment，并登记 Audit 和 Outbox；普通流水端点不能拆腿修改。

---

## 5. 一致性规则

- 写入、幂等响应、Audit、Outbox 和余额缓存失效共享同一个 Unit of Work。
- 前端 Decimal 计算只用于输入预览，最终金额、汇率和舍入以后端响应为准。
- Store 使用 `AbortController` 与 generation 隔离筛选、详情、写操作和用户切换。
- 成功写入统一失效流水、Dashboard 和 Report 投影，不在前端保留旧金融总计。
- V7-V9 租户表使用复合约束和 FORCE RLS；request role 连接复用后不会残留前一用户上下文。

---

## 6. 验收结论

- 账户、分类、标签、偏好、流水和 Transfer 的 Unit/API/真实 PostgreSQL 场景通过。
- V7-V9 空库迁移、legacy upgrade、RLS、锁冲突、NUMERIC round-trip 和失败回滚通过。
- Desktop/Mobile 工作流、三浏览器、axe、焦点和无页面级横向溢出门禁通过。
- 流水宽表区域可由 Tab 聚焦、显示焦点环，并在 Chromium/Firefox/WebKit 使用左右方向键横向滚动。
- Daily 10,000 与 Stress 100,000 数据集证明列表和组合筛选满足预算。

后续分析、维护和最终交付结果见 [S09-S12 交付摘要](Phase_2_S09-S12_Delivery_Summary.md)。
