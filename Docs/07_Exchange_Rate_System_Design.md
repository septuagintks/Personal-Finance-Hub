# Personal Finance Hub - Exchange Rate System Design

Version: 1.0

Backend: C++23

Architecture: Clean Architecture + Lightweight DDD

---

## 1. 架构定位与设计目标

汇率系统在整个记账平台中承担“公共设施”的作用。
**核心原则：**

1. **Append-Only（仅追加）**：历史汇率绝对不能被覆盖，保证过去某天的资产报表和跨币种转账在未来任何时候重算都能得出一致的结果。
2. **防腐层隔离（Anti-Corruption Layer）**：外部汇率 API（如 OpenExchangeRates, ECB）的数据格式千奇百怪，必须在 Infrastructure 层进行隔离转换，绝不能让 JSON 解析或 HTTP 响应污染 Domain 层。
3. **三角套汇推导（Triangulation）**：不需要存储任意两两货币的汇率，通常以外部 API 的基础货币（如 USD 或 EUR）作为枢纽（Pivot），在内存中推导交叉汇率（Cross Rate）。

---

## 2. 领域层设计 (Domain Layer)

领域层主要定义汇率值对象、查询接口以及处理交叉汇率的纯内存推导逻辑。

### 2.1 汇率值对象 (Value Object)

`ExchangeRate` 是一个没有身份标识（ID）的值对象，它的唯一性由 `基础币种 + 目标币种 + 时间戳` 共同决定。

```cpp
// domain/value_objects/ExchangeRate.hpp
#pragma once
#include <string>
#include <chrono>
#include "domain/value_objects/Currency.hpp"
#include "domain/value_objects/Decimal.hpp"

class ExchangeRate {
private:
    Currency base_;
    Currency target_;
    Decimal rate_;
    std::chrono::system_clock::time_point fetchedAt_;
    std::string providerName_; // 例如: "ECB", "Manual", "OpenExchangeRates"

public:
    ExchangeRate(Currency base, Currency target, Decimal rate,
                 std::chrono::system_clock::time_point fetchedAt, std::string providerName)
        : base_(std::move(base)), target_(std::move(target)),
          rate_(std::move(rate)), fetchedAt_(fetchedAt), providerName_(std::move(providerName)) {}

    const Currency& getBase() const { return base_; }
    const Currency& getTarget() const { return target_; }
    const Decimal& getRate() const { return rate_; }

    // 逆向汇率：如果当前是 USD->CNY (7.18)，调用此方法返回 CNY->USD (1/7.18)
    ExchangeRate inverse() const {
        // Decimal 需要支持明确的舍入模式，如保留 10 位小数
        return ExchangeRate(target_, base_, Decimal(1) / rate_, fetchedAt_, providerName_);
    }
};

```

### 2.2 仓储与网关接口

在领域层声明两个不同的接口：一个负责读写自家数据库（Repository），一个负责请求外部 API（Provider Gateway）。

```cpp
// domain/repositories/IExchangeRateRepository.hpp
#pragma once
#include <vector>
#include <expected>
#include "domain/value_objects/ExchangeRate.hpp"
#include "domain/repositories/RepositoryError.hpp"

class IExchangeRateRepository {
public:
    virtual ~IExchangeRateRepository() = default;

    // 仅追加写入
    virtual std::expected<void, RepositoryError> save(const std::vector<ExchangeRate>& rates) = 0;

    // 获取两个币种之间最新的汇率
    virtual std::expected<ExchangeRate, RepositoryError> getLatest(
        const Currency& base, const Currency& target) = 0;

    // 获取历史上最接近（但在此时间点之前）的汇率记录
    virtual std::expected<ExchangeRate, RepositoryError> getHistorical(
        const Currency& base, const Currency& target,
        std::chrono::system_clock::time_point timestamp) = 0;
};

```

```cpp
// domain/gateways/IExchangeRateProvider.hpp
#pragma once
#include <vector>
#include <expected>
#include "domain/value_objects/ExchangeRate.hpp"

enum class ProviderError { NetworkFailure, InvalidResponse, RateLimitExceeded };

// 外部网关接口（依然定义在 Domain 层或 Application 层，由 Infra 层实现）
class IExchangeRateProvider {
public:
    virtual ~IExchangeRateProvider() = default;
    virtual std::string getProviderName() const = 0;

    // 抓取相对于枢纽货币（如 USD）的所有支持币种的最新汇率
    virtual std::expected<std::vector<ExchangeRate>, ProviderError> fetchLatestRates() = 0;
};

```

