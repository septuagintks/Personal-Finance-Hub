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

    // 抓取相对于枢纽货币（如 USD）的指定活跃币种最新汇率
    virtual std::expected<std::vector<ExchangeRate>, ProviderError> fetchLatestRates(
        const std::vector<Currency>& targetCurrencies) = 0;
};

```

### 2.3 货币转换领域服务与三角折算

**设计原则：以 USD 为枢纽货币（Pivot Currency）**

外部汇率 API（如 OpenExchangeRates、ECB）通常只提供相对于单一基准货币（如 USD）的汇率对。PFH 采用以下策略：

1. **汇率拉取策略**：仅拉取用户系统中已添加货币与 USD 之间的汇率
2. **枢纽货币固定为 USD**：所有汇率以 USD 为基准存储
3. **非 USD 货币对通过三角折算推导**：例如 EUR → CNY 通过 EUR→USD→CNY 计算

**三角折算公式**

已知数据库只保存 USD 枢纽方向：

- `USD → EUR` 汇率为 `r_base`（例如 1 USD = 0.92 EUR）
- `USD → CNY` 汇率为 `r_target`（例如 1 USD = 7.18 CNY）

推导 `EUR → CNY`：

```text
1 EUR = (1 / r_base) USD
1 EUR = (1 / r_base) × r_target CNY
因此 EUR → CNY 汇率 = r_target / r_base
```

推导 `CNY → EUR`（逆向）：

```text
CNY → EUR = r_base / r_target
```

当数据库中只有 `USD -> CNY` 和 `USD -> EUR`，而业务需要 `EUR -> CNY` 时，由应用层先读取所需的 `ExchangeRate`，再交给 `CurrencyConversionService` 在纯内存中推导。
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
    // 逆向折算：已知 Base->Target，推导 Target->Base
    // 例如：USD->CNY = 7.18，推导 CNY->USD = 1 / 7.18
    static std::expected<ExchangeRate, CurrencyConversionError> calculateInverseRate(
        const ExchangeRate& rate)
    {
        if (rate.getRate().isZeroOrNegative()) {
            return std::unexpected(CurrencyConversionError::InfiniteOrZeroRate);
        }

        return rate.inverse();
    }

    // 三角折算：已知 Pivot->Base 和 Pivot->Target，推导 Base->Target
    // 例如：USD->EUR 和 USD->CNY，推导 EUR->CNY
    static std::expected<ExchangeRate, CurrencyConversionError> calculateCrossRate(
        const ExchangeRate& pivotToBase,
        const ExchangeRate& pivotToTarget)
    {
        // 验证两个汇率都以相同的枢纽货币为 base
        if (pivotToBase.getBase().getCode() != PIVOT_CURRENCY ||
            pivotToTarget.getBase().getCode() != PIVOT_CURRENCY) {
            return std::unexpected(CurrencyConversionError::MissingPivotRate);
        }

        // USD->EUR = 0.92, USD->CNY = 7.18
        // EUR->CNY = USD->CNY / USD->EUR = 7.18 / 0.92 ≈ 7.80
        Decimal crossRate = pivotToTarget.getRate() / pivotToBase.getRate();

        if (crossRate.isZeroOrNegative()) {
            return std::unexpected(CurrencyConversionError::InfiniteOrZeroRate);
        }

        return ExchangeRate(
            pivotToBase.getTarget(),
            pivotToTarget.getTarget(),
            crossRate,
            std::max(pivotToBase.getFetchedAt(), pivotToTarget.getFetchedAt()), // 使用较晚的时间戳
            "TriangularCalculation"
        );
    }
};

```

**使用示例**

```cpp
// 应用层 Use Case 中已经从 Repository 读取 USD->EUR 和 USD->CNY
auto rateResult = CurrencyConversionService::calculateCrossRate(
    usdToEur,
    usdToCny
);

if (!rateResult) {
    return std::unexpected(UseCaseError::ExchangeRateNotAvailable);
}

// 得到推导出的 EUR->CNY 汇率
ExchangeRate eurToCny = *rateResult;
```

**数据库存储策略**

数据库中只存储与 USD 的直接汇率对：

