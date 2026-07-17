// Personal Finance Hub - Server-Authoritative User Role Reader

#pragma once

#include "pfh/domain/repositories/repository_error.h"
#include "pfh/domain/user.h"

namespace pfh::application {

class IUserRoleReader {
public:
    virtual ~IUserRoleReader() = default;

    [[nodiscard]] virtual domain::RepositoryResult<domain::UserRole>
    find_role_by_id(domain::UserId user_id) = 0;
};

} // namespace pfh::application
