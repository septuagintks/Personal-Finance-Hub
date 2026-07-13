# Personal Finance Hub - Service & Use Case Design

Version: 1.1

Backend: C++23

Architecture: Clean Architecture + Lightweight DDD

---

## 1. 架构定位与职责边界

在 Clean Architecture 中，业务逻辑被清晰地划分为两层：**领域服务（Domain Services）** 和 **应用层用例（Application Use Cases）**。为了确保代码的高可维护性与测试性，必须严格遵守以下职责边界：

```text
┌────────────────────────────────────────────────────────────────────────┐
│                      Application Layer (Use Case)                      │
│  1. 接收 DTO 输入并校验基础格式                                         │
│  2. 调用 Repository 基础设施加载领域实体 (I/O)                          │
│  3. 协同编排多个领域对象或调用 Domain Service 执行核心规则               │
│  4. 利用 Unit of Work 开启、提交或回滚数据库事务                         │
└───────────────────────────────────┬────────────────────────────────────┘
                                    │ 调用 (不含I/O)
┌───────────────────────────────────▼────────────────────────────────────┐
│                        Domain Layer (Service)                          │
│  1. 纯 C++23 逻辑，无任何 I/O、数据库或框架依赖                          │
│  2. 落地跨实体/跨聚合的核心金融业务规则（如多币种转账推导、金额双向平衡）  │
│  3. 负责实体状态的合法性变更，产出新的领域聚合/对象                       │
└────────────────────────────────────────────────────────────────────────┘

```

---

## 2. 领域服务设计 (Domain Services)

领域服务接口位于 `include/pfh/domain/`，实现位于 `src/domain/`。它们是无状态的，专门用来解决不属于单一实体、而是涉及多个实体交互的核心业务规则。

### 2.1 转账领域服务接口 (`TransferDomainService`)

`TransferDomainService` 负责在纯内存状态下，依据金融原则安全地推导、校验和构造转账聚合。
不要再定义跨层、跨职责的 `AccountingService`。

```cpp
enum class FeeSource {
    SourceAccount,
    TargetAccount,
    ThirdParty
};

struct TransferFee {
    FeeSource source;
    AccountId account_id; // Application 已解析并锁定的实际扣费账户
    Money amount;         // 该账户币种下的正数 magnitude
};

class TransferDomainService {
public:
    static DomainResult<TransferAggregate> build_from_outgoing_and_rate(
        Money outgoing, AccountId source, AccountId target, ExchangeRate rate,
        UserId user, TimePoint occurred_at, std::string description,
        TransferGroupId placeholder_group,
        std::optional<TransferFee> fee = std::nullopt);

    static DomainResult<TransferAggregate> build_from_both_amounts(/* 同一公共尾参数 */);
    static DomainResult<TransferAggregate> build_from_incoming_and_rate(/* 同一公共尾参数 */);
    static DomainVoidResult validate(const TransferAggregate& aggregate);
};
```

完整签名以 `include/pfh/domain/transfer_domain_service.h` 为准。Domain Service 只接收已加载实体的值和强类型 ID，不加载账户、不检查 Repository、不打开事务，也不发布事件。

### 2.2 核心业务规则落地实现 (纯 C++23)

领域实现必须满足：

1. 三种 mode 只负责三选二推导；用户提供的 authoritative amount 不得被反向重写。
2. 同币种转账要求两端金额完全一致且不保存 exchange rate。
3. 跨币种金额按 `NUMERIC(20,8)` Half-Even 舍入，rate 按 `NUMERIC(20,10)` 校验。
4. 双边流水在 Domain 中使用正数 magnitude；Repository 映射为 outgoing 负、incoming 正。
5. 手续费输入必须为正数 magnitude，`SourceAccount`/`TargetAccount` 必须匹配对应账户和币种，`ThirdParty` 必须使用不同账户。
6. 手续费构造为负数 `Adjustment`，与两端共享 user、group 和 occurred_at，不隐藏在主金额中。
7. Adjustment 按 signed 语义校验：零值非法，负数表示手续费/FX loss，正数预留返利/FX gain。
8. Domain Service 不判断账户归属或归档状态；这些依赖持久化读取的规则由 Application Use Case 在同一事务中完成。

