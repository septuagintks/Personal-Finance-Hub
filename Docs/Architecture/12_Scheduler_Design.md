# Personal Finance Hub - Scheduler & Background Task Design

Version: 1.1

Backend: C++23

Framework: Drogon (Event Loop)

Architecture: Clean Architecture (Entry Point)

---

## 1. 架构定位：Scheduler 是另一种“控制器”

在 Clean Architecture 中，前端发起的 HTTP 请求由 `Controller` 接收，并调用 `Use Case`；而时间到达触发的定时任务，则由 `Job` 接收，同样调用 `Use Case`。

**核心原则：**

1. **单一入口**：Job 只是一个触发器（Entry Point），它绝对不能包含任何业务逻辑，必须把所有计算委托给 Application 层的 Use Case。
2. **非阻塞（Non-blocking）**：后台任务运行在 Drogon 的 Event Loop 或其线程池中，绝不能使用 `std::this_thread::sleep_for` 或死循环，否则会阻塞处理 HTTP 请求的线程。
3. **隔离依赖**：为了当前阶段（无 Redis）的纯粹性，我们不引入 Celery 或 RabbitMQ 等沉重的外部任务队列。

```text
[Time Trigger] (Drogon Event Loop)
      │
      ▼
[Scheduler Module] (Job Classes) ── (依赖注入) ──▶ [Application Layer]
      │                                                (Use Cases)
      ▼
[Infrastructure Layer] (如需加分布式锁)

```

---

## 2. 核心调度框架设计

为了统一管理所有定时任务（启动、停止、监控），我们需要在 `scheduler/` 目录下定义标准的接口与注册表。

### 2.1 任务基础接口 (IJob)

所有的后台作业必须实现该接口，方便调度器统一管理生命周期。

```cpp
// scheduler/IJob.hpp
#pragma once
#include <string>

class IJob {
public:
    virtual ~IJob() = default;

    virtual std::string getJobName() const = 0;

    // 启动该任务的定时器
    virtual void start() = 0;

    // 停止该任务
    virtual void stop() = 0;

    // 立即手动执行一次（常用于测试或手动触发）
    virtual void triggerNow() = 0;
};

```

### 2.2 任务管理器 (JobManager)

负责在系统启动时挂载所有的 Job，并在系统关闭时优雅退出。

```cpp
// scheduler/JobManager.hpp
#pragma once
#include <vector>
#include <memory>
#include "scheduler/IJob.hpp"

class JobManager {
private:
    std::vector<std::shared_ptr<IJob>> jobs_;

public:
    void registerJob(std::shared_ptr<IJob> job) {
        jobs_.push_back(std::move(job));
    }

    void startAll() {
        for (auto& job : jobs_) {
            job->start();
            LOG_INFO << "Job started: " << job->getJobName();
        }
    }

    void stopAll() {
        for (auto& job : jobs_) {
            job->stop();
            LOG_INFO << "Job stopped: " << job->getJobName();
        }
    }
};

```

### 2.3 后台执行器 (IBackgroundExecutor)

定时器回调必须保持轻量。任何网络 I/O、数据库 I/O 或 CPU 密集型任务都通过后台执行器执行。

```cpp
// scheduler/IBackgroundExecutor.hpp
#pragma once
#include <functional>

class IBackgroundExecutor {
public:
    virtual ~IBackgroundExecutor() = default;
    virtual bool submit(std::function<void()> task) = 0;
    virtual void shutdown() = 0;
};
```

### 2.4 Phase 1 规范实现

Phase 1 以以下类作为事实实现：

- `DrogonTimerScheduler`：只在 Event Loop 注册/取消 timer。
- `BoundedThreadPool`：固定 worker 数和队列容量；队列满或停止中时 `submit` 返回 `false`，不得无界增长。
- `RecurringJob`：统一处理本机防重入、入队失败、异常边界、任务 ID、执行耗时、软超时和可选分布式租约。
- `JobManager`：拒绝重名 Job；启动任一失败时停止已启动项；关闭时先取消 timer、等待已接受任务，再关闭 worker pool。

