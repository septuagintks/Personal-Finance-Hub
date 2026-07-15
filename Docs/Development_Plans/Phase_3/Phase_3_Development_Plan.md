# Personal Finance Hub Phase 3 开发计划

Version: 2.0
Status: Reserved

---

## 1. 阶段定位

Phase 3 承接外部账单、银行和支付平台生态。该阶段尚未排期，本文只定义稳定边界和进入条件，不承诺具体 Provider 或发布日期。

### 1.1 候选范围

- CSV、Excel、PDF、邮件、银行与信用卡账单导入。
- 支付平台和开放银行只读同步。
- Provider 插件、授权、调度、重试和限流。
- 字段映射、商户标准化、币种/时间解析。
- 幂等指纹、重复检测、冲突处理、预览与确认。
- 规则分类、订阅识别、异常提醒和现金流预测。

### 1.2 明确不做

- 未经用户确认直接写入真实支付交易。
- 绕过 Phase 1 Domain 和 Application 直接落库。
- 以 Provider 原始浮点或未校验金额进入财务模型。
- 将单一外部平台耦合到核心领域类型。

---

## 2. 进入条件

Phase 3 开始前必须满足：

1. Phase 2 的核心前端、用户确认和维护体验完成。
2. 审计、后台任务可见性、告警和失败恢复可用。
3. 外部数据授权、隐私和合规边界明确。
4. 导入预览、回滚和数据修正路径明确。
5. 目标 Provider 的 API 稳定性、速率限制和成本经过评估。

---

## 3. 推荐工作流

### 3.1 P3-S01 导入内核

- 定义 provider-neutral import DTO。
- 建立 staging、validation、preview、confirm 状态机。
- 固定 user-scoped 幂等指纹和重复检测。
- 实现 CSV 作为首个可重复测试的导入格式。

### 3.2 P3-S02 文件账单

- Excel 字段映射。
- PDF/OCR 或邮件解析适配器。
- 日期、金额、币种、商户和备注规范化。
- 无法确定的数据停留在预览状态。

### 3.3 P3-S03 平台同步

- Provider 授权和凭据隔离。
- 只读增量同步、游标、重试与速率限制。
- 外部账户到内部账户映射。
- 撤销授权与数据来源追踪。

### 3.4 P3-S04 自动化

- 可解释的分类规则。
- 商户别名和用户修正规则。
- 订阅、异常交易和预算提醒。
- 预测结果与真实账务事实严格区分。

---

## 4. 一致性规则

- 外部数据先进入 staging，不直接创建 Domain Transaction。
- 用户确认后由 Application Use Case 在 Unit of Work 中写入。
- 幂等键至少包含 `user_id + provider + external_account + external_transaction_id` 或等价稳定指纹。
- 原始 Provider payload 不作为核心领域模型；必要时加密、限期保存并受访问控制。
- 同步失败不影响本地记账、认证和报表。
- Provider 时间、金额和汇率必须经过当前严格边界解析。

---

## 5. 质量门禁

- 每个 Provider 使用脱敏 fixture 与 contract test。
- 重复导入、重试和进程重启不产生重复业务事实。
- 两用户的授权、staging、映射和导入结果完全隔离。
- 部分失败不会提交半个账单批次。
- 预览与最终写入的差异可解释并可审计。
- 真实外部测试不保存凭据、Token、完整响应或个人账单。

---

## 6. 完成定义

Phase 3 只有在至少一个文件导入和一个只读平台同步完成真实端到端验收后，才可从 Reserved 改为 Active/Complete。未进入实现前，本计划不应扩展为具体平台任务清单。

---

## 7. 依赖入口

- [总体开发计划](../Overall_Development_Plan.md)
- [同步框架设计](../../Architecture/11_Sync_Framework_Design.md)
- [事件设计](../../Architecture/14_Event_Design.md)
- [错误处理设计](../../Architecture/15_Error_Handling_Design.md)
