# Personal Finance Hub (PFH) 架构与设计文档优化方案

Version: 1.0  
Backend: C++23  
Architecture: Clean Architecture + Lightweight DDD  
Status: Approved

---

## 1. 核心设计冲突与修正方案

### 1.1 统一 UserPreference 存储与映射设计

- **冲突点**：
  - `02_Database_Design.md` 中设计了一张独立的表 `user_preferences`，用于存储用户的偏好设置（主题、时区、语言等）。
  - `05_Repository_and_Persistence_Design.md` 中却写道：“`UserPreference` 是 Domain Concept，第一阶段由 Repository 从 `users.base_currency_code` 映射得到，不需要独立 `user_preferences` 表”。
- **修正方案**：
  - 鉴于未来扩展性，**保留 `user_preferences` 独立表的设计**。
  - 修改 `05_Repository_and_Persistence_Design.md` 中的表述，明确 `UserPreference` 拥有独立的 `user_preferences` 表，并由 `IUserRepository` 联合查询或通过独立的 `IUserPreferenceRepository` 进行读写。

### 1.2 消除 Service 命名与职责冲突

- **冲突点**：
  - `01_Technical_Architecture.md` 和 `05_Repository_and_Persistence_Design.md` 明确规定：“Application Layer 绝不能定义通用服务如 `AccountingService`、`ExchangeRateService` 或 `ReportService`”。
  - 然而，在 `04_Money_Currency_System_Design.md` 中，仍然提到了 `ExchangeRateService` 的职责；在 `08_Exchange_Rate_System_Design.md` 中，也提到了“不要定义单独的 `ExchangeRateService`，避免和应用层汇率刷新用例混淆”。
- **修正方案**：
  - 全面检索并修改所有文档，**彻底删除 `ExchangeRateService`、`AccountingService` 的表述**。
  - 统一使用 **`RefreshExchangeRatesUseCase`**（应用层用例，负责调度与 I/O）和 **`CurrencyConversionService`**（领域服务，负责纯内存折算）。

---

## 2. 核心业务逻辑与安全性完善方案

### 2.1 增强 `TransferDomainService` 的手续费扣除灵活性

- **缺失点**：在 `06_Service_and_Use_Case_Design.md` 的 `TransferDomainService::buildTransfer` 实现中，手续费 `feeAmount` 被作为独立的 `Adjustment` 流水附属于转账聚合根，且默认从源账户（出账账户）扣除。
- **完善方案**：
  - 在 `buildTransfer` 接口中，显式增加 `FeeSource` 概念：
    ```cpp
    enum class FeeSource {
        SourceAccount,  // 从源账户（出账账户）扣除
        TargetAccount,  // 从目标账户（入账账户）扣除
        ThirdParty      // 从独立的第三方账户扣除（需额外传入 feeAccountId）
    };
    ```
  - 允许在构造转账聚合根时，根据业务场景将手续费 `Adjustment` 挂载到不同的账户上。

### 2.2 强化 Post-Commit Dispatch（事务后触发）的健壮性

- **隐式隐患**：在 C++ 中，如果 `action()` 执行成功，但 `trans->commit()`（Drogon 事务提交）由于网络抖动或数据库崩溃而失败，此时 `pendingEvents_` 是否会被错误地派发？
- **完善方案**：
  - 必须确保事件派发逻辑紧跟在**数据库连接真正 Commit 成功**的物理动作之后。
  - 在 `DrogonUnitOfWork` 的底层实现中，将事件派发绑定到 Drogon 事务的 `commit` 回调中。只有当底层连接返回 `true`（提交成功）时，才调用 `eventBus_->publish()`。如果事务回滚，必须清空 `pendingEvents_` 并记录警告日志。

### 2.3 报表 CQRS 路径下的汇率换算性能优化

- **潜在瓶颈**：`09_Reporting_and_Analytics_Design.md` 规定，报表查询绕过 Domain 实体，直接执行 SQL 拿到原始币种数据，然后在内存中调用 `CurrencyConversionService` 进行汇率换算。如果用户有数万条历史流水，直接在内存中对每条流水进行三角折算会导致 CPU 密集型计算，阻塞 Drogon 的 Event Loop。
- **完善方案**：
  - 补充“大数量级流水折算方案”：
    - 对于**日/月度聚合报表**，直接在 SQL 中通过 `GROUP BY` 和 `JOIN exchange_rates` 在数据库端完成折算。
    - 对于**单笔流水明细报表**，采用延迟加载（Lazy Translation）或前端实时折算。

---

## 3. 文档与代码的 CI 双向校验机制

- **引入 Markdown Linter**：在 CI 流程中加入 `markdownlint`，强制校验标题级联、代码块语言标记、中英文空格等格式。
- **文档版本与代码版本关联**：在每个设计文档的 `Version` 字段旁，注明对应的代码 Tag 或 Git Commit Hash（例如 `Version: 1.0 (Aligns with Commit #a1b2c3d)`），确保文档不会随着代码重构而失效。
