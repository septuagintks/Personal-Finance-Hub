// Personal Finance Hub - Scheduled Job Lifecycle Manager

#include "pfh/infrastructure/scheduler/job_manager.h"

#include <algorithm>
#include <spdlog/spdlog.h>
#include <string>
#include <utility>

namespace pfh::infrastructure {

namespace {

template <typename Action>
void log_noexcept(Action&& action) noexcept {
    try {
        std::forward<Action>(action)();
    } catch (...) {
    }
}

void stop_noexcept(const std::shared_ptr<application::IJob>& job) noexcept {
    try {
        job->stop();
    } catch (...) {
        log_noexcept([&] {
            spdlog::error(
                "Scheduled job stop raised an exception job={}",
                job->name());
        });
    }
}

} // namespace

JobManager::~JobManager() {
    stop_all();
}

domain::RepositoryVoidResult JobManager::register_job(
    std::shared_ptr<application::IJob> job) {
    if (!job || job->name().empty()) {
        return std::unexpected(domain::RepositoryError::validation(
            "Scheduled job registration is invalid"));
    }
    std::scoped_lock lock(mutex_);
    if (started_count_ != 0) {
        return std::unexpected(domain::RepositoryError::conflict(
            "Cannot register jobs after the manager has started"));
    }
    if (std::ranges::any_of(jobs_, [&](const auto& existing) {
            return existing->name() == job->name();
        })) {
        return std::unexpected(domain::RepositoryError::conflict(
            "Scheduled job name is already registered"));
    }
    jobs_.push_back(std::move(job));
    return {};
}

domain::RepositoryVoidResult JobManager::start_all() {
    std::scoped_lock lock(mutex_);
    if (started_count_ != 0) {
        return std::unexpected(domain::RepositoryError::conflict(
            "Scheduled jobs are already started"));
    }
    for (const auto& job : jobs_) {
        domain::RepositoryVoidResult started =
            std::unexpected(domain::RepositoryError::database(
                "Scheduled job start raised an exception"));
        try {
            started = job->start();
        } catch (...) {
            stop_noexcept(job);
        }
        if (!started) {
            while (started_count_ > 0) {
                stop_noexcept(jobs_[--started_count_]);
            }
            return std::unexpected(started.error());
        }
        ++started_count_;
        log_noexcept([&] {
            spdlog::info("Scheduled job registered job={}", job->name());
        });
    }
    return {};
}

void JobManager::stop_all() {
    std::scoped_lock lock(mutex_);
    while (started_count_ > 0) {
        const auto& job = jobs_[--started_count_];
        stop_noexcept(job);
        log_noexcept([&] {
            spdlog::info("Scheduled job stopped job={}", job->name());
        });
    }
}

} // namespace pfh::infrastructure
