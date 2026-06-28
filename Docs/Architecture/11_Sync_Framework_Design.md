# Personal Finance Hub - Sync Framework Design

Version: 1.0

Backend: C++23

Architecture: Clean Architecture (Ports and Adapters)

---

## 1. 架构定位与设计目标

同步框架的职责是将外部异构数据（JSON, CSV, XML）安全地转换为内部的统一领域模型。

**核心设计目标：**

1. **绝对幂等性（Idempotency）**：同一个支付宝账单文件导入 10 次，系统中只能生成 1 份流水，绝不能重复记账。
2. **插件化提供方（Pluggable Providers）**：新增一个数据源（如 Chase Bank API）不应该修改任何现有的核心业务代码。
3. **隔离脏数据（Anti-Corruption）**：外部数据缺失分类、币种不明或金额异常时，必须在进入领域层之前被拦截、补全或拒绝。
4. **统一对账（Reconciliation）**：同步完毕后，需对比“外部平台宣告的余额”与“系统内部推导的余额”，产生差错报告。

---

## 2. 领域/应用层模型设计

为了隔离外部数据，我们定义一个中间态的标准化数据结构 `NormalizedExternalRecord`。

### 2.1 标准化外部记录 DTO

不管外部是支付宝的 CSV 还是网银的 JSON API，基础设施层的提供方必须将其洗清，并转换为以下格式返回给应用层。

```cpp
// application/dto/sync/NormalizedExternalRecord.hpp
#pragma once
#include <string>
#include <optional>
#include "domain/value_objects/Decimal.hpp"

struct NormalizedExternalRecord {
    std::string providerName;          // 例如: "ALIPAY", "CHASE_BANK"
    std::string externalTransactionId; // 外部系统的唯一流水号 (核心幂等键)
    std::string externalAccountId;     // 外部系统的账户标号

    std::string type;                  // "income", "expense"
    Decimal amount;
    std::string currencyCode;

    std::string transactionTime;       // ISO8601 时间字符串
    std::string counterpartName;       // 交易对手 (如: 星巴克)
    std::string description;           // 备注

    std::optional<std::string> mappedCategoryCode; // 预解析的分类 (如果 Provider 足够智能)
};

```

### 2.2 同步网关接口 (Port)

在应用层定义接口，具体的 Provider 插件在基础设施层实现。

```cpp
// application/gateways/IExternalSyncProvider.hpp
#pragma once
#include <vector>
#include <expected>
#include <string>
#include "application/dto/sync/NormalizedExternalRecord.hpp"

enum class SyncProviderError {
    AuthenticationFailed,
    NetworkError,
    ParseError,
    RateLimited
};

class IExternalSyncProvider {
public:
    virtual ~IExternalSyncProvider() = default;

    // 提供方标识符
    virtual std::string getProviderName() const = 0;

    // 拉取或解析数据的通用入口
    // configJson 包含 API Token、文件路径或各种动态配置
    virtual std::expected<std::vector<NormalizedExternalRecord>, SyncProviderError> fetchRecords(
        const std::string& configJson
    ) = 0;

    // (可选) 获取外部平台宣告的当前余额，用于对账
    virtual std::expected<Decimal, SyncProviderError> fetchRemoteBalance(
        const std::string& configJson
    ) = 0;
};

```

---

## 3. 幂等性控制与持久化设计 (Idempotency)

依据《02_Database_Design.md》的预留，我们需要一张专门的映射表 `external_transactions` 来记录“什么外部 ID 对应了什么内部流水 ID”。

### 3.1 无唯一 ID 账单的去重与合成唯一键 (Fingerprint Hash)

许多银行导出的 PDF/CSV 账单并不包含全局唯一的交易流水号（或在出入账两端的标识不同）。为了防止用户多次上传同一份 CSV 时产生重复流水，系统必须通过**合成确定性哈希（Deterministic Hash）**来生成幂等指纹 Key。

#### 3.1.1 指纹合成算法设计

定义一个标准化的指纹生成规则，消除格式、空格及大小写差异：

1. **交易时间 (`transaction_time`)**：统一转换为 UTC 时间戳字符串（格式：`YYYY-MM-DDTHH:mm:ssZ`）。如果账单只有日期没有具体时间，则统一补零（如 `2026-06-27T00:00:00Z`）。
2. **金额 (`amount`)**：去除正负号（由借贷方向决定，哈希只取绝对值），统一保留 8 位小数并去除末尾多余的 0（例如 `100` 和 `100.00000000` 统一标准化为 `"100"`）。
3. **商户/对方名称 (`merchant_name`)**：去除首尾空格，全部转为小写。
4. **账户 ID (`account_id`)**：引入内部账户 ID 防止跨账户的相同交易发生碰撞。

