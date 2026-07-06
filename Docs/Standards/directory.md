# Personal Finance Hub (PFH) 文档目录结构规范

---

```text
Docs/
├── README.md
│
├── Architecture/
│   ├── 01_Technical_Architecture.md
│   ├── 02_Database_Design.md
│   ├── 03_Domain_Model_Design.md
│   ├── 04_Money_Currency_System_Design.md
│   ├── 05_Repository_and_Persistence_Design.md
│   ├── 06_Service_and_Use_Case_Design.md
│   ├── 07_Workflow_and_Lifecycle_Design.md
│   ├── 08_Exchange_Rate_System_Design.md
│   ├── 09_Reporting_and_Analytics_Design.md
│   ├── 10_REST_API_Design.md
│   ├── 11_Sync_Framework_Design.md
│   ├── 12_Scheduler_Design.md
│   ├── 13_Frontend_Design.md
│   ├── 14_Event_Design.md
│   ├── 15_Error_Handling_Design.md
│   └── 16_Testing_Strategy.md
│
├── Development/
│   ├── DEPENDENCY_INSTALLATION.md
│   ├── PHASE_1_FOUNDATION_COMPLETE.md
│   ├── Phase_1_Progress_Report.md
│   ├── Phase_1_S01-S03_Delivery_Summary.md
│   ├── Phase_1_S04_Completion_Report.md
│   ├── QUICK_REFERENCE.md
│   └── tasks.md
│
├── Develop_Plan/
│   ├── Overall_Development_Plan.md
│   ├── Phase_1_Development_Plan.md
│   ├── Phase_1/
│   │   └── Phase_1_Detailed_Development_Plan.md
│   ├── Phase_2_Development_Plan.md
│   └── Phase_3_Development_Plan.md
│
├── Standards/
│   ├── directory.md
│   └── Documents_Format_Standard.md
│
└── Completed_Modifications/
    ├── Documents_Optimize_1.md
    ├── Documents_Optimize_2.md
    └── Documents_Optimize_3.md
```

说明：

1. 上述目录树只描述当前仓库中已经提交的文档。
2. `Docs/Development/Documents_Optimize_Plan.md` 用于正在设计中的文档优化方案，按需创建。
3. `Docs/Development/Phase_<N>_S<start>-S<end>_Delivery_Summary.md` 用于开发阶段交付记录，开发中存放在 `Docs/Development/`，验收完成后归档到 `Docs/Completed_Modifications/`。
4. `Docs/Develop_Plan/` 用于总体与阶段性开发计划，当前包含三阶段总开发计划大纲、Phase 1 细化子计划和 Phase 1、Phase 2、Phase 3 开发计划。
