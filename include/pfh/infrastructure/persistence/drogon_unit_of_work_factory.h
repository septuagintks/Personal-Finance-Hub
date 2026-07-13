// Personal Finance Hub - Drogon Unit of Work Factory

#pragma once

#include "pfh/application/persistence/i_unit_of_work_factory.h"

#ifdef PFH_HAS_POSTGRESQL

#include <drogon/orm/DbClient.h>

#include <memory>
#include <utility>

namespace pfh::infrastructure {

class DrogonUnitOfWorkFactory final : public application::IUnitOfWorkFactory {
public:
    explicit DrogonUnitOfWorkFactory(drogon::orm::DbClientPtr request_db)
        : request_db_(std::move(request_db)) {}

    [[nodiscard]] std::unique_ptr<application::IUnitOfWork> create_for_user(
        domain::UserId user_id) override;

    [[nodiscard]] std::unique_ptr<application::IBootstrapUnitOfWork>
    create_bootstrap() override;

private:
    drogon::orm::DbClientPtr request_db_;
};

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