`job_execution_timeout` 是**软期限**。C++ 不安全地强杀工作线程会破坏事务和对象生命周期，因此超时后记录 warning，不强制终止；HTTP Provider 另有硬请求超时，Outbox processing timeout 与任务租约都必须长于软期限。Event Loop callback 只允许执行本机状态检查和一次有界 `submit`，不得直接调用 Use Case、HTTP 或 `execSqlSync`。Executor、lease adapter 或日志边界出现异常时，Job 必须收束异常并清除本机 `running` 状态，不能终止 worker 或永久阻止后续调度。

Phase 1 不实现 lease heartbeat。若任务异常慢并超过 `lease_duration`，其他实例可在旧任务仍未返回时接管，因此 Job 必须可重复执行，lease 时长必须覆盖正常最坏执行时间并留出裕量。当前汇率 append 允许出现语义等价的重复快照，认证清理天然幂等；长任务重叠、进程重启和租约恢复必须在 P1-S12 真实 PostgreSQL runtime 中验证。若后续引入不可重复副作用或长任务，应先增加 token-guarded lease renew，不能只继续放大软超时。

---

## 3. 具体后台任务实现案例

以下代码用于说明 Job 作为入口调用 Use Case 的关系；生命周期、入队、租约和异常处理以 2.4 节的 `RecurringJob` 规范实现为准，不在每个 Job 内重复手写。

### 3.1 每日汇率刷新任务 (Exchange Rate Sync)

承接《08_Exchange_Rate_System_Design.md》，每天执行一次汇率拉取。
这个 Job 不携带任何用户上下文；`RefreshExchangeRatesUseCase` 通过 Application 的 `IActiveCurrencyQuery` 系统查询端口合并所有未归档账户币种与用户报表基准币种，再把币种集合传给汇率 Provider。PostgreSQL 实现使用独立后台只读连接，不复用 request-scoped `AccountRepository` 或普通请求数据库角色。

权限边界只对跨租户读取例外：`PostgresActiveCurrencyQuery` 使用 BYPASSRLS + default-read-only client；汇率 append、Outbox、AuditLog、token 清理和 scheduled lease 都使用普通 request-role client 在无租户事务中访问非 RLS 表。后台特权 client 不得注入这些写 adapter。

```cpp
// scheduler/jobs/ExchangeRateSyncJob.cpp
#include "scheduler/IJob.hpp"
#include "application/use_cases/RefreshExchangeRatesUseCase.hpp"
#include <drogon/drogon.h>

class ExchangeRateSyncJob : public IJob {
private:
    std::shared_ptr<RefreshExchangeRatesUseCase> useCase_;
    std::shared_ptr<IBackgroundExecutor> backgroundExecutor_;
    drogon::TimerId timerId_;
    double intervalSeconds_ = 12.0 * 3600.0; // 12小时

public:
    ExchangeRateSyncJob(
        std::shared_ptr<RefreshExchangeRatesUseCase> useCase,
        std::shared_ptr<IBackgroundExecutor> backgroundExecutor)
        : useCase_(useCase),
          backgroundExecutor_(backgroundExecutor),
          timerId_(drogon::kInvalidTimerId) {}

    std::string getJobName() const override { return "ExchangeRateSyncJob"; }

    void start() override {
        // 使用 Drogon 主事件循环
        timerId_ = drogon::app().getLoop()->runEvery(intervalSeconds_, [this]() {
            this->triggerNow();
        });
    }

    void stop() override {
        if (timerId_ != drogon::kInvalidTimerId) {
            drogon::app().getLoop()->invalidateTimer(timerId_);
            timerId_ = drogon::kInvalidTimerId;
        }
    }

    void triggerNow() override {
        backgroundExecutor_->submit([useCase = useCase_, jobName = getJobName()]() {
            LOG_INFO << "Executing " << jobName << "...";
            auto result = useCase->execute();

            if (!result) {
                LOG_ERROR << jobName << " failed: " << result.error();
                // 在这里可以接入邮件预警系统或告警模块
            } else {
                LOG_INFO << jobName << " completed successfully.";
            }
        });
    }
};

```

### 3.2 僵尸数据清理任务 (Data Cleanup)

Phase 1 自动清理 `refresh_tokens`、`revoked_access_tokens` 和 `revoked_sessions` 中 `expires_at <= NOW()` 的记录。PostgreSQL 以数据库时钟判断过期，避免应用主机时钟超前而提前删除仍应生效的撤销记录；三张表在同一无租户事务中各按配置的 batch limit 使用 `FOR UPDATE SKIP LOCKED` 删除。软删除流水清理不属于 Phase 1。

