// Personal Finance Hub - Scheduler and Maintenance Tests

#include "pfh/application/maintenance/cleanup_expired_sessions_use_case.h"
#include "pfh/application/scheduler/i_timer_scheduler.h"
#include "pfh/infrastructure/persistence/in_memory_job_lease_repository.h"
#include "pfh/infrastructure/persistence/in_memory_session_cleanup_repository.h"
#include "pfh/infrastructure/scheduler/bounded_thread_pool.h"
#include "pfh/infrastructure/scheduler/job_manager.h"
#include "pfh/infrastructure/scheduler/recurring_job.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace pfh::infrastructure {
namespace {

using namespace std::chrono_literals;

class SchedulerClock final : public application::IClock {
public:
    explicit SchedulerClock(std::chrono::system_clock::time_point now)
        : now_(now) {}

    [[nodiscard]] std::chrono::system_clock::time_point now() const override {
        return now_;
    }

private:
    std::chrono::system_clock::time_point now_;
};

class ManualTimerScheduler final : public application::ITimerScheduler {
public:
    [[nodiscard]] domain::RepositoryResult<application::TimerHandle>
    schedule_every(
        std::chrono::seconds interval,
        std::function<void()> callback) override {
        if (interval <= std::chrono::seconds::zero() || !callback) {
            return std::unexpected(domain::RepositoryError::validation(
                "invalid timer"));
        }
        const auto handle = next_handle_++;
        callbacks_.emplace(handle, std::move(callback));
        return handle;
    }

    void cancel(application::TimerHandle handle) override {
        callbacks_.erase(handle);
    }

    void fire_all() {
        const auto snapshot = callbacks_;
        for (const auto& [ignored, callback] : snapshot) {
            (void)ignored;
            callback();
        }
    }

    [[nodiscard]] std::size_t timer_count() const noexcept {
        return callbacks_.size();
    }

private:
    application::TimerHandle next_handle_ = 1;
    std::map<application::TimerHandle, std::function<void()>> callbacks_;
};

class ThrowingExecutor final : public application::IBackgroundExecutor {
public:
    [[nodiscard]] bool submit(std::function<void()>) override {
        ++submit_calls;
        throw std::runtime_error("executor failure");
    }

    void shutdown() override {}

    int submit_calls = 0;
};

class InlineExecutor final : public application::IBackgroundExecutor {
public:
    [[nodiscard]] bool submit(std::function<void()> task) override {
        task();
        return true;
    }

    void shutdown() override {}
};

class ThrowingLeaseRepository final
    : public application::IJobLeaseRepository {
public:
    [[nodiscard]] domain::RepositoryResult<
        std::optional<application::JobLease>>
    try_acquire(
        std::string_view,
        std::string_view,
        std::chrono::system_clock::time_point,
        std::chrono::seconds) override {
        ++acquire_calls;
        throw std::runtime_error("lease failure");
    }

    [[nodiscard]] domain::RepositoryResult<bool> release(
        std::string_view,
        std::string_view,
        std::string_view,
        std::chrono::system_clock::time_point) override {
        return false;
    }

    int acquire_calls = 0;
};

class LifecycleJob final : public application::IJob {
public:
    LifecycleJob(std::string job_name, bool throw_on_start)
        : name_(std::move(job_name)), throw_on_start_(throw_on_start) {}

    [[nodiscard]] std::string_view name() const noexcept override {
        return name_;
    }

    [[nodiscard]] domain::RepositoryVoidResult start() override {
        ++start_calls;
        if (throw_on_start_) {
            throw std::runtime_error("start failure");
        }
        return {};
    }

    void stop() override { ++stop_calls; }
    [[nodiscard]] bool trigger_now() override { return false; }

    int start_calls = 0;
    int stop_calls = 0;

private:
    std::string name_;
    bool throw_on_start_;
};

TEST(BoundedThreadPoolTest, RejectsOverflowAndDrainsAcceptedTasksOnShutdown) {
    BoundedThreadPool pool(1, 1);
    std::promise<void> first_started;
    std::promise<void> release_first;
    const auto release = release_first.get_future().share();
    std::atomic<int> executed = 0;

    ASSERT_TRUE(pool.submit([&] {
        first_started.set_value();
        release.wait();
        ++executed;
    }));
    first_started.get_future().wait();
    ASSERT_TRUE(pool.submit([&] { ++executed; }));
    EXPECT_FALSE(pool.submit([&] { ++executed; }));

    release_first.set_value();
    pool.shutdown();
    EXPECT_EQ(executed.load(), 2);
    EXPECT_FALSE(pool.submit([] {}));
}

TEST(InMemoryJobLeaseRepositoryTest,
     LeaseExpiryAndTokenPreventStaleOwnerRelease) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;
    InMemoryJobLeaseRepository repository(store);

