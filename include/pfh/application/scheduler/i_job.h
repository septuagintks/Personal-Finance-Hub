// Personal Finance Hub - Scheduled Job Lifecycle Port

#pragma once

#include "pfh/domain/repositories/repository_error.h"

#include <string_view>

namespace pfh::application {

class IJob {
public:
    virtual ~IJob() = default;

    [[nodiscard]] virtual std::string_view name() const noexcept = 0;
    [[nodiscard]] virtual domain::RepositoryVoidResult start() = 0;
    virtual void stop() = 0;
    [[nodiscard]] virtual bool trigger_now() = 0;
};

} // namespace pfh::application
