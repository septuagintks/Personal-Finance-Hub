# Personal Finance Hub 文档目录指南

Version: 2.0
Status: Active

---

## 1. 目录职责

| 目录 | 内容 | 生命周期 |
| ---- | ---- | -------- |
| `Architecture/` | 当前系统设计与契约 | 随实现持续维护 |
| `Development_Plans/` | 总路线和各 Phase 计划 | Phase 完成后保留最终范围 |
| `Guides/` | 构建、配置、迁移和测试操作 | 只保留当前可执行方法 |
| `Standards/` | 格式、命名和治理规则 | 稳定维护 |
| `Archive/` | 已完成阶段的结果投影 | 不追加过程日志 |

当前不设置长期 `Development/` 任务目录。进行中的任务优先由对应 Phase 计划跟踪；需要临时文档时可在 `Docs/Development/` 创建，并在完成后合并到正式文档或删除。

---

## 2. Development Plans

每个 Phase 使用独立子目录：

```text
Development_Plans/
├── Overall_Development_Plan.md
├── Phase_1/Phase_1_Development_Plan.md
├── Phase_2/Phase_2_Development_Plan.md
└── Phase_3/Phase_3_Development_Plan.md
```

计划只描述目标、范围、执行顺序、质量门禁和能力边界。普通实现过程、临时 blocker 和逐次 review 不进入计划。

---

## 3. Archive

一个完成 Phase 默认保留：

1. 一份 `Phase_N_Development_Record.md`。
2. 按自然模块划分的少量 `Phase_N_Sxx-Syy_Delivery_Summary.md`。
3. 必要的跨阶段治理总结。

交付摘要应保持体量接近，并只记录：

- 最终交付能力。
- 持续有效的架构规则。
- 最终验收矩阵。
- 明确的能力边界。
- 指向现行设计和指南的链接。

不单独归档普通 bug、一次性测试报告、临时任务清单或已经被最终门禁覆盖的中间结果。完整历史由 Git 提供。

---

## 4. 命名

- 架构文档：两位数字 + PascalCase 单词，例如 `08_Exchange_Rate_System_Design.md`。
- 开发计划：`Phase_N_Development_Plan.md`。
- 开发记录：`Phase_N_Development_Record.md`。
- 交付摘要：`Phase_N_Sxx-Syy_Delivery_Summary.md`。
- 指南和规范：PascalCase 单词以下划线连接。

命名和目录树必须描述实际文件，不列规划中尚未创建的文档。

---

## 5. 更新检查

目录或文件名变化时同步检查：

- `Docs/README.md`。
- 本文件。
- 根 `README.md` 的文档入口。
- Markdown 相对链接。
- 计划、指南和归档中的单一事实来源。