```text
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
    std::shared_ptr<IAccountRepository> accountRepository_;
    std::shared_ptr<IAuditLogRepository> auditRepo_;

public:
    RefreshExchangeRatesUseCase(
        std::shared_ptr<IExchangeRateProvider> provider,
        std::shared_ptr<IExchangeRateRepository> repository,
        std::shared_ptr<IAccountRepository> accountRepository,
        std::shared_ptr<IAuditLogRepository> auditRepo)
        : provider_(provider), repository_(repository), accountRepository_(accountRepository), auditRepo_(auditRepo) {}

    std::expected<void, std::string> execute() {
        LOG_INFO << "Starting exchange rate refresh from " << provider_->getProviderName();

        // 0. 先收集系统中所有未归档账户正在使用的币种，调度器本身不携带 UserId
        auto activeCurrenciesResult = accountRepository_->findActiveCurrencies();
        if (!activeCurrenciesResult) {
            std::string errorMsg = "Failed to collect active currencies";
            LOG_ERROR << errorMsg;
            return std::unexpected(errorMsg);
        }

        const auto& activeCurrencies = activeCurrenciesResult.value();
        if (activeCurrencies.empty()) {
            LOG_WARN << "No active currencies found, skipping exchange rate refresh";
            return {};
        }

        // 1. 调用基础设施层请求外部 API
        auto fetchedRatesResult = provider_->fetchLatestRates(activeCurrencies);
        if (!fetchedRatesResult) {
            std::string errorMsg = "Failed to fetch rates from " + provider_->getProviderName();
            LOG_ERROR << errorMsg;

            // 写入审计日志记录失败
            auditRepo_->log(AuditAction::RefreshExchangeRate, "ExchangeRate", "bulk",
                           std::nullopt, std::nullopt,
                           {{"status", "failed"}, {"provider", provider_->getProviderName()}, {"currency_count", activeCurrencies.size()}});

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
                        {"provider", provider_->getProviderName()},
                        {"currency_count", activeCurrencies.size()}});

        LOG_INFO << "Successfully refreshed " << rates.size() << " exchange rates";
        return {};
    }
};

```

### 3.2 汇率查询与降级策略

当外部 API 完全不可用时，应用层应能够使用数据库中的历史汇率作为降级方案。

#### 3.2.1 多级降级查询链 (Fallback Chain)

在应用层 `ExchangeRateQueryService` 中，汇率查询采用责任链模式进行降级；`CurrencyConversionService` 只负责逆向折算、三角折算等纯内存计算：

1. **直接汇率**：尝试直接查询 Base -> Target。
2. **逆向汇率**：尝试查询逆向汇率 Target -> Base，然后取倒数。
3. **三角折算**：通过 USD 枢纽推导。
4. **历史降级**：寻找历史上最接近（但在此时间点之前）的汇率记录。
5. **返回明确错误**：若以上均不可得，返回 `ExchangeRateNotAvailable` 类应用层错误。

#### 3.2.2 熔断与告警机制

- **断路器 (Circuit Breaker)**：如果外部 API 连续请求失败超过 3 次，断路器打开（进入 `Open` 状态）。在接下来的 1 小时内，调度任务不再请求外部 API，直接使用本地历史汇率，避免阻塞系统线程。
- **断路器状态机设计**：
  - **Closed（闭合）**：正常状态，所有请求直接发送给外部 API。如果连续失败 3 次，状态转移到 `Open`。
  - **Open（断开）**：熔断状态，所有请求直接拦截，不调用外部 API，直接降级使用本地数据库的历史汇率。1 小时后，状态转移到 `Half-Open`。
  - **Half-Open（半开）**：尝试状态，允许 1 次请求发送给外部 API。如果请求成功，断路器恢复到 `Closed` 状态；如果请求失败，断路器重新回到 `Open` 状态并重置 1 小时计时器。
- **L1 内存缓存（高频查询优化）**：
  - 在应用层 `ExchangeRateQueryService` 内部维护一个轻量级的进程内 L1 缓存：`std::unordered_map<std::string, ExchangeRate>`，其中 Key 为 `base_currency + "_" + target_currency`。
  - 每次高频查询（如报表生成、净资产折算）时，优先命中 L1 缓存。若未命中，再查询数据库（L2 缓存）并回填 L1。
  - L1 缓存的生命周期与单次请求上下文绑定，或设置 5 分钟的滑动过期时间，防止内存无限膨胀和数据严重滞后。
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

    std::expected<ExchangeRate, std::string> readRate(
        const Currency& base,
        const Currency& target,
        std::optional<std::chrono::system_clock::time_point> timestamp)
    {
        auto result = timestamp
            ? repository_->getHistorical(base, target, *timestamp)
            : repository_->getLatest(base, target);

        if (!result) {
            return std::unexpected("Exchange rate not found");
        }

        return *result;
    }