---

## 3. 应用层用例设计 (Application Use Cases)

应用层用例位于 `include/pfh/application/use_cases/`。它通过依赖注入获取 Domain 仓储接口与工作单元，负责处理 I/O 编排与数据库事务。

### 3.1 创建跨账户转账用例 (`CreateTransferUseCase`)

这是系统中最复杂的写入用例，涉及多表写入、事务保证以及领域服务的协同。

当前执行顺序固定如下：

1. 在事务外校验强类型 ID、mode 字段组合和手续费字段组合，非法契约直接返回 ValidationError。
2. 打开一个 Unit of Work 事务，收集源、目标及可选第三方手续费账户 ID，去重后按 ID 升序调用 `find_by_id_for_update`。
3. 在同一事务中校验所有账户属于当前用户且未归档；第三方手续费账户必须与两端不同。
4. 按各账户币种解析十进制字符串，将手续费解析为选中账户币种下的 `TransferFee`。
5. 调用纯 `TransferDomainService` 构造并校验聚合。
6. 调用 `ITransactionRepository::save_transfer(tx_ctx, aggregate)` 原子保存 transfer group、双边流水和全部 Adjustment。
7. 用 Repository 返回的 group/leg ID 构造 `TransferCompletedEvent`，登记到同一 Unit of Work 的 outbox。
8. Commit 成功后返回 `TransferResultDto`；任一步失败时业务记录和 outbox 一起回滚。

Application 必须保留精确错误语义：跨用户/不存在账户返回 NotFound，归档账户返回 ArchivedAccountOperation，契约和数值错误返回 ValidationError/DomainRuleViolation，数据库错误脱敏为 InfrastructureFailure。

### 3.2 创建常规收支用例 (`CreateTransactionUseCase`)

处理单笔独立的 Income、Expense 或 Adjustment。执行顺序为：

1. 拒绝直接创建 Transfer；Transfer 只能通过聚合用例产生。
2. 在同一事务内锁定当前用户账户，并校验账户未归档、请求币种等于账户币种。
3. 按 `NUMERIC(20,8)` 解析金额；Income/Expense 只接受正 magnitude，Adjustment 接受非零 signed amount。
4. 如提供分类，通过 `ICategoryRepository` 锁定读取真实分类并校验用户归属与 board，不接受调用方伪造 board。
5. 调用 `save_single(tx_ctx, transaction)`，并在同一 Unit of Work 登记 `TransactionCreatedEvent`。
6. 返回 Repository 分配 ID 和规范化存储符号后的 DTO；失败时业务写入与 outbox 一起回滚。

### 3.3 初始化用户默认数据 (`InitializeUserDefaultsUseCase`)

用户注册后不能面对空分类、空偏好和没有可选账户 subtype 的状态。
注册流程必须在同一个应用层编排中初始化默认数据。

该用例在 P1-S10-06/S10-07 随注册与基础资源 API 落地。它必须通过一个 Unit of Work 使用同一事务上下文调用 `IUserPreferenceRepository`、`ICategoryRepository` 与 `IAuditLogRepository`，并把 Repository 错误映射为稳定的 Application `Error`，不得在 Domain Service 中执行 seed 或数据库访问。

注册专用 bootstrap UoW 在插入 `users` 前不绑定租户；取得数据库分配的 `UserId` 后，必须在同一事务中一次性绑定该租户并执行 `SET LOCAL`，再写入偏好、分类、审计和 outbox。该事务不能切换第二次租户，也不能拆成“先提交用户、再初始化默认数据”的两个成功边界。

初始化内容：

1. `UserPreference` 默认值
2. 收入板块默认一级分类和必要二级分类
3. 支出板块默认一级分类和必要二级分类
4. 账户 subtype 预设只作为前端可选项或配置，不创建账户实例
5. 初始化 AuditLog

