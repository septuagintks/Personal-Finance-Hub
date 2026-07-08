# Personal Finance Hub - Documents Optimize 1

Version: 1.0  
Backend: C++23  
Architecture: Clean Architecture + Lightweight DDD  
Status: Approved

---

## 1. 建议完善的关键设计空白

### 1.1 安全性与身份认证细节 (Security & Authentication) ✅ 已完成

已在以下文档中明确规约：

- [02_Database_Design.md § 3.1](../Architecture/02_Database_Design.md) - 密码存储规范（Argon2id / bcrypt）
- [08_REST_API_Design.md § 6](../Architecture/10_REST_API_Design.md) - JWT + Refresh Token 完整机制
  - Access Token 结构与有效期（15 min）
  - Refresh Token 轮换与泄漏检测
  - Redis 黑名单机制（`jti` 和 `sid` 撤销）
  - JwtFilter 规则与错误响应
  - 密钥配置规范（环境变量注入）
  - 认证接口设计（login / refresh / logout）

### 1.2 多币种与三角汇率折算 (Triangular Exchange Rate Conversion) ✅ 已完成

已在 [08_Exchange_Rate_System_Design.md](../Architecture/08_Exchange_Rate_System_Design.md) 中明确规约：

- **§ 2.3** - 货币转换领域服务与三角折算
  - 以 USD 为固定枢纽货币（Pivot Currency）
  - 汇率拉取策略：仅拉取系统支持货币与 USD 的汇率对
  - 三角折算公式与实现（`CurrencyConversionService::calculateCrossRate`）
  - 便捷方法：`findOrCalculateRate`（直接查询 > 逆向 > 三角折算）
  - 数据库存储策略：只存 N-1 条 USD 汇率对，交叉汇率运行时计算
- **§ 3.2** - 汇率查询与降级策略
  - 优先级：直接汇率 > 逆向汇率 > 三角折算 > 历史汇率降级
  - 外部 API 失败时自动使用数据库最新历史汇率
  - 告警触发机制（连续失败 3 次、历史汇率超 24 小时）
- **§ 4.2** - 外部提供方实现
  - 只拉取系统 `currencies` 表中 `is_enabled = true` 的货币
  - 所有汇率以 USD 为 `base_currency_code` 存储

### 1.3 初始数据的国际化边界 (I18n Initialization Seeding) ✅ 已完成

已在 [07_Workflow_and_Lifecycle_Design.md § 7](../Architecture/07_Workflow_and_Lifecycle_Design.md) 中明确规约：

- **语言检测优先级**：前端显式传递 > Accept-Language Header > IP 地理位置 > 系统默认
- **分类模板多语言存储**：
  - `system_category_templates` 新增 `locale` 字段
  - 同一分类支持多语言版本（zh-CN, en-US, ja-JP 等）
  - 唯一约束修改为：`(locale, group_name, parent_id, name)`
- **注册流程时序**：
  - 从 DTO 或 HTTP Header 解析用户首选语言
  - 根据语言拉取对应模板初始化分类
  - 自动推断 timezone（如 en-US → America/New_York）
- **语言回退机制**：用户请求语言 → 语言主类别 → en-US → zh-CN
- **支持语言**：Phase 1 (zh-CN, en-US)，Phase 2 (ja-JP, zh-TW, ko-KR, es-ES)

### 1.4 Drogon 框架下的全局异常拦截器 (Global Exception Boundary) ✅ 已完成

已在 [15_Error_Handling_Design.md § 7](../Architecture/15_Error_Handling_Design.md) 中明确规约：

- **§ 7.1** - 设计目标（统一兜底、安全响应、可追溯、标准格式）
- **§ 7.2** - Drogon 全局异常处理器配置
  - `drogon::app().setExceptionHandler` 注册
  - 生成唯一 TraceId
  - 记录服务端日志（包含完整异常信息）
  - 返回安全的 500 响应（不泄露敏感信息）
- **§ 7.3** - TraceId 生成策略（UUID v4 或 时间戳+随机数）
- **§ 7.4** - 分层异常处理策略
  - Infrastructure 层捕获异常转换为 `std::expected`
  - Application 层不抛异常，通过 `std::expected` 返回错误
  - Presentation 层处理预期错误，全局处理器兜底非预期异常
- **§ 7.5** - 生产环境安全规约
  - 绝不返回给前端：堆栈跟踪、SQL、文件路径、内存地址、密钥
  - 必须记录到服务端日志：完整异常、TraceId、用户 ID、堆栈跟踪
  - 监控告警：异常频率超阈值自动告警

### 1.5 数据库版本演进与迁移机制 (Database Schema Migration) ✅ 已完成

已在 [02_Database_Design.md § 23](../Architecture/02_Database_Design.md) 中明确规约：

