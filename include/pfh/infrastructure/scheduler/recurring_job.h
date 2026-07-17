// Personal Finance Hub - Reliable Recurring Job Runtime

#pragma once

#include "pfh/application/ports/i_clock.h"
#include "pfh/application/scheduler/i_background_executor.h"
#include "pfh/application/scheduler/i_job.h"
#include "pfh/application/scheduler/i_job_lease_repository.h"
#include "pfh/application/scheduler/i_timer_scheduler.h"
#include "pfh/application/operations/i_operations_repository.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <expected>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace pfh::infrastructure {

struct JobExecutionError {
    std::string summary;
};

using JobExecutionResult = std::expected<std::string, JobExecutionError>;
using JobAction = std::function<JobExecutionResult()>;

struct RecurringJobConfig {
    std::string name;
    std::chrono::seconds interval{60};
    std::chrono::seconds execution_timeout{30};
    std::chrono::seconds lease_duration{60};
    bool run_immediately = true;
};

class RecurringJob : public application::IJob,
                     public application::IJobRuntimeStatusReader,
                     public std::enable_shared_from_this<RecurringJob> {
public:
    RecurringJob(
        RecurringJobConfig config,
        application::ITimerScheduler& timers,
        application::IBackgroundExecutor& executor,
        const application::IClock& clock,
        JobAction action,
        application::IJobLeaseRepository* leases = nullptr,
        std::string owner_id = {});
    ~RecurringJob() override;

    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] domain::RepositoryVoidResult start() override;
    void stop() override;
    [[nodiscard]] bool trigger_now() override;
    [[nodiscard]] application::JobRuntimeSnapshot runtime_snapshot()
        const override;

private:
    void execute(std::uint64_t sequence) noexcept;
    void execute_run(std::uint64_t sequence);
    void finish_run() noexcept;
    [[nodiscard]] domain::RepositoryVoidResult validate() const;

    RecurringJobConfig config_;
    application::ITimerScheduler& timers_;
    application::IBackgroundExecutor& executor_;
    const application::IClock& clock_;
    JobAction action_;
    application::IJobLeaseRepository* leases_;
    std::string owner_id_;

    mutable std::mutex mutex_;
    std::condition_variable finished_;
    std::optional<application::TimerHandle> timer_;
    bool started_ = false;
    bool stopping_ = false;
    bool running_ = false;
    std::atomic<std::uint64_t> execution_sequence_{0};
    application::JobLastResult last_result_ =
        application::JobLastResult::NeverRun;
    std::optional<std::chrono::system_clock::time_point> last_started_at_;
    std::optional<std::chrono::system_clock::time_point> last_finished_at_;
    std::int64_t last_duration_milliseconds_ = 0;
};

} // namespace pfh::infrastructure
