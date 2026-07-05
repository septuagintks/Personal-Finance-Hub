# Personal Finance Hub - Service & Use Case Design

Version: 1.0

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

领域服务位于 `domain/services/`。它们是无状态的，专门用来解决不属于单一实体、而是涉及多个实体交互的核心业务规则。

### 2.1 转账领域服务接口 (`TransferDomainService`)

`TransferDomainService` 负责在纯内存状态下，依据金融原则安全地推导、校验和构造转账聚合。
不要再定义跨层、跨职责的 `AccountingService`。

```cpp
// domain/services/TransferDomainService.hpp
#pragma once
#include <expected>
#include <optional>
#include "domain/entities/Account.hpp"
#include "domain/aggregates/TransferAggregate.hpp"
#include "domain/entities/Transaction.hpp"

enum class TransferRuleError {
    CurrencyMismatch,
    NegativeAmount,
    InvalidExchangeRate,
    AccountFrozenOrArchived,
    TransferImbalance
};

enum class FeeSource {
    SourceAccount,  // 从源账户（出账账户）扣除
    TargetAccount,  // 从目标账户（入账账户）扣除
    ThirdParty      // 从独立的第三方账户扣除（需额外传入 feeAccountId）
};

class TransferDomainService {
public:
    // 核心规则：校验并构造一个转账聚合（处理出账、入账、汇率三选二推导及手续费隔离）
    static std::expected<TransferAggregate, TransferRuleError> buildTransfer(
        const Account& sourceAccount,
        const Account& targetAccount,
        TransferMode mode,
        std::optional<Money> sourceAmount,
        std::optional<Money> targetAmount,
        std::optional<Decimal> exchangeRate,
        std::optional<Money> feeAmount, // 独立的手续费调整项
        FeeSource feeSource,            // 手续费扣除来源
        std::optional<AccountId> feeAccountId, // 第三方手续费账户 ID
        const std::string& description
    );
};

```

### 2.2 核心业务规则落地实现 (纯 C++23)

以下展示跨币种转账在领域层如何进行严格的三选二公式推导，并强力确保手续费作为 `Adjustment` 离散。

```cpp
// domain/services/TransferDomainService.cpp
#include "domain/services/TransferDomainService.hpp"
#include <cmath>

std::expected<TransferAggregate, TransferRuleError> TransferDomainService::buildTransfer(
    const Account& sourceAccount,
    const Account& targetAccount,
    TransferMode mode,
    std::optional<Money> sourceAmount,
    std::optional<Money> targetAmount,
    std::optional<Decimal> exchangeRate,
    std::optional<Money> feeAmount,
    const std::string& description)
{
    // 约束 1：冻结或归档账户禁止发生资金变动
    if (sourceAccount.isArchived() || targetAccount.isArchived()) {
        return std::unexpected(TransferRuleError::AccountFrozenOrArchived);
    }

    Money finalSource = sourceAmount.value_or(Money(Decimal(0), sourceAccount.getCurrency()));
    Money finalTarget = targetAmount.value_or(Money(Decimal(0), targetAccount.getCurrency()));
    Decimal finalRate = exchangeRate.value_or(Decimal(1));

    // 约束 2：同币种转账特殊简化校验
    if (sourceAccount.getCurrency() == targetAccount.getCurrency()) {
        if (mode == TransferMode::OutgoingAndIncoming && finalSource.getAmount() != finalTarget.getAmount()) {
            return std::unexpected(TransferRuleError::TransferImbalance);
        }
        finalTarget = finalSource;
        finalRate = Decimal(1);
    } else {
        // 约束 3：跨币种转账根据模式强制推导第三变量（根据 04_Money_Currency_System_Design 规范）
        switch (mode) {
            case TransferMode::OutgoingAndRate:
                if (!sourceAmount || !exchangeRate) return std::unexpected(TransferRuleError::TransferImbalance);
                finalTarget = Money(finalSource.getAmount() * finalRate, targetAccount.getCurrency());
                break;
            case TransferMode::OutgoingAndIncoming:
                if (!sourceAmount || !targetAmount) return std::unexpected(TransferRuleError::TransferImbalance);
                finalRate = finalTarget.getAmount() / finalSource.getAmount();
                break;
            case TransferMode::IncomingAndRate:
                if (!targetAmount || !exchangeRate) return std::unexpected(TransferRuleError::TransferImbalance);
                finalSource = Money(finalTarget.getAmount() / finalRate, sourceAccount.getCurrency());
                break;
        }
    }

    if (finalSource.getAmount() <= 0 || finalTarget.getAmount() <= 0) {
        return std::unexpected(TransferRuleError::NegativeAmount);
    }

    // 构造转账双流水聚合根
    TransferAggregate aggregate(sourceAccount.getId(), targetAccount.getId(), finalSource, finalTarget, finalRate, description);

    // 约束 4：手续费绝不隐藏在转账主金额中，作为独立 Adjustment 流水附属于聚合根
    if (feeAmount && feeAmount->getAmount() > 0) {
        Transaction feeTx(
            TransactionId(0), // 由基础设施生成
            sourceAccount.getId(),
            TransactionType::Adjustment, // 显式标记为调整项
            *feeAmount,
            CategoryCode("SYSTEM_FEE"),
            "手续费: " + description
        );
        aggregate.addAdjustment(std::move(feeTx));
    }

    return aggregate;
}

```

