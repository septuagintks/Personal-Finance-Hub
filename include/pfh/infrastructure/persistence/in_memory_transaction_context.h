// Personal Finance Hub - In-Memory Transaction Context
// Version: 1.0
// C++23
//
// Opaque transaction handle for in-memory persistence tests.
// Real Drogon implementation will wrap drogon::orm::Transaction.

#pragma once

#include "pfh/domain/repositories/i_transaction_context.h"
#include <cstdint>

namespace pfh::infrastructure {

class InMemoryTransactionContext final : public domain::ITransactionContext {
public:
    explicit InMemoryTransactionContext(std::uint64_t id) : id_(id) {}

    [[nodiscard]] std::uint64_t id() const noexcept { return id_; }

private:
    std::uint64_t id_;
};

} // namespace pfh::infrastructure