### 2.3 货币转换领域服务与三角折算

**设计原则：以 USD 为枢纽货币（Pivot Currency）**

外部汇率 API（如 OpenExchangeRates、ECB）通常只提供相对于单一基准货币（如 USD）的汇率对。PFH 采用以下策略：

1. **汇率拉取策略**：仅拉取用户系统中已添加货币与 USD 之间的汇率
2. **枢纽货币固定为 USD**：所有汇率以 USD 为基准存储
3. **非 USD 货币对通过三角折算推导**：例如 EUR → CNY 通过 EUR→USD→CNY 计算

**三角折算公式**

已知：

- `EUR → USD` 汇率为 `r1`（例如 1 EUR = 1.08 USD）
- `USD → CNY` 汇率为 `r2`（例如 1 USD = 7.18 CNY）

推导 `EUR → CNY`：

```
1 EUR = r1 USD = r1 × r2 CNY
因此 EUR → CNY 汇率 = r1 × r2
```

推导 `CNY → EUR`（逆向）：

```
CNY → EUR = 1 / (r1 × r2)
```

当数据库中只有 `USD->CNY` 和 `USD->EUR`，而业务需要 `EUR->CNY` 时，由 `CurrencyConversionService` 负责在纯内存中推导。
不要定义单独的 `ExchangeRateService`，避免和应用层汇率刷新用例、基础设施 Provider 混在一起。

```cpp
// domain/services/CurrencyConversionService.hpp
#pragma once
#include <expected>
#include "domain/value_objects/ExchangeRate.hpp"

enum class CurrencyConversionError {
    MissingPivotRate,
    InfiniteOrZeroRate,
    UnsupportedCurrencyPair
};

class CurrencyConversionService {
private:
    static constexpr const char* PIVOT_CURRENCY = "USD";

public:
    // 三角折算：已知 Base->Pivot 和 Target->Pivot，推导 Base->Target
    // 例如：EUR->USD 和 CNY->USD，推导 EUR->CNY
    static std::expected<ExchangeRate, CurrencyConversionError> calculateCrossRate(
        const ExchangeRate& baseToPivot,
        const ExchangeRate& targetToPivot)
    {
        // 验证两个汇率都以相同的枢纽货币为目标
        if (baseToPivot.getTarget().getCode() != PIVOT_CURRENCY ||
            targetToPivot.getTarget().getCode() != PIVOT_CURRENCY) {
            return std::unexpected(CurrencyConversionError::MissingPivotRate);
        }

        // EUR->USD = 1.08, CNY->USD = 0.139
        // EUR->CNY = EUR->USD / CNY->USD = 1.08 / 0.139 ≈ 7.77
        Decimal crossRate = baseToPivot.getRate() / targetToPivot.getRate();

        if (crossRate.isZeroOrNegative()) {
            return std::unexpected(CurrencyConversionError::InfiniteOrZeroRate);
        }

        return ExchangeRate(
            baseToPivot.getBase(),
            targetToPivot.getBase(),
            crossRate,
            std::max(baseToPivot.getFetchedAt(), targetToPivot.getFetchedAt()), // 使用较晚的时间戳
            "TriangularCalculation"
        );
    }

    // 便捷方法：直接从 Repository 查询并推导
    // 适用于 Application 层调用
    static std::expected<ExchangeRate, CurrencyConversionError> findOrCalculateRate(
        const Currency& base,
        const Currency& target,
        IExchangeRateRepository& repository,
        std::optional<std::chrono::system_clock::time_point> timestamp = std::nullopt)
    {
        // 1. 尝试直接查询 Base -> Target
        auto directRate = timestamp
            ? repository.getHistorical(base, target, *timestamp)
            : repository.getLatest(base, target);

        if (directRate) {
            return directRate;
        }

        // 2. 尝试查询逆向汇率 Target -> Base，然后取倒数
        auto inverseRate = timestamp
            ? repository.getHistorical(target, base, *timestamp)
            : repository.getLatest(target, base);

        if (inverseRate) {
            return inverseRate->inverse();
        }

        // 3. 三角折算：通过 USD 枢纽推导
        Currency pivot(PIVOT_CURRENCY);

        // 如果 Base 或 Target 本身就是 USD，说明缺少必要汇率
        if (base == pivot || target == pivot) {
            return std::unexpected(CurrencyConversionError::MissingPivotRate);
        }

        // 查询 Base -> USD
        auto baseToPivot = timestamp
            ? repository.getHistorical(base, pivot, *timestamp)
            : repository.getLatest(base, pivot);

        if (!baseToPivot) {
            return std::unexpected(CurrencyConversionError::MissingPivotRate);
        }

        // 查询 Target -> USD
        auto targetToPivot = timestamp
            ? repository.getHistorical(target, pivot, *timestamp)
            : repository.getLatest(target, pivot);

        if (!targetToPivot) {
            return std::unexpected(CurrencyConversionError::MissingPivotRate);
        }

        // 执行三角折算
        return calculateCrossRate(*baseToPivot, *targetToPivot);
    }
};

```