---

## 3. 应用层用例设计 (Application Use Cases)

应用层位于 `application/use_cases/`。它通过依赖注入获取 Domain 仓储接口与工作单元，负责处理 I/O 编排与数据库事务。

### 3.1 创建跨账户转账用例 (`CreateTransferUseCase`)

这是系统中最复杂的写入用例，涉及多表写入、事务保证以及领域服务的协同。

```cpp
// application/use_cases/CreateTransferUseCase.hpp
#pragma once
#include <memory>
#include "domain/repositories/IAccountRepository.hpp"
#include "domain/repositories/ITransactionRepository.hpp"
#include "application/persistence/IUnitOfWork.hpp"
#include "application/dto/TransferInputDTO.hpp"

enum class UseCaseError {
    AccountNotFound,
    DomainRuleViolation,
    InfrastructureFailure
};

class CreateTransferUseCase {
private:
    std::shared_ptr<IAccountRepository> accountRepo_;
    std::shared_ptr<ITransactionRepository> txRepo_;
    std::shared_ptr<IUnitOfWork> uow_;

public:
    CreateTransferUseCase(
        std::shared_ptr<IAccountRepository> accountRepo,
        std::shared_ptr<ITransactionRepository> txRepo,
        std::shared_ptr<IUnitOfWork> uow)
        : accountRepo_(accountRepo), txRepo_(txRepo), uow_(uow) {}

    std::expected<void, UseCaseError> execute(const TransferInputDTO& dto) {
        // 1. I/O 阶段：从仓储读取源账户与目标账户实体
        auto sourceOpt = accountRepo_->findById(AccountId(dto.sourceAccountId));
        if (!sourceOpt) return std::unexpected(UseCaseError::AccountNotFound);

        auto targetOpt = accountRepo_->findById(AccountId(dto.targetAccountId));
        if (!targetOpt) return std::unexpected(UseCaseError::AccountNotFound);

        // 2. 领域计算阶段：调用无状态领域服务执行金融核心推导规则
        auto aggregateResult = TransferDomainService::buildTransfer(
            *sourceOpt,
            *targetOpt,
            dto.mode,
            dto.sourceAmount,
            dto.targetAmount,
            dto.exchangeRate,
            dto.feeAmount,
            dto.description
        );

        if (!aggregateResult) {
            return std::unexpected(UseCaseError::DomainRuleViolation);
        }

        // 3. 编排持久化阶段：通过 Unit Of Work 开启强 ACID 事务
        auto txResult = uow_->executeInTransaction([&]() -> std::expected<void, RepositoryError> {
            // 保存转账聚合，返回持久化后的 transfer_group_id
            auto transferGroupIdResult = txRepo_->saveTransfer(*aggregateResult);
            if (!transferGroupIdResult) {
                return std::unexpected(transferGroupIdResult.error());
            }

            // 将领域事件登记到当前 UoW，随后由 outbox 写入同一事务
            uow_->registerEvent(std::make_shared<TransferCompletedEvent>(
                sourceOpt->getId(),
                targetOpt->getId(),
                *transferGroupIdResult
            ));

            return {};
        });

        if (!txResult) {
            return std::unexpected(UseCaseError::InfrastructureFailure);
        }

        return {};
    }
};

```