`system_category_templates` seed 由数据库迁移或启动脚本维护。
Use Case 只负责把模板复制到用户自己的 `categories`。

### 3.4 系统预设数据 Seed

Seed 数据属于基础设施初始化，不属于 Domain。

必须包含：

- `currencies`: CNY、USD、EUR、JPY、HKD、BTC、ETH 等元数据
- `system_category_templates`: 可选分类池、收入默认分类、支出默认分类
- account subtype presets: 储蓄账户、信用账户、数字钱包、投资账户、现金账户、虚拟货币账户、其他账户

Seed 规则：

1. Seed 必须幂等，可重复执行
2. 使用稳定自然键，例如 `group_name + parent_id + name`
3. 禁止修改用户已经复制出来的 `categories`
4. 新增系统模板只影响未来初始化或用户手动启用
5. 删除系统模板必须保留历史引用，不影响用户已有分类

---

## 4. 数据传输对象 (DTO) 定义

DTO 当前集中在 `include/pfh/application/dto.h`。它们是没有业务行为的边界结构体，专门用于在 Presentation 层（HTTP/JSON）与 Application 层之间安全传输数据，使应用层不直接暴露 Domain 实体。

```cpp
struct CreateTransactionCommand {
    UserId user_id;
    AccountId account_id;
    TransactionType type;
    std::string amount;        // Income/Expense 正 magnitude；Adjustment signed
    std::string currency_code;
    std::string description;
    std::optional<CategoryId> category_id;
    std::optional<TimePoint> occurred_at;
};
```

```cpp
struct CreateTransferCommand {
    UserId user_id;
    AccountId source_account_id;
    AccountId target_account_id;
    TransferInputMode mode;
    std::string outgoing_amount;
    std::string incoming_amount;
    std::string rate;
    std::optional<std::string> fee_amount;
    std::optional<FeeSource> fee_source;
    std::optional<AccountId> fee_account_id;
    std::string description;
    std::optional<TimePoint> occurred_at;
};
```

Application DTO 保留字符串金额，只有 Use Case/Domain 解析为 `Decimal`/`Money`。HTTP 使用 camelCase 的映射由 Presentation 层负责，不能让 JSON 模型直接进入 Domain。

---

## 5. 异常与控制流规范 (C++23 std::expected)

在本项目的高级 C++23 实践中，全面禁止在业务主流程中使用传统的 `try-catch` 异常进行控制流跳转。所有可预期的业务失败均采用强类型的 `std::expected` 显式向上传递。

### 5.1 错误链条单向传导机制

- **Domain 层错误**：`DomainError` / `DomainErrorCode`（表达纯领域规则，如金额平衡或币种不匹配）。
- **Infrastructure 层错误**：`RepositoryError`（表达底层数据库连接中断、违反唯一索引等故障）。
- **Application 层错误**：`Error` / `ErrorCode`（对 Presentation 暴露稳定、脱敏的错误类别）。

### 5.2 表现层（Presentation）响应映射准则

当前端请求由于逻辑错误被拒绝时，Controller 应该提供统一的 DTO 映射：

```cpp
// 示例：在 presentation 模块中如何消费用例的返回值并转化为 HTTP 状态码
void HandleTransferResult(const Result<TransferResultDto>& result) {
    if (!result) {
        switch (result.error().code) {
            case ErrorCode::NotFound:
                // 映射为 HTTP 404 Not Found
                break;
            case ErrorCode::DomainRuleViolation:
                // 映射为 HTTP 422 Unprocessable Entity (业务规则冲突)
                break;
            case ErrorCode::InfrastructureFailure:
                // 映射为 HTTP 500 Internal Server Error
                break;
            default:
                // 其余映射遵循 15_Error_Handling_Design.md
                break;
        }
    } else {
        // 映射为 HTTP 200 OK / 201 Created
    }
}

```