**使用示例**

```cpp
// 应用层 Use Case 中
auto rateResult = CurrencyConversionService::findOrCalculateRate(
    Currency("EUR"),
    Currency("CNY"),
    *exchangeRateRepo_
);

if (!rateResult) {
    return std::unexpected(UseCaseError::ExchangeRateNotAvailable);
}

// 得到推导出的 EUR->CNY 汇率
ExchangeRate eurToCny = *rateResult;
```

**数据库存储策略**

数据库中只存储与 USD 的直接汇率对：

```
exchange_rates 表内容示例：
| base_currency_code | target_currency_code | rate     | source              | fetched_at          |
|--------------------|----------------------|----------|---------------------|---------------------|
| USD                | CNY                  | 7.18     | OpenExchangeRates   | 2026-06-25 08:00:00 |
| USD                | EUR                  | 0.92     | OpenExchangeRates   | 2026-06-25 08:00:00 |
| USD                | JPY                  | 149.50   | OpenExchangeRates   | 2026-06-25 08:00:00 |
| USD                | GBP                  | 0.78     | OpenExchangeRates   | 2026-06-25 08:00:00 |
```

不存储推导出的交叉汇率（如 EUR->CNY、JPY->GBP），这些在运行时按需计算。

**优势**

1. **存储精简**：N 个货币只需存储 N-1 条汇率记录，而非 N×(N-1) 条
2. **一致性保证**：所有汇率来自同一时间点的同一数据源
3. **扩展性强**：添加新货币只需拉取该货币与 USD 的汇率
4. **审计清晰**：所有汇率来源可追溯，推导过程透明

---

## 3. 应用层设计 (Application Layer)

应用层将协调外部 Provider 与内部 Repository，完成定期的数据拉取和写入。

### 3.1 刷新汇率用例 (`RefreshExchangeRatesUseCase`)

供后台 Scheduler 每日定时调用。

