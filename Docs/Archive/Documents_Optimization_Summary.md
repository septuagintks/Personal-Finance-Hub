# Personal Finance Hub 文档优化总结

Version: 1.0
Status: Final

---

## 1. 文档体系

PFH 文档只保留对开发与维护仍有价值的信息：现行架构规则、阶段范围、最终交付能力、验收结果、操作指南和明确的能力边界。方案讨论、临时任务、阶段性 blocker、重复示例和已被最终实现取代的过程说明通过 Git 历史追溯。

### 1.1 目录职责

| 目录 | 职责 |
| ---- | ---- |
| `Docs/Architecture/` | 当前有效的系统设计、接口和数据契约 |
| `Docs/Development_Plans/` | 总体路线与各 Phase 的计划 |
| `Docs/Guides/` | 构建、配置、迁移、测试和日常开发操作 |
| `Docs/Standards/` | 文档格式、命名和治理规则 |
| `Docs/Archive/` | 已完成阶段的开发记录与交付摘要 |

`Docs/Development/` 不再保存已完成 Phase 的任务清单。进行中的临时文档仅在确有需要时创建，并在完成后合并到对应计划、指南、架构文档或归档记录。

### 1.2 Phase 1 归档结构

Phase 1 的归档入口为：

- `Phase_1_Development_Record.md`：开发路线、最终范围、验收矩阵和能力边界。
- `Phase_1_S01-S04_Delivery_Summary.md`：工程基础、配置、日志与质量入口。
- `Phase_1_S05-S08_Delivery_Summary.md`：金融原语、领域模型、数据库与持久化。
- `Phase_1_S09-S12_Delivery_Summary.md`：应用层、API、认证、后台任务与最终跨平台签署。

### 1.3 Phase 2 归档结构

Phase 2 使用相同的结果投影规则：

- `Phase_2_Development_Record.md`：开发路线、最终产品范围、验收矩阵和能力边界。
- `Phase_2_S01-S04_Delivery_Summary.md`：工程、OpenAPI、Web Edge 与浏览器会话。
- `Phase_2_S05-S08_Delivery_Summary.md`：账户、元数据、普通流水与 Transfer 工作流。
- `Phase_2_S09-S12_Delivery_Summary.md`：分析、维护、产品硬化与最终跨平台签署。

---

## 2. 已统一的设计结论

### 2.1 架构边界

- Domain 保持纯 C++23，不依赖 Drogon、PostgreSQL、JSON 或外部 I/O。
- Domain Service 只执行纯领域规则，不访问 Repository、不打开事务、不发布事件。
- Application 负责权限、事务、Repository 编排和错误映射。
- Infrastructure 实现持久化、外部 Provider、调度与安全适配器。
- Presentation 只处理 HTTP、DTO、认证上下文和稳定错误响应。

### 2.2 金融与汇率

- 金额和汇率统一通过十进制字符串进入 `Decimal`，不使用二进制浮点承载金融值。
- 数据库存储 USD 枢纽方向汇率，交叉汇率在内存中推导。
- 默认舍入为 Half-Even；金额和汇率分别对齐 `NUMERIC(20,8)` 与 `NUMERIC(20,10)`。
- FreeCurrencyAPI 是主源，exchangerate.fun 是整批备用源；成功批次不混用来源。
- 当前实时覆盖 20 种法币与 BTC。其余 12 种加密货币没有实时汇率保证，缺失时明确返回不可用或在完整历史快照存在时降级。该限制只作为能力边界，不列为当前开发任务。

### 2.3 事务、事件与多租户

- 业务写入与 Outbox 写入共享同一个数据库事务。
- 事件只在提交后由 Outbox Publisher 领取和投递。
- 租户表使用 `user_id`、复合外键与 FORCE RLS 共同约束隔离。
- request role 与后台只读 BYPASSRLS role 分离；后台角色仅用于批准的跨租户只读查询。
- Outbox claim、定时任务 lease、重试和 dead letter 以 PostgreSQL 作为一致性基础。

### 2.4 配置与安全

- 配置优先级为环境变量、JSON、本地默认值。
- `PFH_*` 是首选变量名；兼容别名只用于平滑迁移。
- 数据库密码、JWT secret、password pepper 与 Provider key 不进入 Git。
- 未替换的 placeholder、非法端口、同名数据库角色和不满足权限约束的生产配置会在启动时失败。
- 生产错误响应不暴露 SQL、文件路径、Token、密钥或底层异常文本。

---

## 3. 文档维护规则

### 3.1 结果优先

文档默认回答以下问题：

1. 当前系统具备什么能力。
2. 哪些规则必须保持。
3. 如何构建、配置、测试或调用。
4. 哪些能力明确不在当前范围。
5. 哪份文档是该事实的唯一入口。

无需专门保留的内容包括：曾经考虑但未采用的普通方案、已经修复且没有持续约束价值的缺陷过程、重复的测试日志、临时任务编号和由后续门禁完全取代的中间基线。

### 3.2 单一事实来源

- 架构行为写入 `Docs/Architecture/`。
- 配置变量和运行方式写入 `config/README.md` 与 `Docs/Guides/`。
- Phase 范围与顺序写入 `Docs/Development_Plans/Phase_N/`。
- 已完成阶段的结果写入 `Docs/Archive/`。
- 目录结构变化同步更新 `Docs/README.md` 与 `Docs/Guides/Directory_Guidance.md`。

### 3.3 追溯方式

工作树不承担完整项目史。需要调查历史决策、临时缺陷或某次测试原始上下文时，使用 Git log、对应提交和独立交接仓库；不得把历史材料重新复制回现行文档。

完整加密货币实时定价不进入当前任务路线，只在汇率架构、Phase 计划和交付记录中声明现有 Provider 能力边界。
