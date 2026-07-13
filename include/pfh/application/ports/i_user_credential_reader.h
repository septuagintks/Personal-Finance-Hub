// Personal Finance Hub - User Credential Reader Port

#pragma once

#include "pfh/application/security/auth_models.h"
#include "pfh/domain/repositories/repository_error.h"

#include <string>

namespace pfh::application {

class IUserCredentialReader {
public:
    virtual ~IUserCredentialReader() = default;

    [[nodiscard]] virtual domain::RepositoryResult<UserCredentialRecord>
    find_credentials_by_username(const std::string& normalized_username) = 0;
};

} // namespace pfh::application
