// Personal Finance Hub - In-Memory Unit of Work Factory

#pragma once

#include "pfh/application/persistence/i_unit_of_work_factory.h"
#include "pfh/infrastructure/persistence/in_memory_store.h"
#include "pfh/infrastructure/persistence/in_memory_unit_of_work.h"

#include <memory>

namespace pfh::infrastructure {

class InMemoryUnitOfWorkFactory final : public application::IUnitOfWorkFactory {
public:
    explicit InMemoryUnitOfWorkFactory(InMemoryStore& store) : store_(store) {}

    [[nodiscard]] std::unique_ptr<application::IUnitOfWork> create_for_user(
        domain::UserId user_id) override {
        return std::make_unique<InMemoryUnitOfWork>(store_, user_id);
    }

    [[nodiscard]] std::unique_ptr<application::IBootstrapUnitOfWork>
    create_bootstrap() override {
        return std::make_unique<InMemoryUnitOfWork>(store_);
    }

private:
    InMemoryStore& store_;
};

} // namespace pfh::infrastructure