```cpp
// application/use_cases/RefreshExchangeRatesUseCase.hpp
#pragma once
#include <memory>
#include "domain/gateways/IExchangeRateProvider.hpp"
#include "domain/repositories/IExchangeRateRepository.hpp"

class RefreshExchangeRatesUseCase {
private:
    std::shared_ptr<IExchangeRateProvider> provider_;
    std::shared_ptr<IExchangeRateRepository> repository_;
    std::shared_ptr<IAuditLogRepository> auditRepo_;

public:
    RefreshExchangeRatesUseCase(
        std::shared_ptr<IExchangeRateProvider> provider,
        std::shared_ptr<IExchangeRateRepository> repository,
        std::shared_ptr<IAuditLogRepository> auditRepo)
        : provider_(provider), repository_(repository), auditRepo_(auditRepo) {}

    std::expected<void, std::string> execute() {
        LOG_INFO << "Starting exchange rate refresh from " << provider_->getProviderName();

        // 1. 调用基础设施层请求外部 API
        auto fetchedRatesResult = provider_->fetchLatestRates();
        if (!fetchedRatesResult) {
            std::string errorMsg = "Failed to fetch rates from " + provider_->getProviderName();
            LOG_ERROR << errorMsg;

            // 写入审计日志记录失败
            auditRepo_->log(AuditAction::RefreshExchangeRate, "ExchangeRate", "bulk",
                           std::nullopt, std::nullopt,
                           {{"status", "failed"}, {"provider", provider_->getProviderName()}});

            return std::unexpected(errorMsg);
        }

        auto& rates = fetchedRatesResult.value();
        if (rates.empty()) {
            LOG_WARN << "No rates fetched from provider, skipping";
            return {}; // 无数据则跳过
        }

        // 2. 将数据以 Append-Only 模式持久化到数据库
        auto saveResult = repository_->save(rates);
        if (!saveResult) {
            std::string errorMsg = "Failed to persist exchange rates to database";
            LOG_ERROR << errorMsg;

            auditRepo_->log(AuditAction::RefreshExchangeRate, "ExchangeRate", "bulk",
                           std::nullopt, std::nullopt,
                           {{"status", "db_failed"}, {"count", rates.size()}});

            return std::unexpected(errorMsg);
        }

        // 3. 写入审计日志记录成功
        auditRepo_->log(AuditAction::RefreshExchangeRate, "ExchangeRate", "bulk",
                       std::nullopt, std::nullopt,
                       {{"status", "success"},
                        {"count", rates.size()},
                        {"provider", provider_->getProviderName()}});

        LOG_INFO << "Successfully refreshed " << rates.size() << " exchange rates";
        return {};
    }
};

```

### 3.2 汇率查询与降级策略

当外部 API 完全不可用时，应用层应能够使用数据库中的历史汇率作为降级方案。

#### 3.2.1 多级降级查询链 (Fallback Chain)

在 `CurrencyConversionService` 中，汇率换算采用责任链模式进行降级：

1. **直接汇率**：尝试直接查询 Base -> Target。
2. **逆向汇率**：尝试查询逆向汇率 Target -> Base，然后取倒数。
3. **三角折算**：通过 USD 枢纽推导。
4. **历史降级**：寻找历史上最接近（但在此时间点之前）的汇率记录。
5. **抛出异常**：若以上均不可得，抛出 `ExchangeRateUnavailableException`。

#### 3.2.2 熔断与告警机制

- **断路器 (Circuit Breaker)**：如果外部 API 连续请求失败超过 3 次，断路器打开，在接下来的 1 小时内，调度任务不再请求外部 API，直接使用本地历史汇率，避免阻塞系统线程。
- **事件告警**：一旦触发降级（如使用了超过 24 小时未更新的历史汇率），通过 `EventBus` 发布 `ExchangeRateDegradedEvent`，触发系统审计日志，并通过邮件/Webhook 告警通知管理员。

```cpp
// application/services/ExchangeRateQueryService.hpp
#pragma once
#include <memory>
#include "domain/repositories/IExchangeRateRepository.hpp"
#include "domain/services/CurrencyConversionService.hpp"

class ExchangeRateQueryService {
private:
    std::shared_ptr<IExchangeRateRepository> repository_;

public:
    explicit ExchangeRateQueryService(std::shared_ptr<IExchangeRateRepository> repository)
        : repository_(repository) {}

    // 获取汇率，支持三角折算和历史查询
    std::expected<ExchangeRate, std::string> getRate(
        const Currency& base,
        const Currency& target,
        std::optional<std::chrono::system_clock::time_point> timestamp = std::nullopt)
    {
        auto result = CurrencyConversionService::findOrCalculateRate(
            base, target, *repository_, timestamp
        );

        if (!result) {
            // 映射领域错误为应用层错误消息
            switch (result.error()) {
                case CurrencyConversionError::MissingPivotRate:
                    return std::unexpected("Exchange rate not available for currency pair");
                case CurrencyConversionError::InfiniteOrZeroRate:
                    return std::unexpected("Invalid exchange rate calculation result");
                case CurrencyConversionError::UnsupportedCurrencyPair:
                    return std::unexpected("Unsupported currency pair");
                default:
                    return std::unexpected("Unknown exchange rate error");
            }
        }

        return result;
    }

    // 批量转换金额到基准货币
    std::expected<std::vector<Money>, std::string> convertToBase(
        const std::vector<Money>& amounts,
        const Currency& baseCurrency,
        std::optional<std::chrono::system_clock::time_point> timestamp = std::nullopt)
    {
        std::vector<Money> converted;
        converted.reserve(amounts.size());

        for (const auto& money : amounts) {
            // 如果已经是基准货币，直接添加
            if (money.currency() == baseCurrency) {
                converted.push_back(money);
                continue;
            }

            // 查询或推导汇率
            auto rateResult = getRate(money.currency(), baseCurrency, timestamp);
            if (!rateResult) {
                return std::unexpected(rateResult.error());
            }

            // 执行转换
            Decimal convertedAmount = money.amount() * rateResult->getRate();
            converted.emplace_back(convertedAmount, baseCurrency);
        }

        return converted;
    }
};
```

