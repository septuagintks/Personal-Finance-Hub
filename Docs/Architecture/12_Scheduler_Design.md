# Personal Finance Hub - Scheduler & Background Task Design

Version: 1.0

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

---

## 3. 具体后台任务实现案例

以下展示如何利用 Drogon 的 `trantor::EventLoop` 实现具体的业务 Job。

### 3.1 每日汇率刷新任务 (Exchange Rate Sync)

承接《08_Exchange_Rate_System_Design.md》，每天执行一次汇率拉取。

```cpp
// scheduler/jobs/ExchangeRateSyncJob.cpp
#include "scheduler/IJob.hpp"
#include "application/use_cases/RefreshExchangeRatesUseCase.hpp"
#include <drogon/drogon.h>

class ExchangeRateSyncJob : public IJob {
private:
    std::shared_ptr<RefreshExchangeRatesUseCase> useCase_;
    drogon::TimerId timerId_;
    double intervalSeconds_ = 12.0 * 3600.0; // 12小时

public:
    ExchangeRateSyncJob(std::shared_ptr<RefreshExchangeRatesUseCase> useCase)
        : useCase_(useCase), timerId_(drogon::kInvalidTimerId) {}

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
        LOG_INFO << "Executing " << getJobName() << "...";
        auto result = useCase_->execute();

        if (!result) {
            LOG_ERROR << getJobName() << " failed: " << result.error();
            // 在这里可以接入邮件预警系统或告警模块
        } else {
            LOG_INFO << getJobName() << " completed successfully.";
        }
    }
};

```

### 3.2 僵尸数据清理任务 (Data Cleanup)

自动清理归档超过 30 天的无效会话、过期的 JWT Token，或软删除超过 1 年的废弃流水。

```cpp
// scheduler/jobs/DataCleanupJob.cpp
// 每周日凌晨运行一次，调用 CleanupUseCase，执行物理删除 DELETE 操作。
// ... (代码结构同上，间隔设为 7 * 24 * 3600，且可以利用 runAt 设置特定时间点) ...

```

---

## 4. 异常捕获与容错机制 (Resilience)

后台任务与 HTTP 请求不同，它们如果在执行过程中抛出未捕获的 C++ 异常，会导致整个 Drogon 进程崩溃宕机（Crash）。

**防护规范**：
在 `triggerNow()` 的实现中，虽然 Use Case 层面我们强制要求返回 `std::expected` 来避免业务异常，但为了防御基础设施层（如 JsonCpp 解析崩溃、第三方底层库抛出 `std::bad_alloc`），必须在 Job 的最外层加固。

```cpp
void triggerNow() override {
    try {
        auto result = useCase_->execute();
        if (!result) {
            LOG_ERROR << getJobName() << " domain failure: " << result.error();
        }
    } catch (const std::exception& e) {
        LOG_FATAL << getJobName() << " crashed with standard exception: " << e.what();
    } catch (...) {
        LOG_FATAL << getJobName() << " crashed with unknown exception!";
    }
}

```

---

## 5. 多实例部署下的并发执行冲突 (分布式锁)

**痛点**：如果我们为了负载均衡，部署了 3 个后台容器实例（Instance A, B, C）。当定时器时间一到，3 个实例都会触发 `ExchangeRateSyncJob`，导致同时向外部 API 发送 3 次请求，并往数据库写入 3 份重复的历史汇率。

**解决方案：基于 PostgreSQL 的轻量级悲观锁**
由于我们没有 Redis，我们需要利用 PostgreSQL 强大的原生功能来防止并发冲突。

### 5.1 方案 A：PG 原生咨询锁 (Advisory Locks)

不需要建表，非常轻量。

```sql
-- 在 C++ 中执行 SQL 获取锁（假设锁 ID 为 1001 表示汇率任务）
SELECT pg_try_advisory_lock(1001);
-- 返回 true 说明拿到锁，继续执行任务；
-- 返回 false 说明别的实例正在执行，当前实例直接跳过本次执行。

-- 执行完毕后释放锁
SELECT pg_advisory_unlock(1001);

```

### 5.2 方案 B：实体锁表 (sys_locks)

适用于需要记录“上次是谁执行的、执行了多久”的场景。

```sql
CREATE TABLE sys_locks (
    job_name VARCHAR(64) PRIMARY KEY,
    locked_at TIMESTAMPTZ,
    locked_by VARCHAR(64) -- 实例的 IP 或 Hostname
);

```

在 C++ Job 中：

```cpp
// 使用带有 NOWAIT 的排他锁
auto res = dbClient_->execSqlSync("SELECT * FROM sys_locks WHERE job_name = $1 FOR UPDATE NOWAIT", jobName);

```

---

## 6. 系统生命周期集成 (main.cpp)

在系统的入口，必须按正确的顺序初始化。

```cpp
// main.cpp
#include <drogon/drogon.h>
#include "scheduler/JobManager.hpp"
// ... include 其他依赖

int main() {
    // 1. 初始化依赖注入 (Repositories, Providers, UseCases)
    auto rateProvider = std::make_shared<OpenExchangeRatesProvider>("API_KEY");
    auto rateRepo = std::make_shared<ExchangeRateRepositoryImpl>(drogon::app().getDbClient("default"));
    auto rateUseCase = std::make_shared<RefreshExchangeRatesUseCase>(rateProvider, rateRepo);

    // 2. 初始化调度器并注册 Jobs
    JobManager jobManager;
    jobManager.registerJob(std::make_shared<ExchangeRateSyncJob>(rateUseCase));

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