```cpp
// scheduler/jobs/DataCleanupJob.cpp
// 每周日凌晨运行一次，调用 CleanupUseCase，执行物理删除 DELETE 操作。
// ... (代码结构同上，间隔设为 7 * 24 * 3600，且可以利用 runAt 设置特定时间点) ...

```

### 3.3 Outbox 投递任务 (OutboxPublisherJob)

负责从 `domain_events_outbox` 读取已提交但尚未投递的事件，并通过 `IEventBus` 分发给本地处理器。

Phase 1 的 Job 不直接拼 SQL 或操作 EventBus，而是调用 `OutboxPublisher::run_once(worker_id)`；Publisher 负责 claim、逐条同步等待 handler、按 claim token 完成状态转换以及重试未完成的 dead-letter 审计。下方早期骨架只说明定时入口，不是当前状态机实现。

```cpp
// scheduler/jobs/OutboxPublisherJob.cpp
#include "scheduler/IJob.hpp"
#include "application/events/IEventBus.hpp"

class OutboxPublisherJob : public IJob {
private:
    std::shared_ptr<IEventBus> eventBus_;
    drogon::TimerId timerId_;
    double intervalSeconds_ = 10.0;

public:
    OutboxPublisherJob(std::shared_ptr<IEventBus> eventBus)
        : eventBus_(eventBus), timerId_(drogon::kInvalidTimerId) {}

    std::string getJobName() const override { return "OutboxPublisherJob"; }

    void start() override {
        timerId_ = drogon::app().getLoop()->runEvery(intervalSeconds_, [this]() {
            this->triggerNow();
        });
    }

    void stop() override {
        if (timerId_ != drogon::kInvalidTimerId) {
            drogon::app().getLoop()->invalidateTimer(timerId_);
            timerId_ = drogon::kInvalidTimerId;
        }
    }

    void triggerNow() override {
        // 1. 使用 FOR UPDATE SKIP LOCKED / advisory lock claim 待处理 outbox 记录
        // 2. 反序列化 payload -> IDomainEvent
        // 3. 调用 eventBus_->publish(event)
        // 4. 成功后标记 published，失败后递增 retry_count 并记录 last_error
    }
};
```

**并发规则：**

1. 多实例部署时，必须使用 `pg_try_advisory_lock` 或 `FOR UPDATE SKIP LOCKED` 避免重复投递
2. 处理成功后再标记 `published`
3. 处理失败后更新 `retry_count`、`next_retry_at` 和 `last_error`
4. 超过最大重试次数后进入 `dead_letter`

---

## 4. 异常捕获与容错机制 (Resilience)

后台任务与 HTTP 请求不同，它们如果在执行过程中抛出未捕获的 C++ 异常，会导致整个 Drogon 进程崩溃宕机（Crash）。

**防护规范**：
在 `triggerNow()` 的实现中，虽然 Use Case 层面我们强制要求返回 `std::expected` 来避免业务异常，但为了防御基础设施层（如 JsonCpp 解析崩溃、第三方底层库抛出 `std::bad_alloc`），必须在 Job 的最外层加固。

```cpp
void triggerNow() override {
    backgroundExecutor_->submit([useCase = useCase_, jobName = getJobName()]() {
        try {
            auto result = useCase->execute();
            if (!result) {
                LOG_ERROR << jobName << " domain failure: " << result.error();
            }
        } catch (const std::exception& e) {
            LOG_FATAL << jobName << " crashed with standard exception: " << e.what();
        } catch (...) {
            LOG_FATAL << jobName << " crashed with unknown exception!";
        }
    });
}

```

---

## 5. 多实例部署下的并发执行冲突 (分布式锁)

**痛点**：如果我们为了负载均衡，部署了 3 个后台容器实例（Instance A, B, C）。当定时器时间一到，3 个实例都会触发 `ExchangeRateSyncJob`，导致同时向外部 API 发送 3 次请求，并往数据库写入 3 份重复的历史汇率。

**解决方案：基于 PostgreSQL 的轻量级悲观锁**
由于我们没有 Redis，我们需要利用 PostgreSQL 强大的原生功能来防止并发冲突。