**降级策略规约**

1. **优先级**：直接汇率 > 逆向汇率 > 三角折算 > 历史汇率降级
2. **失败处理**：
   - 外部 API 失败时，自动使用数据库最新历史汇率
   - 数据库也无汇率时，返回明确错误，不使用默认值（如 1.0）
   - 记录降级事件到审计日志
3. **告警触发**：
   - 外部 API 连续失败 3 次，触发告警通知管理员
   - 使用历史汇率超过 24 小时，触发汇率过期告警
4. **透明度**：
   - 前端展示汇率时，必须显示汇率来源和时间戳
   - 使用历史汇率时，前端应提示"使用历史汇率"

````

---

## 4. 基础设施层设计 (Infrastructure Layer)

基础设施层负责真正的 HTTP 请求和 PostgreSQL 读写。

### 4.2 外部提供方实现 (Drogon HTTP Client)

这里以抓取 OpenExchangeRates (JSON 格式) 为例，展示防腐层的落地：隔离 JSON 结构，将其转换为纯净的 `ExchangeRate` 对象。

**汇率拉取策略**

只拉取系统中用户已添加的货币与 USD 的汇率对，避免拉取无用数据：

1. 查询 `currencies` 表，获取所有 `is_enabled = true` 的货币代码
2. 过滤掉 USD 本身
3. 从外部 API 拉取所有汇率后，只保留系统支持的货币
4. 所有汇率以 USD 为 `base_currency_code` 存储