    auto first = repository.try_acquire("refresh", "instance-a", now, 60s);
    ASSERT_TRUE(first.has_value());
    ASSERT_TRUE(first->has_value());
    const auto stale_token = (*first)->token;

    auto blocked = repository.try_acquire("refresh", "instance-b", now, 60s);
    ASSERT_TRUE(blocked.has_value());
    EXPECT_FALSE(blocked->has_value());

    auto replacement = repository.try_acquire(
        "refresh", "instance-b", now + 61s, 60s);
    ASSERT_TRUE(replacement.has_value());
    ASSERT_TRUE(replacement->has_value());
    EXPECT_NE((*replacement)->token, stale_token);

    auto stale_release = repository.release(
        "refresh", "instance-a", stale_token, now + 62s);
    ASSERT_TRUE(stale_release.has_value());
    EXPECT_FALSE(*stale_release);
    auto current_release = repository.release(
        "refresh", "instance-b", (*replacement)->token, now + 62s);
    ASSERT_TRUE(current_release.has_value());
    EXPECT_TRUE(*current_release);
}

TEST(CleanupExpiredSessionsUseCaseTest,
     DeletesOnlyExpiredRowsWithPerTableBatchLimit) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    InMemoryStore store;
    application::RefreshTokenRecord expired_refresh;
    expired_refresh.token_hash = "expired-refresh";
    expired_refresh.expires_at = now;
    store.refresh_tokens.emplace(expired_refresh.token_hash, expired_refresh);
    application::RefreshTokenRecord future_refresh;
    future_refresh.token_hash = "future-refresh";
    future_refresh.expires_at = now + 1h;
    store.refresh_tokens.emplace(future_refresh.token_hash, future_refresh);
    store.revoked_access_tokens.emplace(
        "expired-access",
        InMemoryRevokedAccessToken{
            "pfh", "expired-access", "session", now - 1s, now - 1h});
    store.revoked_access_tokens.emplace(
        "future-access",
        InMemoryRevokedAccessToken{
            "pfh", "future-access", "session", now + 1h, now - 1h});
    store.revoked_sessions.emplace(
        "expired-session",
        InMemoryRevokedSession{
            domain::UserId(1),
            "expired-session",
            now,
            now - 1h,
            "reuse"});
    store.revoked_sessions.emplace(
        "future-session",
        InMemoryRevokedSession{
            domain::UserId(1),
            "future-session",
            now + 1h,
            now - 1h,
            "reuse"});

    SchedulerClock clock(now);
    InMemorySessionCleanupRepository repository(store);
    application::CleanupExpiredSessionsUseCase use_case(repository, clock, 1);
    auto cleaned = use_case.execute();

    ASSERT_TRUE(cleaned.has_value()) << cleaned.error().message;
    EXPECT_EQ(cleaned->refresh_tokens_deleted, 1U);
    EXPECT_EQ(cleaned->revoked_access_tokens_deleted, 1U);
    EXPECT_EQ(cleaned->revoked_sessions_deleted, 1U);
    EXPECT_EQ(cleaned->total_deleted(), 3U);
    EXPECT_TRUE(store.refresh_tokens.contains("future-refresh"));
    EXPECT_TRUE(store.revoked_access_tokens.contains("future-access"));
    EXPECT_TRUE(store.revoked_sessions.contains("future-session"));

    auto repeated = use_case.execute();
    ASSERT_TRUE(repeated.has_value());
    EXPECT_EQ(repeated->total_deleted(), 0U);
}

