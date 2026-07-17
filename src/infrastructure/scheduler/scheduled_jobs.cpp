// Personal Finance Hub - Phase 1 Scheduled Job Entry Points

#include "pfh/infrastructure/scheduler/scheduled_jobs.h"

#include <string>
#include <utility>

namespace pfh::infrastructure {

namespace {

[[nodiscard]] RecurringJobConfig named(
    RecurringJobConfig config,
    std::string name) {
    config.name = std::move(name);
    return config;
}

} // namespace

OutboxPublisherJob::OutboxPublisherJob(
    application::ITimerScheduler& timers,
    application::IBackgroundExecutor& executor,
    const application::IClock& clock,
    application::OutboxPublisher& publisher,
    std::string worker_id,
    RecurringJobConfig config)
    : RecurringJob(
          named(std::move(config), "outbox-publisher"),
          timers,
          executor,
          clock,
          [&publisher, worker_id = std::move(worker_id)] {
              auto published = publisher.run_once(worker_id);
              if (!published) {
                  return JobExecutionResult(std::unexpected(
                      JobExecutionError{published.error().message}));
              }
              return JobExecutionResult(
                  "claimed=" + std::to_string(published->claimed) +
                  " published=" + std::to_string(published->published) +
                  " failed=" + std::to_string(published->failed) +
                  " dead_lettered=" +
                  std::to_string(published->dead_lettered));
          }) {}

ExchangeRateRefreshJob::ExchangeRateRefreshJob(
    application::ITimerScheduler& timers,
    application::IBackgroundExecutor& executor,
    const application::IClock& clock,
    application::RefreshExchangeRatesUseCase& use_case,
    application::IJobLeaseRepository& leases,
    std::string owner_id,
    RecurringJobConfig config)
    : RecurringJob(
          named(std::move(config), "exchange-rate-refresh"),
          timers,
          executor,
          clock,
          [&use_case] {
              auto refreshed = use_case.execute();
              if (!refreshed) {
                  return JobExecutionResult(std::unexpected(
                      JobExecutionError{refreshed.error().message}));
              }
              return JobExecutionResult(refreshed->message);
          },
          &leases,
          std::move(owner_id)) {}

SessionCleanupJob::SessionCleanupJob(
    application::ITimerScheduler& timers,
    application::IBackgroundExecutor& executor,
    const application::IClock& clock,
    application::CleanupExpiredSessionsUseCase& use_case,
    application::IJobLeaseRepository& leases,
    std::string owner_id,
    RecurringJobConfig config)
    : RecurringJob(
          named(std::move(config), "session-cleanup"),
          timers,
          executor,
          clock,
          [&use_case] {
              auto cleaned = use_case.execute();
              if (!cleaned) {
                  return JobExecutionResult(std::unexpected(
                      JobExecutionError{cleaned.error().message}));
              }
              return JobExecutionResult(
                  "refresh_tokens=" +
                  std::to_string(cleaned->refresh_tokens_deleted) +
                  " revoked_access_tokens=" +
                  std::to_string(cleaned->revoked_access_tokens_deleted) +
                  " revoked_sessions=" +
                  std::to_string(cleaned->revoked_sessions_deleted));
          },
          &leases,
          std::move(owner_id)) {}

IdempotencyCleanupJob::IdempotencyCleanupJob(
    application::ITimerScheduler& timers,
    application::IBackgroundExecutor& executor,
    const application::IClock& clock,
    application::CleanupExpiredIdempotencyUseCase& use_case,
    application::IJobLeaseRepository& leases,
    std::string owner_id,
    RecurringJobConfig config)
    : RecurringJob(
          named(std::move(config), "idempotency-cleanup"),
          timers,
          executor,
          clock,
          [&use_case] {
              auto cleaned = use_case.execute();
              if (!cleaned) {
                  return JobExecutionResult(std::unexpected(
                      JobExecutionError{cleaned.error().message}));
              }
              return JobExecutionResult(
                  "request_idempotency=" + std::to_string(*cleaned));
          },
          &leases,
          std::move(owner_id)) {}

} // namespace pfh::infrastructure