```cpp
// infrastructure/external/OpenExchangeRatesProvider.cpp
#include "domain/gateways/IExchangeRateProvider.hpp"
#include <drogon/HttpClient.h>
#include <json/json.h>

class OpenExchangeRatesProvider : public IExchangeRateProvider {
private:
    std::string apiKey_;
    std::string baseUrl_;
    std::shared_ptr<ICurrencyRepository> currencyRepo_; // 用于查询系统支持的货币

public:
    OpenExchangeRatesProvider(
        std::string apiKey,
        std::shared_ptr<ICurrencyRepository> currencyRepo)
        : apiKey_(std::move(apiKey)),
          baseUrl_("https://openexchangerates.org/api/"),
          currencyRepo_(currencyRepo) {}

    std::string getProviderName() const override { return "OpenExchangeRates"; }

    std::expected<std::vector<ExchangeRate>, ProviderError> fetchLatestRates() override {
        // 1. 查询系统支持的货币列表
        auto supportedCurrenciesResult = currencyRepo_->findAllEnabled();
        if (!supportedCurrenciesResult) {
            return std::unexpected(ProviderError::ConfigurationError);
        }

        auto supportedCurrencies = *supportedCurrenciesResult;

        // 2. 构建货币白名单（排除 USD 本身）
        std::unordered_set<std::string> targetCurrencies;
        for (const auto& currency : supportedCurrencies) {
            if (currency.getCode() != "USD") {
                targetCurrencies.insert(currency.getCode());
            }
        }

        if (targetCurrencies.empty()) {
            return {}; // 系统只支持 USD，无需拉取汇率
        }

        // 3. 调用外部 API
        auto client = drogon::HttpClient::newHttpClient(baseUrl_);
        auto req = drogon::HttpRequest::newHttpRequest();

        // OpenExchangeRates 默认返回所有货币，也可以通过 symbols 参数过滤
        // 例如：latest.json?app_id=xxx&symbols=CNY,EUR,JPY
        std::string symbols = joinCurrencies(targetCurrencies);
        req->setPath("latest.json?app_id=" + apiKey_ + "&symbols=" + symbols);
        req->setMethod(drogon::Get);

        // 同步请求（实际生产可用协程或异步回调）
        auto [reqResult, response] = client->sendRequest(req);

        if (reqResult != drogon::ReqResult::Ok || response->getStatusCode() != 200) {
            LOG_ERROR << "Failed to fetch rates from OpenExchangeRates: "
                      << static_cast<int>(reqResult);
            return std::unexpected(ProviderError::NetworkFailure);
        }

        // 4. 解析 JSON
        auto jsonPtr = response->getJsonObject();
        if (!jsonPtr) {
            return std::unexpected(ProviderError::InvalidResponse);
        }

        const auto& json = *jsonPtr;

        // OpenExchangeRates 响应格式：
        // {
        //   "disclaimer": "...",
        //   "license": "...",
        //   "timestamp": 1719302400,
        //   "base": "USD",
        //   "rates": {
        //     "CNY": 7.18,
        //     "EUR": 0.92,
        //     "JPY": 149.50
        //   }
        // }

        if (!json.isMember("rates") || !json["rates"].isObject()) {
            return std::unexpected(ProviderError::InvalidResponse);
        }

        // 5. 转换为领域对象
        std::vector<ExchangeRate> domainRates;
        auto now = std::chrono::system_clock::now();

        // 如果 API 返回了时间戳，优先使用
        if (json.isMember("timestamp") && json["timestamp"].isInt64()) {
            auto timestamp = json["timestamp"].asInt64();
            now = std::chrono::system_clock::from_time_t(timestamp);
        }

        Currency pivot("USD");
        const auto& ratesJson = json["rates"];

        for (auto it = ratesJson.begin(); it != ratesJson.end(); ++it) {
            std::string targetCode = it.name();

            // 只保留系统支持的货币
            if (targetCurrencies.find(targetCode) == targetCurrencies.end()) {
                continue;
            }

            // 处理汇率值（可能是 number 或 string）
            Decimal rateVal;
            if (it->isNumeric()) {
                rateVal = Decimal(it->asDouble()); // 从 double 转换时需小心精度
            } else if (it->isString()) {
                rateVal = Decimal(it->asString());
            } else {
                LOG_WARN << "Invalid rate format for " << targetCode << ", skipping";
                continue;
            }

            domainRates.emplace_back(
                pivot,                  // base: USD
                Currency(targetCode),   // target: CNY/EUR/JPY...
                rateVal,
                now,
                getProviderName()
            );
        }

        LOG_INFO << "Fetched " << domainRates.size() << " exchange rates from OpenExchangeRates";
        return domainRates;
    }

private:
    // 辅助方法：将货币集合拼接为逗号分隔字符串
    std::string joinCurrencies(const std::unordered_set<std::string>& currencies) {
        std::string result;
        for (const auto& code : currencies) {
            if (!result.empty()) result += ",";
            result += code;
        }
        return result;
    }
};

````

**汇率存储规约**

所有外部汇率以 **USD 为 base_currency_code** 存储到数据库：

```sql
INSERT INTO exchange_rates (base_currency_code, target_currency_code, rate, source, fetched_at)
VALUES
  ('USD', 'CNY', 7.18, 'OpenExchangeRates', '2026-06-25 08:00:00'),
  ('USD', 'EUR', 0.92, 'OpenExchangeRates', '2026-06-25 08:00:00'),
  ('USD', 'JPY', 149.50, 'OpenExchangeRates', '2026-06-25 08:00:00');
```

**降级策略**

如果外部 API 完全不可用，应用层应使用数据库中最新的历史汇率作为降级方案，并触发告警通知管理员。

### 4.2 PG 仓储实现 (Repository)

利用 `ORDER BY fetched_at DESC LIMIT 1` 来获取特定时间点的有效汇率。

```cpp
// infrastructure/persistence/ExchangeRateRepositoryImpl.cpp
#include "domain/repositories/IExchangeRateRepository.hpp"
#include <drogon/drogon.h>

