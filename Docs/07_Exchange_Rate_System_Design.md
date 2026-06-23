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

### 2.3 货币转换领域服务

当数据库中只有 `USD->CNY` 和 `USD->EUR`，而业务需要 `EUR->CNY` 时，由 `CurrencyConversionService` 负责在纯内存中推导。
不要定义单独的 `ExchangeRateService`，避免和应用层汇率刷新用例、基础设施 Provider 混在一起。

```cpp
// domain/services/CurrencyConversionService.hpp
#pragma once
#include <expected>
#include "domain/value_objects/ExchangeRate.hpp"

enum class CurrencyConversionError { MissingPivotRate, InfiniteOrZeroRate };

class CurrencyConversionService {
public:
    // 已知 Base->Pivot 和 Target->Pivot，推导出 Base->Target
    static std::expected<ExchangeRate, CurrencyConversionError> calculateCrossRate(
        const ExchangeRate& baseToPivot,
        const ExchangeRate& targetToPivot)
    {
        if (baseToPivot.getTarget() != targetToPivot.getTarget()) {
            return std::unexpected(CurrencyConversionError::MissingPivotRate);
        }

        Decimal crossRate = baseToPivot.getRate() / targetToPivot.getRate();

        return ExchangeRate(
            baseToPivot.getBase(),
            targetToPivot.getBase(),
            crossRate,
            baseToPivot.fetchedAt_, // 以较早的时间戳或当前推导时间为准
            "Calculated_Cross"
        );
    }
};

```

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

public:
    RefreshExchangeRatesUseCase(
        std::shared_ptr<IExchangeRateProvider> provider,
        std::shared_ptr<IExchangeRateRepository> repository)
        : provider_(provider), repository_(repository) {}

    std::expected<void, std::string> execute() {
        // 1. 调用基础设施层请求外部 API
        auto fetchedRatesResult = provider_->fetchLatestRates();
        if (!fetchedRatesResult) {
            return std::unexpected("Failed to fetch rates from external provider");
        }

        auto& rates = fetchedRatesResult.value();
        if (rates.empty()) return {}; // 无数据则跳过

        // 2. 将数据以 Append-Only 模式持久化到数据库
        auto saveResult = repository_->save(rates);
        if (!saveResult) {
            return std::unexpected("Failed to persist new exchange rates to database");
        }

        return {};
    }
};

```

---

## 4. 基础设施层设计 (Infrastructure Layer)

基础设施层负责真正的 HTTP 请求和 PostgreSQL 读写。

### 4.1 外部提供方实现 (Drogon HTTP Client)

这里以抓取 OpenExchangeRates (JSON 格式) 为例，展示防腐层的落地：隔离 JSON 结构，将其转换为纯净的 `ExchangeRate` 对象。

```cpp
// infrastructure/external/OpenExchangeRatesProvider.cpp
#include "domain/gateways/IExchangeRateProvider.hpp"
#include <drogon/HttpClient.h>
#include <json/json.h>

class OpenExchangeRatesProvider : public IExchangeRateProvider {
private:
    std::string apiKey_;
    std::string baseUrl_;

public:
    OpenExchangeRatesProvider(std::string apiKey)
        : apiKey_(std::move(apiKey)), baseUrl_("https://openexchangerates.org/api/") {}

    std::string getProviderName() const override { return "OpenExchangeRates"; }

    std::expected<std::vector<ExchangeRate>, ProviderError> fetchLatestRates() override {
        auto client = drogon::HttpClient::newHttpClient(baseUrl_);
        auto req = drogon::HttpRequest::newHttpRequest();
        req->setPath("latest.json?app_id=" + apiKey_);
        req->setMethod(drogon::Get);

        // 使用同步或协程等待响应 (演示使用简化的同步方式)
        auto [reqResult, response] = client->sendRequest(req);

        if (reqResult != drogon::ReqResult::Ok || response->getStatusCode() != 200) {
            return std::unexpected(ProviderError::NetworkFailure);
        }

        auto jsonPtr = response->getJsonObject();
        if (!jsonPtr) return std::unexpected(ProviderError::InvalidResponse);

        std::vector<ExchangeRate> domainRates;
        auto now = std::chrono::system_clock::now(); // 或者使用 JSON 返回的 timestamp
        Currency pivot("USD");

        // 解析 JSON {"rates": {"CNY": 7.18, "EUR": 0.92}}
        const auto& ratesJson = (*jsonPtr)["rates"];
        for (auto it = ratesJson.begin(); it != ratesJson.end(); ++it) {
            std::string targetCode = it.name();
            Decimal rateVal(it->asString()); // 转为定点数

            domainRates.emplace_back(
                pivot,
                Currency(targetCode),
                rateVal,
                now,
                getProviderName()
            );
        }

        return domainRates;
    }
};

```

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