**拼接公式：**
$$\text{RawString} = \text{provider} + \text{":"} + \text{account\_id} + \text{":"} + \text{transaction\_time} + \text{":"} + \text{amount} + \text{":"} + \text{merchant\_name}$$

**哈希算法：** 使用 `SHA-256` 生成 64 位的十六进制字符串作为 `external_transaction_id`。

#### 3.1.2 C++ 实现示例

```cpp
// infrastructure/utils/IdempotencyFingerprint.hpp
#pragma once
#include <string>
#include <iomanip>
#include <sstream>
#include <algorithm>
#include <openssl/sha.h> // 使用 OpenSSL 计算 SHA-256

class IdempotencyFingerprint {
public:
    static std::string generate(
        const std::string& provider,
        int64_t accountId,
        const std::string& utcTime,
        const std::string& normalizedAmount,
        const std::string& merchantName)
    {
        // 1. 清理商户名
        std::string cleanMerchant = merchantName;
        cleanMerchant.erase(0, cleanMerchant.find_first_not_of(" "));
        cleanMerchant.erase(cleanMerchant.find_last_not_of(" ") + 1);
        std::transform(cleanMerchant.begin(), cleanMerchant.end(), cleanMerchant.begin(), ::tolower);

        // 2. 拼接原始字符串
        std::string raw = provider + ":" +
                          std::to_string(accountId) + ":" +
                          utcTime + ":" +
                          normalizedAmount + ":" +
                          cleanMerchant;

        // 3. 计算 SHA-256
        unsigned char hash[SHA256_DIGEST_LENGTH];
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        SHA256_Update(&sha256, raw.c_str(), raw.size());
        SHA256_Final(hash, &sha256);

        std::stringstream ss;
        for(int i = 0; i < SHA256_DIGEST_LENGTH; i++) {
            ss << std::hex << std::setw(2) << std::setfill('0') << (int)hash[i];
        }
        return ss.str();
    }
};
```

### 3.2 基础设施层仓储接口

```cpp
// domain/repositories/ISyncMappingRepository.hpp
#pragma once
#include <string>
#include <expected>
#include "domain/repositories/RepositoryError.hpp"

class ISyncMappingRepository {
public:
    virtual ~ISyncMappingRepository() = default;

    // 检查外部 ID 是否已经被导入过
    virtual std::expected<bool, RepositoryError> exists(
        const std::string& provider,
        const std::string& externalTxId) = 0;

    // 绑定映射关系 (在内部 Transaction 插入成功后调用)
    virtual std::expected<void, RepositoryError> saveMapping(
        const std::string& provider,
        const std::string& externalTxId,
        TransactionId internalTxId) = 0;
};

```

---

## 4. 同步核心编排用例 (Application Use Case)

这是整个框架的大脑 `RunSyncJobUseCase`。它负责调用 Provider 获取标准化数据，过滤已存在的流水，并通过领域服务安全地创建真正的账务流水。

```cpp
// application/use_cases/RunSyncJobUseCase.cpp
#include "application/use_cases/RunSyncJobUseCase.hpp"

std::expected<SyncJobResultDTO, UseCaseError> RunSyncJobUseCase::execute(
    AccountId accountId,
    std::shared_ptr<IExternalSyncProvider> provider,
    const std::string& configJson)
{
    SyncJobResultDTO result = {0, 0, 0}; // total, imported, skipped

    // 1. 获取目标内部账户
    auto accountOpt = accountRepo_->findById(accountId);
    if (!accountOpt) return std::unexpected(UseCaseError::AccountNotFound);

    // 2. 从外部 Provider 获取清洗后的标准化数据
    auto recordsRes = provider->fetchRecords(configJson);
    if (!recordsRes) return std::unexpected(UseCaseError::InfrastructureFailure);

    const auto& records = recordsRes.value();
    result.total = records.size();

    // 3. 遍历记录进行幂等导入
    for (const auto& record : records) {
        // 开启数据库事务 (单笔事务，避免某一条失败导致整个文件回滚)
        auto txRes = uow_->executeInTransaction([&]() -> std::expected<void, RepositoryError> {

            // 3.1 检查幂等性
            auto existsRes = syncMappingRepo_->exists(provider->getProviderName(), record.externalTransactionId);
            if (!existsRes) return std::unexpected(existsRes.error());
            if (existsRes.value() == true) {
                result.skipped++;
                return {}; // 已经存在，跳过，但不报错
            }

            // 3.2 构建真正的交易流水。
            // 单笔同步流水不调用通用 AccountingService；Use Case 负责账户、币种、分类板块校验。
            // 分类回退机制：如果没有解析出分类，使用默认的 "UNCATEGORIZED"
            std::string finalCategory = record.mappedCategoryCode.value_or("UNCATEGORIZED");

            // Note: 这里的 type 映射需要额外小心，转账识别会在下一节详述
            TransactionType txType = (record.type == "income") ? TransactionType::Income : TransactionType::Expense;

            if (accountOpt->getCurrency() != Currency(record.currencyCode)) {
                return std::unexpected(RepositoryError{RepositoryStatus::ValidationError, "Currency mismatch"});
            }

            Transaction newTx(
                TransactionId(0),
                accountOpt->getId(),
                txType,
                Money(record.amount, Currency(record.currencyCode)),
                CategoryCode(finalCategory),
                record.counterpartName + " - " + record.description
            );

            // 3.3 保存流水
            auto saveTxRes = txRepo_->saveSingle(newTx);
            if (!saveTxRes) return saveTxRes;

            // 3.4 保存防重映射表 external_transactions
            return syncMappingRepo_->saveMapping(
                provider->getProviderName(),
                record.externalTransactionId,
                newTx.getId()
            );
        });

        if (txRes.has_value() && !/*was skipped*/) {
            result.imported++;
        }
    }

    // 4. (预留) 余额对账校验逻辑
    // ...

    return result;
}

```