### 5.1 未采用：PG 原生咨询锁 (Advisory Locks)

不需要建表，非常轻量。

```sql
-- 在 C++ 中执行 SQL 获取锁（假设锁 ID 为 1001 表示汇率任务）
SELECT pg_try_advisory_lock(1001);
-- 返回 true 说明拿到锁，继续执行任务；
-- 返回 false 说明别的实例正在执行，当前实例直接跳过本次执行。

-- 执行完毕后释放锁
SELECT pg_advisory_unlock(1001);

```

### 5.2 Phase 1 采用：带 token 的过期租约表

适用于需要记录“上次是谁执行的、执行了多久”的场景。

```sql
CREATE TABLE scheduled_job_leases (
    job_name VARCHAR(128) PRIMARY KEY,
    owner_id VARCHAR(128) NOT NULL,
    lease_token UUID NOT NULL,
    lease_until TIMESTAMPTZ NOT NULL,
    updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

```

汇率刷新和认证清理在执行前原子取得未过期租约；release 必须匹配
`job_name + owner_id + lease_token`，旧 owner 不能释放后来实例的新租约。Outbox
Publisher 不使用全局 Job lease，因为 `FOR UPDATE SKIP LOCKED` + 每行 claim token
允许多个实例安全并行消费。PostgreSQL adapter 必须以数据库 `NOW()` 作为 lease 获取、过期和释放的共享时钟，不能依赖各应用主机可能偏移的系统时钟。所有租约写入使用普通 request-role client 访问非 RLS 表。

---

## 6. 系统生命周期集成 (main.cpp)

在系统的入口，必须按正确的顺序初始化。Phase 1 的实际装配位于
`ProductionCompositionRoot`：先构造 request/background DbClient 和全部 Job 依赖，
再通过 Drogon beginning advice 调用 `JobManager::start_all()`。Drogon 1.8.7 没有
ending advice；服务退出后由 `ProductionCompositionRoot` 析构函数依次执行
`stop_all()` 和 `BoundedThreadPool::shutdown()`。启动任一 Job 失败时记录 critical
并退出服务。下面代码只保留生命周期顺序示意，类名和构造参数以实际实现为准。

```cpp
// main.cpp
#include <drogon/drogon.h>
#include "scheduler/JobManager.hpp"
// ... include 其他依赖

int main() {
    // 1. 初始化依赖注入 (Repositories, Providers, UseCases)
    auto primaryTransport = std::make_shared<DrogonHttpTransport>(
        "https://api.freecurrencyapi.com");
    auto fallbackTransport = std::make_shared<DrogonHttpTransport>(
        "https://api.exchangerate.fun");
    auto primary = std::make_shared<FreeCurrencyApiProvider>(
        *primaryTransport, *clock, std::move(config.exchange_rate.api_key));
    auto fallback = std::make_shared<ExchangeRateFunProvider>(
        *fallbackTransport, *clock);
    auto rateProvider = std::make_shared<FailoverExchangeRateProvider>(
        *primary, *fallback);
    auto rateRepo = std::make_shared<ExchangeRateRepositoryImpl>(drogon::app().getDbClient("default"));
    auto backgroundExecutor = std::make_shared<DrogonBackgroundExecutor>();
    auto rateUseCase = std::make_shared<RefreshExchangeRatesUseCase>(rateProvider, rateRepo);
    auto eventBus = std::make_shared<LocalEventBus>(backgroundExecutor);
    auto outboxJob = std::make_shared<OutboxPublisherJob>(eventBus);

    // 2. 初始化调度器并注册 Jobs
    JobManager jobManager;
    jobManager.registerJob(std::make_shared<ExchangeRateSyncJob>(rateUseCase, backgroundExecutor));
    jobManager.registerJob(outboxJob);

    // 3. 在 Drogon 启动前或启动回调中启动任务
    drogon::app().registerBeginningAdvice([&jobManager]() {
        jobManager.startAll();
        LOG_INFO << "All background jobs scheduled.";
    });

    // 4. 启动服务器
    drogon::app()
        .addListener("0.0.0.0", 8080)
        .loadConfigFile("config.json")
        .run();

    return 0;
}

```