- **§ 23.1** - 迁移工具选型：推荐 Flyway（语言无关、版本化、状态追踪、幂等性）
- **§ 23.2** - Flyway 集成方式
  - 方案 A：独立 CLI 工具（推荐，适合生产）
  - 方案 B：嵌入式集成（适合开发环境）
- **§ 23.3** - 迁移脚本组织结构
  - 版本化迁移：`V<版本号>__<描述>.sql`
  - 可重复迁移：`R__<描述>.sql`
  - Undo 迁移：`U<版本号>__<描述>.sql`
- **§ 23.4** - 迁移脚本示例（V1 初始架构、V2 用户偏好、V3 Refresh Token）
- **§ 23.5** - 迁移执行流程（开发环境、CI/CD、生产环境 Kubernetes Init Container）
- **§ 23.6** - 迁移最佳实践
  - 向后兼容、幂等性、事务性、测试、文档化
- **§ 23.7** - 回滚策略（迁移失败自动回滚、应用回滚手动 Undo、数据错误补偿脚本）

---

## 2. 细节设计优化与规范补充

### 2.1 外部导入对账的幂等指纹 (Idempotency Fingerprint) ✅ 已完成

已在 [11_Sync_Framework_Design.md § 3.1](../Architecture/11_Sync_Framework_Design.md) 中明确规约：

- **无唯一 ID 账单的去重**：许多银行导出的 PDF/CSV 账单并不包含全局唯一的交易流水号。
- **合成唯一键（Fingerprint Hash）**：通过合成哈希 `Hash(provider + account_id + transaction_time + amount + merchant_name)` 生成一个幂等指纹 Key，使用 SHA-256 算法，以防止用户多次上传同一份 CSV 时产生重复流水。
- **C++ 实现示例**：提供了 `IdempotencyFingerprint` 辅助类，对商户名进行小写和去空格标准化，对金额进行 8 位小数标准化，确保哈希的确定性。

### 2.2 余额缓存的并发安全与自愈 (Concurrency & Locks on Balance Cache) ✅ 已完成

已在 [02_Database_Design.md § 9.1-9.2](../Architecture/02_Database_Design.md) 和 [05_Repository_and_Persistence_Design.md § 4.1](../Architecture/05_Repository_and_Persistence_Design.md) 中明确规约：

- **悲观锁防死锁策略**：在涉及余额变动的事务中，必须在事务开始时对**聚合根（Account）**加锁。
  - **规则 1**：始终通过 `SELECT ... FOR UPDATE` 锁住 `accounts` 表，而不是直接锁缓存表。
  - **规则 2**：跨账户转账时，必须按 `account_id` 升序加锁，从根本上消除死锁环路。
- **缓存更新排他锁**：在 Repository 层通过数据库排他性语句更新，确保并发写入下的账目准确。
- **自愈机制 (Self-Healing)**：设计了双向校验与自愈服务，在每日凌晨对账、用户手动重建或检测到版本不连续时，通过流水聚合重新计算并强制覆盖更新缓存表。

### 2.3 汇率刷新失败容灾策略 (Resiliency for Exchange Rate Sync) ✅ 已完成

已在 [08_Exchange_Rate_System_Design.md § 3.2](../Architecture/08_Exchange_Rate_System_Design.md) 中明确规约：

- **多级降级查询链 (Fallback Chain)**：在 `CurrencyConversionService` 中，汇率换算采用责任链模式进行降级：直接汇率 $\rightarrow$ 逆向汇率 $\rightarrow$ 三角折算 $\rightarrow$ 历史降级 $\rightarrow$ 抛出异常。
- **熔断与告警机制**：
  - **断路器 (Circuit Breaker)**：如果外部 API 连续请求失败超过 3 次，断路器打开，在接下来的 1 小时内，调度任务不再请求外部 API，直接使用本地历史汇率，避免阻塞系统线程。
  - **事件告警**：一旦触发降级（如使用了超过 24 小时未更新的历史汇率），通过 `EventBus` 发布 `ExchangeRateDegradedEvent`，触发 system 审计日志，并通过邮件/Webhook 告警通知管理员。

### 2.4 前端静态元数据缓存策略 (Frontend Static Metadata Caching) ✅ 已完成

已在 [13_Frontend_Design.md § 3.0](../Architecture/13_Frontend_Design.md) 中明确规约：

- **Pinia + LocalStorage 协同设计**：在 Pinia Store 初始化时优先从客户端本地缓存（`localStorage` 或 `sessionStorage`）读取，并设置 24 小时缓存有效期。
- **主动失效与刷新时机**：
  - **用户登录成功时**：清除所有本地缓存，防止 A 用户的分类树/偏好泄露给 B 用户。
  - **用户执行写操作后**：用户新增/修改分类或修改个人偏好时，强制刷新相关缓存。
- **后端 ETag 机制**：Drogon 后端对静态元数据接口开启 `ETag` 支持，当前端收到 `304 Not Modified` 时，直接使用浏览器本地缓存，极大减少对 Drogon 后端 API 的并发压力。
