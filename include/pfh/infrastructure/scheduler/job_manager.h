// Personal Finance Hub - Scheduled Job Lifecycle Manager

#pragma once

#include "pfh/application/scheduler/i_job.h"

#include <memory>
#include <mutex>
#include <vector>

namespace pfh::infrastructure {

class JobManager {
public:
    ~JobManager();

    [[nodiscard]] domain::RepositoryVoidResult register_job(
        std::shared_ptr<application::IJob> job);
    [[nodiscard]] domain::RepositoryVoidResult start_all();
    void stop_all();

private:
    std::mutex mutex_;
    std::vector<std::shared_ptr<application::IJob>> jobs_;
    std::size_t started_count_ = 0;
};

} // namespace pfh::infrastructure
