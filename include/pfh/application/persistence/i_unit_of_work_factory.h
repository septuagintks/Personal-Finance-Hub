// Personal Finance Hub - Unit of Work Factory Port

#pragma once

#include "pfh/application/persistence/i_bootstrap_unit_of_work.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/domain/user.h"

#include <memory>

namespace pfh::application {

class IUnitOfWorkFactory {
public:
    virtual ~IUnitOfWorkFactory() = default;

    [[nodiscard]] virtual std::unique_ptr<IUnitOfWork> create_for_user(
        domain::UserId user_id) = 0;
    [[nodiscard]] virtual std::unique_ptr<IBootstrapUnitOfWork> create_bootstrap() = 0;
};

} // namespace pfh::application
