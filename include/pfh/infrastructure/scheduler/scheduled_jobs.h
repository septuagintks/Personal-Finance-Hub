// Personal Finance Hub - Phase 1 Scheduled Job Entry Points

#pragma once

#include "pfh/application/events/outbox_publisher.h"
#include "pfh/application/maintenance/cleanup_expired_sessions_use_case.h"
#include "pfh/application/maintenance/cleanup_expired_idempotency_use_case.h"
#include "pfh/application/use_cases/refresh_exchange_rates_use_case.h"
#include "pfh/infrastructure/scheduler/recurring_job.h"

#include <string>

namespace pfh::infrastructure {

class OutboxPublisherJob final : public RecurringJob {
public:
    OutboxPublisherJob(
        application::ITimerScheduler& timers,
        application::IBackgroundExecutor& executor,
        const application::IClock& clock,
        application::OutboxPublisher& publisher,
        std::string worker_id,
        RecurringJobConfig config);
};

class ExchangeRateRefreshJob final : public RecurringJob {
public:
    ExchangeRateRefreshJob(
        application::ITimerScheduler& timers,
        application::IBackgroundExecutor& executor,
        const application::IClock& clock,
        application::RefreshExchangeRatesUseCase& use_case,
        application::IJobLeaseRepository& leases,
        std::string owner_id,
        RecurringJobConfig config);
};

class SessionCleanupJob final : public RecurringJob {
public:
    SessionCleanupJob(
        application::ITimerScheduler& timers,
        application::IBackgroundExecutor& executor,
        const application::IClock& clock,
        application::CleanupExpiredSessionsUseCase& use_case,
        application::IJobLeaseRepository& leases,
        std::string owner_id,
        RecurringJobConfig config);
};

class IdempotencyCleanupJob final : public RecurringJob {
public:
    IdempotencyCleanupJob(
        application::ITimerScheduler& timers,
        application::IBackgroundExecutor& executor,
        const application::IClock& clock,
        application::CleanupExpiredIdempotencyUseCase& use_case,
        application::IJobLeaseRepository& leases,
        std::string owner_id,
        RecurringJobConfig config);
};

} // namespace pfh::infrastructure
