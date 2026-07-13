// Personal Finance Hub - Active Currency Query Port
// Version: 1.0
// C++23
//
// System-scoped scheduler query. Unlike tenant repositories, this port has no
// authenticated UserId and must be implemented with an explicitly privileged,
// read-only infrastructure connection when PostgreSQL FORCE RLS is enabled.

#pragma once

#include "pfh/domain/currency.h"
#include "pfh/domain/repositories/repository_error.h"

#include <vector>

namespace pfh::application {

class IActiveCurrencyQuery {
public:
    virtual ~IActiveCurrencyQuery() = default;

    /// Return distinct currencies required by reporting: every non-archived
    /// account currency plus every user's configured base currency.
    [[nodiscard]] virtual domain::RepositoryResult<std::vector<domain::Currency>>
    list_active_currencies() = 0;
};

}  // namespace pfh::application