public:
    explicit ExchangeRateQueryService(std::shared_ptr<IExchangeRateRepository> repository)
        : repository_(repository) {}

    // 获取汇率，支持三角折算和历史查询
    std::expected<ExchangeRate, std::string> getRate(
        const Currency& base,
        const Currency& target,
        std::optional<std::chrono::system_clock::time_point> timestamp = std::nullopt)
    {
        // 1. 直接查询 Base -> Target
        if (auto directRate = readRate(base, target, timestamp)) {
            return directRate;
        }

        // 2. 查询逆向 Target -> Base，由 Domain Service 纯计算倒数
        if (auto inverseRate = readRate(target, base, timestamp)) {
            auto calculated = CurrencyConversionService::calculateInverseRate(*inverseRate);
            if (calculated) {
                return calculated;
            }
        }

        // 3. 查询 USD 枢纽两侧汇率，由 Domain Service 纯计算交叉汇率
        Currency pivot("USD");
        if (base != pivot && target != pivot) {
            auto pivotToBase = readRate(pivot, base, timestamp);
            auto pivotToTarget = readRate(pivot, target, timestamp);

            if (pivotToBase && pivotToTarget) {
                auto calculated = CurrencyConversionService::calculateCrossRate(
                    *pivotToBase,
                    *pivotToTarget
                );

                if (calculated) {
                    return calculated;
                }
            }
        }

        return std::unexpected("Exchange rate not available for currency pair");
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

---

## 4. 基础设施层设计 (Infrastructure Layer)

基础设施层负责真正的 HTTP 请求和 PostgreSQL 读写。

### 4.1 外部提供方实现 (Drogon HTTP Client)

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

    std::expected<std::vector<ExchangeRate>, ProviderError> fetchLatestRates(
        const std::vector<Currency>& targetCurrencies) override {
        // 1. 构建货币白名单（排除 USD 本身）
        std::unordered_set<std::string> filteredTargets;
        for (const auto& currency : targetCurrencies) {
            if (currency.getCode() != "USD") {
                filteredTargets.insert(currency.getCode());
            }
        }

        if (filteredTargets.empty()) {
            return {}; // 系统只支持 USD，无需拉取汇率
        }

        // 3. 调用外部 API
        auto client = drogon::HttpClient::newHttpClient(baseUrl_);
        auto req = drogon::HttpRequest::newHttpRequest();

        // OpenExchangeRates 默认返回所有货币，也可以通过 symbols 参数过滤
        // 例如：latest.json?app_id=xxx&symbols=CNY,EUR,JPY
        std::string symbols = joinCurrencies(filteredTargets);
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
            if (!filteredTargets.contains(targetCode)) {
                continue;
            }

            // 处理汇率值。Provider 适配层必须转成十进制字符串，禁止经由 double。
            Decimal rateVal;
            if (it->isString()) {
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

```

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
    std::shared_ptr<IBackgroundExecutor> backgroundExecutor_;
    drogon::TimerId timerId_;

public:
    ExchangeRateJob(
        std::shared_ptr<RefreshExchangeRatesUseCase> useCase,
        std::shared_ptr<IBackgroundExecutor> backgroundExecutor)
        : useCase_(useCase), backgroundExecutor_(backgroundExecutor) {}

    void startScheduling() {
        // 使用 Drogon 的主事件循环创建一个定时器，每 12 小时执行一次 (12 * 3600 秒)
        timerId_ = drogon::app().getLoop()->runEvery(12.0 * 3600.0, [this]() {
            // Event Loop 只负责触发，实际网络 I/O 与数据库 I/O 进入后台执行器。
            backgroundExecutor_->submit([useCase = useCase_]() {
                LOG_INFO << "Starting scheduled exchange rate refresh...";
                auto result = useCase->execute();
                if (!result) {
                    LOG_ERROR << "Exchange rate refresh failed: " << result.error();
                } else {
                    LOG_INFO << "Exchange rate refresh completed successfully.";
                }
            });
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