---

## 5. 具体插件实现案例 (Infrastructure Layer)

在底层，开发者可以随意发挥，只要最终返回 `NormalizedExternalRecord` 即可。

### 5.1 案例一：CSV 解析提供方 (支付宝/微信账单)

```cpp
// infrastructure/sync_providers/AlipayCsvProvider.cpp
#include "application/gateways/IExternalSyncProvider.hpp"
#include <fstream>
#include <sstream>

class AlipayCsvProvider : public IExternalSyncProvider {
public:
    std::string getProviderName() const override { return "ALIPAY_CSV"; }

    std::expected<std::vector<NormalizedExternalRecord>, SyncProviderError> fetchRecords(
        const std::string& configJson) override
    {
        // 假设 configJson 中包含了文件路径 {"file_path": "/tmp/alipay_2026.csv"}
        std::string filePath = parseJsonForPath(configJson);
        std::ifstream file(filePath);
        if (!file.is_open()) return std::unexpected(SyncProviderError::ParseError);

        std::vector<NormalizedExternalRecord> records;
        std::string line;

        // 忽略头部无关信息...
        while (std::getline(file, line)) {
            // 解析 CSV 列...
            NormalizedExternalRecord record;
            record.providerName = getProviderName();
            record.externalTransactionId = parsedColumns[0]; // 支付宝交易号
            record.transactionTime = parsedColumns[1];
            record.counterpartName = parsedColumns[2];
            record.type = parsedColumns[3] == "支出" ? "expense" : "income";
            record.amount = Decimal(parsedColumns[4]);
            record.currencyCode = "CNY"; // 支付宝默认为 CNY
            record.description = parsedColumns[5];

            records.push_back(record);
        }
        return records;
    }
    // ...
};

```

---

## 6. 高阶难题处理策略 (Advanced Challenges)

在设计自动化同步时，以下三个业务难题必须在架构中预留处理空间。

### 6.1 智能分类推断 (Category Guessing)

外部系统（如银行）给出的通常是“星巴克”或“Uber”，而不是系统内的“餐饮”或“交通”分类。

- **策略**：在 Application 层建立一个 `CategoryMatcherService`。它可以通过简单的基于规则的匹配（正则关键字），或者在第三阶段引入轻量级的机器学习/NLP 库，对 `counterpartName` 进行打标。
- **实现点**：如果打标置信度过低，则分类标记为 `Needs Review`（需人工审核），提供给前端一个批量审核界面。

### 6.2 隐式转账识别 (Transfer Detection)

**痛点**：当用户用一张银行卡还另一张信用卡的钱时，银行 A 会同步一条 `Expense`，信用卡 B 会同步一条 `Income`。如果不处理，报表中的总支出和总收入都会虚高。

- **策略（对冲融合）**：在跑完所有 SyncJob 后，调度器触发 `TransferDetectorJob`。
- **逻辑**：在过去 48 小时内，寻找金额绝对值完全相同的一笔 Income 和一笔 Expense。如果发现，系统将这两笔独立的单据“融合（Merge）”，升级为一个 `TransferAggregate`（生成 `transfer_groups` 记录，并修改 `type = 'transfer'`）。

### 6.3 软性对账预警 (Soft Reconciliation)

由于时区、入账时间延迟，流水计算出的余额可能与银行当天的总余额有微小差别。

- **策略**：同步完成后，拉取银行当前总余额。若 `abs(internalBalance - bankBalance) != 0`，不阻塞系统，但往 `sync_jobs` 表或预警系统写入一条 Warning：`Reconciliation discrepancy detected: difference is 12.50 USD`，提示用户可能存在漏拉的数据。