### 3.2 创建常规收支用例 (`CreateTransactionUseCase`)

处理单笔独立的 Income（收入）或 Expense（支出）。

```cpp
// application/use_cases/CreateTransactionUseCase.hpp
#pragma once
#include <chrono>
#include <memory>
#include "domain/repositories/IAccountRepository.hpp"
#include "domain/repositories/ITransactionRepository.hpp"
#include "application/persistence/IUnitOfWork.hpp"
#include "application/dto/TransactionInputDTO.hpp"

class CreateTransactionUseCase {
private:
    std::shared_ptr<IAccountRepository> accountRepo_;
    std::shared_ptr<ITransactionRepository> txRepo_;
    std::shared_ptr<IUnitOfWork> uow_;

public:
    CreateTransactionUseCase(
        std::shared_ptr<IAccountRepository> accountRepo,
        std::shared_ptr<ITransactionRepository> txRepo,
        std::shared_ptr<IUnitOfWork> uow)
        : accountRepo_(accountRepo), txRepo_(txRepo), uow_(uow) {}

    std::expected<void, UseCaseError> execute(const TransactionInputDTO& dto) {
        auto accountOpt = accountRepo_->findById(AccountId(dto.accountId));
        if (!accountOpt) return std::unexpected(UseCaseError::AccountNotFound);

        if (accountOpt->getCurrency() != Currency(dto.currencyCode)) {
            return std::unexpected(UseCaseError::DomainRuleViolation);
        }

        // 单笔收支不通过通用 AccountingService。
        // Use Case 校验账户、币种、分类板块后，构造普通 Transaction。
        Transaction transaction(
            TransactionId(0),
            accountOpt->getId(),
            dto.type,
            Money(dto.amount, Currency(dto.currencyCode)),
            CategoryCode(dto.categoryCode),
            dto.description
        );

        // 利用事务持久化
        auto saveResult = uow_->executeInTransaction([&]() -> std::expected<void, RepositoryError> {
            auto transactionIdResult = txRepo_->saveSingle(transaction);
            if (!transactionIdResult) {
                return std::unexpected(transactionIdResult.error());
            }

            uow_->registerEvent(std::make_shared<TransactionCreatedEvent>(
                accountOpt->getOwner(),
                *transactionIdResult,
                accountOpt->getId(),
                std::chrono::system_clock::now()
            ));

            return {};
        });

        if (!saveResult) return std::unexpected(UseCaseError::InfrastructureFailure);
        return {};
    }
};

```

### 3.3 初始化用户默认数据 (`InitializeUserDefaultsUseCase`)

用户注册后不能面对空分类、空偏好和没有可选账户 subtype 的状态。
注册流程必须在同一个应用层编排中初始化默认数据。

