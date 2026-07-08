# Personal Finance Hub (PFH) 文档治理优化记录

Version: 1.0
Backend: C++23
Architecture: Clean Architecture + Lightweight DDD
Status: Approved

---

## 1. 优化目标

本次优化用于收束文档治理相关事项，确保目录结构、格式规范、任务跟踪和归档记录彼此一致。

### 1.1 范围

- **目录结构对齐**：同步 `Docs/README.md`、`Docs/Guides/Directory_Guidance.md` 和实际文件结构。
- **格式规范落位**：确认 `Documents_Format_Standard.md` 作为项目文档格式与排版规范，存放在 `Docs/Standards/`。
- **任务清单优化**：将 `Docs/Development/Tasks.md` 从纯待办列表优化为包含使用规则、优先级、可验证任务项和验收口径的开发跟踪文档。
- **阶段计划创建**：创建 `Docs/Development_Plans/Phase_1_Development_Plan.md`，明确 Phase 1 的范围、里程碑和质量门禁。
- **计划归档**：将已完成的 `Documents_Optimize_Plan.md` 归档为 `Docs/Archive/Documents_Optimize_3.md`。

---

## 2. 已完成变更

### 2.1 文档目录说明修正

- `Docs/README.md` 的目录树已改为只列出当前仓库实际存在的文档。
- `Docs/Guides/Directory_Guidance.md` 已同步当前目录结构。
- `Docs/Development/Documents_Optimize_Plan.md` 已移除，不再作为进行中的计划文档保留。
- `Docs/Archive/Documents_Optimize_3.md` 已加入归档目录。
- `Docs/Development_Plans/Phase_1_Development_Plan.md` 已加入阶段性开发计划目录。

### 2.2 文档格式规范位置确认

- `Documents_Format_Standard.md` 当前位于 `Docs/Standards/`。
- 新文档或修改文档时，应遵守该规范中关于标题级联、代码块语言标记、中英文混排、目录说明和待决策项的规则。

### 2.3 任务跟踪优化

- `Docs/Development/Tasks.md` 已补充任务使用规则，明确任务 ID、完成条件和新增任务方式。
- 已增加“当前执行优先级”，便于从文档收尾进入 Phase 1 开发准备。
- 已保留原有任务 ID，避免破坏历史引用。
- 已将任务项优化为更具体的可验证交付物，并补充任务验收口径，减少“勾选完成”时的判断歧义。

---

## 3. 后续事项

### 3.1 Phase 1 开发执行

- 下一步进入工程骨架、测试入口和核心金融原语实现。
- `Docs/Development/Tasks.md` 作为开发执行跟踪入口。

### 3.2 新一轮文档优化

- 如后续出现新的文档优化工作，应重新在 `Docs/Development/` 下创建 `Documents_Optimize_Plan.md`。
- 完成并确认后，再按递增编号归档到 `Docs/Archive/`。

---

## 4. 待决策选项

当前没有必须由维护者立即选择的内容。
