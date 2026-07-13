// Personal Finance Hub - Drogon Unit of Work Factory

#include "pfh/infrastructure/persistence/drogon_unit_of_work_factory.h"

#ifdef PFH_HAS_POSTGRESQL

#include "pfh/infrastructure/persistence/drogon_unit_of_work.h"

namespace pfh::infrastructure {

std::unique_ptr<application::IUnitOfWork>
DrogonUnitOfWorkFactory::create_for_user(domain::UserId user_id) {
    return std::make_unique<DrogonUnitOfWork>(request_db_, user_id);
}

std::unique_ptr<application::IBootstrapUnitOfWork>
DrogonUnitOfWorkFactory::create_bootstrap() {
    return std::make_unique<DrogonUnitOfWork>(request_db_, std::nullopt);
}

} // namespace pfh::infrastructure

#endif // PFH_HAS_POSTGRESQL