class ExchangeRateRepositoryImpl : public IExchangeRateRepository {
private:
    drogon::orm::DbClientPtr dbClient_;

public:
    ExchangeRateRepositoryImpl(drogon::orm::DbClientPtr dbClient) : dbClient_(dbClient) {}

    std::expected<void, RepositoryError> save(const std::vector<ExchangeRate>& rates) override {
        try {
            auto trans = dbClient_->newTransaction();
            // 采用批量插入或循环插入 (Append Only)
            for (const auto& rate : rates) {
                trans->execSqlSync(
                    "INSERT INTO exchange_rates (base_currency_code, target_currency_code, rate, source, fetched_at) "
                    "VALUES ($1, $2, $3, $4, $5)",
                    rate.getBase().getCode(),
                    rate.getTarget().getCode(),
                    rate.getRate().to_string(), // Decimal 转回高精度字符串入库
                    rate.providerName_,
                    // C++ chrono 转换为 PG timestamp...
                );
            }
            return {};
        } catch (...) {
            return std::unexpected(RepositoryError{RepositoryStatus::DatabaseError, "Batch insert failed"});
        }
    }

    std::expected<ExchangeRate, RepositoryError> getHistorical(
        const Currency& base, const Currency& target,
        std::chrono::system_clock::time_point timestamp) override
    {
        // SQL 逻辑：查找指定时间点之前，最新的一条记录
        auto result = dbClient_->execSqlSync(
            "SELECT * FROM exchange_rates "
            "WHERE base_currency_code = $1 AND target_currency_code = $2 AND fetched_at <= $3 "
            "ORDER BY fetched_at DESC LIMIT 1",
            base.getCode(), target.getCode(), timestamp // timestamp 需序列化为字面量
        );

        if (result.empty()) {
            return std::unexpected(RepositoryError{RepositoryStatus::NotFound, "No rate found for timeline"});
        }

        // 返回 mapped Domain 对象...
    }
};

```

---

## 5. 调度器接入 (Scheduler)

后台作业直接挂载在 Drogon 的 Event Loop（事件循环）上，避免引入额外沉重的任务队列中间件。模块放在 `scheduler/` 目录下。

```cpp
// scheduler/ExchangeRateJob.cpp
#pragma once
#include <drogon/drogon.h>
#include "application/use_cases/RefreshExchangeRatesUseCase.hpp"

class ExchangeRateJob {
private:
    std::shared_ptr<RefreshExchangeRatesUseCase> useCase_;
    drogon::TimerId timerId_;

public:
    ExchangeRateJob(std::shared_ptr<RefreshExchangeRatesUseCase> useCase)
        : useCase_(useCase) {}

    void startScheduling() {
        // 使用 Drogon 的主事件循环创建一个定时器，每 12 小时执行一次 (12 * 3600 秒)
        timerId_ = drogon::app().getLoop()->runEvery(12.0 * 3600.0, [this]() {
            LOG_INFO << "Starting scheduled exchange rate refresh...";
            auto result = useCase_->execute();
            if (!result) {
                LOG_ERROR << "Exchange rate refresh failed: " << result.error();
            } else {
                LOG_INFO << "Exchange rate refresh completed successfully.";
            }
        });
    }

    void stop() {
        drogon::app().getLoop()->invalidateTimer(timerId_);
    }
};

```

---

## 6. 特别注意：汇率换算的精度损失与补偿

由于汇率常常存在除不尽的情况（如 100 USD / 7.123），在应用层或报表层调用 `CurrencyConversionService` 进行换算时，必须**延迟舍入**。

- **中间过程存储**：系统所有的 `ExchangeRate` 对象中的 `Decimal` 推荐保持 **至少 10 位有效小数**。
- **持久化**：汇率表 `exchange_rates` 的 `rate` 字段被设定为 `NUMERIC(20,10)`。
- **计算边界**：只有在最终生成 `Transaction` 的金额，或者展示前端报表时，才根据 `TransferMode` 或前端需求进行业务舍入（如银行家舍入保留 2 位到 8 位）。