TEST(RecurringJobTest, TimerOnlyEnqueuesAndLocalReentryIsRejected) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    SchedulerClock clock(now);
    ManualTimerScheduler timers;
    BoundedThreadPool pool(1, 2);
    std::promise<void> action_started;
    std::promise<void> release_action;
    const auto release = release_action.get_future().share();
    std::atomic<int> runs = 0;

    auto job = std::make_shared<RecurringJob>(
        RecurringJobConfig{
            "blocking-job", 10s, 5s, 10s, false},
        timers,
        pool,
        clock,
        [&] {
            action_started.set_value();
            release.wait();
            ++runs;
            return JobExecutionResult("completed");
        });
    ASSERT_TRUE(job->start());
    EXPECT_EQ(timers.timer_count(), 1U);

    timers.fire_all();
    action_started.get_future().wait();
    EXPECT_FALSE(job->trigger_now());
    release_action.set_value();
    job->stop();

    EXPECT_EQ(runs.load(), 1);
    EXPECT_EQ(timers.timer_count(), 0U);
    pool.shutdown();

    ManualTimerScheduler rejecting_timers;
    ThrowingExecutor rejecting_executor;
    auto rejected = std::make_shared<RecurringJob>(
        RecurringJobConfig{"rejecting-job", 10s, 5s, 10s, false},
        rejecting_timers,
        rejecting_executor,
        clock,
        [] { return JobExecutionResult("not executed"); });
    ASSERT_TRUE(rejected->start());
    EXPECT_FALSE(rejected->trigger_now());
    EXPECT_FALSE(rejected->trigger_now());
    EXPECT_EQ(rejecting_executor.submit_calls, 2);
    rejected->stop();
}

TEST(RecurringJobTest, DistributedLeaseAllowsOnlyOneInstanceToExecute) {
    const auto now = std::chrono::system_clock::time_point{123456s};
    SchedulerClock clock(now);
    ManualTimerScheduler first_timers;
    ManualTimerScheduler second_timers;
    BoundedThreadPool first_pool(1, 1);
    BoundedThreadPool second_pool(1, 1);
    InMemoryStore store;
    InMemoryJobLeaseRepository leases(store);
    std::promise<void> first_started;
    std::promise<void> release_first;
    const auto release = release_first.get_future().share();
    std::atomic<int> first_runs = 0;
    std::atomic<int> second_runs = 0;
    const RecurringJobConfig config{
        "leased-job", 10s, 5s, 30s, false};

    auto first = std::make_shared<RecurringJob>(
        config,
        first_timers,
        first_pool,
        clock,
        [&] {
            first_started.set_value();
            release.wait();
            ++first_runs;
            return JobExecutionResult("first completed");
        },
        &leases,
        "instance-a");
    auto second = std::make_shared<RecurringJob>(
        config,
        second_timers,
        second_pool,
        clock,
        [&] {
            ++second_runs;
            return JobExecutionResult("second completed");
        },
        &leases,
        "instance-b");
    ASSERT_TRUE(first->start());
    ASSERT_TRUE(second->start());
    ASSERT_TRUE(first->trigger_now());
    first_started.get_future().wait();
    ASSERT_TRUE(second->trigger_now());
    second->stop();

    EXPECT_EQ(second_runs.load(), 0);
    release_first.set_value();
    first->stop();
    EXPECT_EQ(first_runs.load(), 1);
    first_pool.shutdown();
    second_pool.shutdown();

    ManualTimerScheduler throwing_timers;
    InlineExecutor inline_executor;
    ThrowingLeaseRepository throwing_leases;
    std::atomic<int> unexpected_runs = 0;
    auto throwing = std::make_shared<RecurringJob>(
        config,
        throwing_timers,
        inline_executor,
        clock,
        [&] {
            ++unexpected_runs;
            return JobExecutionResult("must not execute");
        },
        &throwing_leases,
        "instance-c");
    ASSERT_TRUE(throwing->start());
    EXPECT_TRUE(throwing->trigger_now());
    EXPECT_TRUE(throwing->trigger_now());
    EXPECT_EQ(throwing_leases.acquire_calls, 2);
    EXPECT_EQ(unexpected_runs.load(), 0);
    throwing->stop();

    JobManager manager;
    auto started_job = std::make_shared<LifecycleJob>("started", false);
    auto failed_job = std::make_shared<LifecycleJob>("failed", true);
    ASSERT_TRUE(manager.register_job(started_job));
    ASSERT_TRUE(manager.register_job(failed_job));
    auto manager_start = manager.start_all();
    ASSERT_FALSE(manager_start.has_value());
    EXPECT_EQ(started_job->start_calls, 1);
    EXPECT_EQ(started_job->stop_calls, 1);
    EXPECT_EQ(failed_job->start_calls, 1);
    EXPECT_EQ(failed_job->stop_calls, 1);
}

} // namespace
} // namespace pfh::infrastructure