```cpp
// application/use_cases/InitializeUserDefaultsUseCase.hpp
#pragma once
#include <memory>
#include "domain/repositories/ICategoryRepository.hpp"
#include "domain/repositories/IUserPreferenceRepository.hpp"
#include "domain/repositories/IAuditLogRepository.hpp"
#include "application/persistence/IUnitOfWork.hpp"

class InitializeUserDefaultsUseCase {
private:
    std::shared_ptr<ICategoryRepository> categoryRepo_;
    std::shared_ptr<IUserPreferenceRepository> preferenceRepo_;
    std::shared_ptr<IAuditLogRepository> auditRepo_;
    std::shared_ptr<IUnitOfWork> uow_;

public:
    std::expected<void, UseCaseError> execute(UserId userId, Currency defaultCurrency) {
        return uow_->executeInTransaction([&]() -> std::expected<void, RepositoryError> {
            UserPreference preference = UserPreference::defaults(userId, defaultCurrency);
            auto prefResult = preferenceRepo_->save(preference);
            if (!prefResult) return prefResult;

            auto categoryResult = categoryRepo_->initializeDefaultsForUser(userId);
            if (!categoryResult) return categoryResult;

            AuditLog log = AuditLog::system(
                userId,
                AuditAction::Create,
                "UserDefaults",
                std::to_string(userId.value())
            );
            return auditRepo_->record(log);
        }).transform_error([](const RepositoryError&) {
            return UseCaseError::InfrastructureFailure;
        });
    }
};
```

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

DTO 位于 `application/dto/`。它们是纯粹的结构体（POD），没有任何业务行为，专门用于在 Presentation 层（HTTP/JSON）与 Application 层之间安全传输数据，使应用层不直接暴露 Domain 实体。

```cpp
// application/dto/TransactionInputDTO.hpp
#pragma once
#include <string>
#include "domain/entities/Transaction.hpp" // 引入 TransactionType 枚举

struct TransactionInputDTO {
    int64_t accountId;
    TransactionType type; // income 或 expense
    Decimal amount;
    std::string currencyCode;
    std::string categoryCode;
    std::string description;
};

```

```cpp
// application/dto/TransferInputDTO.hpp
#pragma once
#include <string>
#include <optional>
#include "domain/value_objects/Money.hpp"
#include "domain/value_objects/Decimal.hpp"
#include "domain/value_objects/TransferMode.hpp"

struct TransferInputDTO {
    int64_t sourceAccountId;
    int64_t targetAccountId;
    TransferMode mode;
    std::optional<Money> sourceAmount;
    std::optional<Money> targetAmount;
    std::optional<Decimal> exchangeRate;
    std::optional<Money> feeAmount;
    std::string description;
};

```

---

## 5. 异常与控制流规范 (C++23 std::expected)

在本项目的高级 C++23 实践中，全面禁止在业务主流程中使用传统的 `try-catch` 异常进行控制流跳转。所有可预期的业务失败均采用强类型的 `std::expected` 显式向上传递。

### 5.1 错误链条单向传导机制

- **Domain 层错误**：`TransferRuleError`（表达违反纯粹转账规则，如：金额平衡失败）。
- **Infrastructure 层错误**：`RepositoryError`（表达底层数据库连接中断、违反唯一索引等故障）。
- **Application 层错误**：`UseCaseError`（对表现层暴露的顶层高维概括错误）。

### 5.2 表现层（Presentation）响应映射准则

当前端请求由于逻辑错误被拒绝时，Controller 应该提供统一的 DTO 映射：

```cpp
// 示例：在 presentation 模块中如何消费用例的返回值并转化为 HTTP 状态码
void HandleTransferResult(const std::expected<void, UseCaseError>& result) {
    if (!result) {
        switch (result.error()) {
            case UseCaseError::AccountNotFound:
                // 映射为 HTTP 404 Not Found
                break;
            case UseCaseError::DomainRuleViolation:
                // 映射为 HTTP 422 Unprocessable Entity (业务规则冲突)
                break;
            case UseCaseError::InfrastructureFailure:
                // 映射为 HTTP 500 Internal Server Error
                break;
        }
    } else {
        // 映射为 HTTP 200 OK / 201 Created
    }
}

```
