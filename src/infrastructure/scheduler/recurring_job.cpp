// Personal Finance Hub - Reliable Recurring Job Runtime

#include "pfh/infrastructure/scheduler/recurring_job.h"

#include <algorithm>
#include <exception>
#include <spdlog/spdlog.h>
#include <string_view>
#include <utility>

namespace pfh::infrastructure {

namespace {

[[nodiscard]] std::string safe_log_summary(std::string_view value) {
    constexpr std::size_t kMaximumBytes = 512;
    std::string result;
    result.reserve(std::min(value.size(), kMaximumBytes));
    for (const char raw : value) {
        if (result.size() >= kMaximumBytes) {
            break;
        }
        const auto c = static_cast<unsigned char>(raw);
        if (c >= 0x20 && c != 0x7f) {
            result.push_back(raw);
        }
    }
    return result.empty() ? "background job failed" : result;
}

template <typename Action>
void log_noexcept(Action&& action) noexcept {
    try {
        std::forward<Action>(action)();
    } catch (...) {
    }
}

} // namespace

RecurringJob::RecurringJob(
    RecurringJobConfig config,
    application::ITimerScheduler& timers,
    application::IBackgroundExecutor& executor,
    const application::IClock& clock,
    JobAction action,
    application::IJobLeaseRepository* leases,
    std::string owner_id)
    : config_(std::move(config)),
      timers_(timers),
      executor_(executor),
      clock_(clock),
      action_(std::move(action)),
      leases_(leases),
      owner_id_(std::move(owner_id)) {}

RecurringJob::~RecurringJob() {
    stop();
}

std::string_view RecurringJob::name() const noexcept {
    return config_.name;
}

application::JobRuntimeSnapshot RecurringJob::runtime_snapshot() const {
    std::scoped_lock lock(mutex_);
    return application::JobRuntimeSnapshot{
        config_.name,
        started_,
        running_,
        execution_sequence_.load(std::memory_order_relaxed),
        last_result_,
        last_started_at_,
        last_finished_at_,
        last_duration_milliseconds_};
}

domain::RepositoryVoidResult RecurringJob::validate() const {
    if (config_.name.empty() || config_.name.size() > 128 ||
        config_.interval <= std::chrono::seconds::zero() ||
        config_.execution_timeout <= std::chrono::seconds::zero() ||
        !action_) {
        return std::unexpected(domain::RepositoryError::validation(
            "Recurring job configuration is invalid"));
    }
    if (leases_ != nullptr &&
        (owner_id_.empty() || owner_id_.size() > 128 ||
         config_.lease_duration <= config_.execution_timeout)) {
        return std::unexpected(domain::RepositoryError::validation(
            "Distributed job lease must outlive the execution timeout"));
    }
    return {};
}

domain::RepositoryVoidResult RecurringJob::start() {
    if (auto valid = validate(); !valid) {
        return valid;
    }
    const auto weak_self = weak_from_this();
    if (weak_self.expired()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Recurring jobs must be owned by std::shared_ptr"));
    }
    {
        std::scoped_lock lock(mutex_);
        if (started_) {
            return std::unexpected(domain::RepositoryError::conflict(
                "Recurring job is already started"));
        }
        started_ = true;
        stopping_ = false;
    }

    domain::RepositoryResult<application::TimerHandle> scheduled =
        std::unexpected(domain::RepositoryError::database(
            "Failed to register recurring timer"));
    try {
        scheduled = timers_.schedule_every(
            config_.interval,
            [weak_self] {
                if (const auto job = weak_self.lock()) {
                    (void)job->trigger_now();
                }
            });
    } catch (...) {
    }
    if (!scheduled) {
        std::scoped_lock lock(mutex_);
        started_ = false;
        return std::unexpected(scheduled.error());
    }
    {
        std::scoped_lock lock(mutex_);
        timer_ = *scheduled;
    }
    if (config_.run_immediately) {
        (void)trigger_now();
    }
    return {};
}

void RecurringJob::stop() {
    std::optional<application::TimerHandle> timer;
    {
        std::scoped_lock lock(mutex_);
        if (!started_ && !running_) {
            return;
        }
        stopping_ = true;
        started_ = false;
        timer = std::exchange(timer_, std::nullopt);
    }
    if (timer.has_value()) {
        try {
            timers_.cancel(*timer);
        } catch (...) {
        }
    }
    {
        std::unique_lock lock(mutex_);
        finished_.wait(lock, [this] { return !running_; });
        stopping_ = false;
    }
}

bool RecurringJob::trigger_now() {
    const auto self = weak_from_this().lock();
    if (!self) {
        return false;
    }
    std::uint64_t sequence = 0;
    {
        std::scoped_lock lock(mutex_);
        if (stopping_ || running_) {
            return false;
        }
        running_ = true;
        sequence = ++execution_sequence_;
    }
    bool submitted = false;
    try {
        submitted = executor_.submit(
            [self, sequence] { self->execute(sequence); });
    } catch (...) {
    }
    if (!submitted) {
        finish_run();
        log_noexcept([&] {
            spdlog::warn(
                "Background job queue rejected task job={} job_id={}-{}",
                config_.name,
                config_.name,
                sequence);
        });
        return false;
    }
    return true;
}

void RecurringJob::execute(std::uint64_t sequence) noexcept {
    try {
        execute_run(sequence);
    } catch (const std::exception&) {
        log_noexcept([&] {
            spdlog::error(
                "Background job runtime raised an exception job={} sequence={}",
                config_.name,
                sequence);
        });
    } catch (...) {
        log_noexcept([&] {
            spdlog::error(
                "Background job runtime raised an unknown exception job={} "
                "sequence={}",
                config_.name,
                sequence);
        });
    }
    finish_run();
}

void RecurringJob::execute_run(std::uint64_t sequence) {
    const std::string job_id = config_.name + "-" + std::to_string(sequence);
    const auto started_at = std::chrono::steady_clock::now();
    const auto wall_started_at = clock_.now();
    {
        std::scoped_lock lock(mutex_);
        last_started_at_ = wall_started_at;
    }
    log_noexcept([&] {
        spdlog::info(
            "Background job started job={} job_id={} trace_id={}",
            config_.name,
            job_id,
            job_id);
    });

    std::optional<application::JobLease> lease;
    bool should_execute = true;
    bool lease_skipped = false;
    bool lease_failed = false;
    if (leases_ != nullptr) {
        auto acquired = leases_->try_acquire(
            config_.name,
            owner_id_,
            clock_.now(),
            config_.lease_duration);
        if (!acquired) {
            should_execute = false;
            lease_failed = true;
            log_noexcept([&] {
                spdlog::error(
                    "Background job lease failed job={} job_id={} error={}",
                    config_.name,
                    job_id,
                    safe_log_summary(acquired.error().message));
            });
        } else if (!acquired->has_value()) {
            should_execute = false;
            lease_skipped = true;
            log_noexcept([&] {
                spdlog::debug(
                    "Background job skipped because another instance owns "
                    "lease job={} job_id={}",
                    config_.name,
                    job_id);
            });
        } else {
            lease = std::move(**acquired);
        }
    }

    std::optional<JobExecutionResult> result;
    if (should_execute) {
        try {
            result = action_();
        } catch (const std::exception&) {
            result = std::unexpected(JobExecutionError{
                "background job raised an exception"});
        } catch (...) {
            result = std::unexpected(JobExecutionError{
                "background job raised an unknown exception"});
        }
    }

    if (lease.has_value()) {
        auto released = leases_->release(
            config_.name,
            owner_id_,
            lease->token,
            clock_.now());
        if (!released || !*released) {
            log_noexcept([&] {
                spdlog::warn(
                    "Background job lease release failed job={} job_id={}",
                    config_.name,
                    job_id);
            });
        }
    }

    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - started_at);
    if (elapsed > config_.execution_timeout) {
        log_noexcept([&] {
            spdlog::warn(
                "Background job exceeded soft timeout job={} job_id={} "
                "duration_ms={} timeout_ms={}",
                config_.name,
                job_id,
                elapsed.count(),
                std::chrono::duration_cast<std::chrono::milliseconds>(
                    config_.execution_timeout).count());
        });
    }
    if (result.has_value()) {
        if (*result) {
            log_noexcept([&] {
                spdlog::info(
                    "Background job completed job={} job_id={} duration_ms={} "
                    "result={}",
                    config_.name,
                    job_id,
                    elapsed.count(),
                    safe_log_summary(**result));
            });
        } else {
            log_noexcept([&] {
                spdlog::error(
                    "Background job failed job={} job_id={} duration_ms={} "
                    "error={}",
                    config_.name,
                    job_id,
                    elapsed.count(),
                    safe_log_summary(result->error().summary));
            });
        }
    }
    {
        std::scoped_lock lock(mutex_);
        last_finished_at_ = clock_.now();
        last_duration_milliseconds_ = elapsed.count();
        if (result.has_value()) {
            last_result_ = *result
                ? application::JobLastResult::Succeeded
                : application::JobLastResult::Failed;
        } else if (lease_skipped) {
            last_result_ = application::JobLastResult::Skipped;
        } else if (lease_failed) {
            last_result_ = application::JobLastResult::Failed;
        }
    }
}

void RecurringJob::finish_run() noexcept {
    {
        std::scoped_lock lock(mutex_);
        running_ = false;
    }
    finished_.notify_all();
}

} // namespace pfh::infrastructure
