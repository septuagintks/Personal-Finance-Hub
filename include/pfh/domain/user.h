// Personal Finance Hub - User Entity
// Version: 1.0
// C++23
//
// User is a minimal identity entity. Business preferences are separated into
// UserPreference, which is composed from users.base_currency_code and the
// user_preferences table by the repository layer.

#pragma once

#include "pfh/domain/typed_id.h"
#include <string>

namespace pfh::domain {

/// @brief Strongly-typed user identifier.
using UserId = TypedId<UserIdTag>;

enum class UserRole {
    User,
    Operator
};

/// @brief User entity — minimal identity holder.
///
/// Responsible for identity only. All preferences (base currency, locale, etc.)
/// are handled by UserPreference.
class User {
public:
    User(UserId id, std::string username, UserRole role = UserRole::User)
        : id_(id), username_(std::move(username)), role_(role) {}

    [[nodiscard]] UserId id() const noexcept { return id_; }
    [[nodiscard]] const std::string& username() const noexcept { return username_; }
    [[nodiscard]] UserRole role() const noexcept { return role_; }

private:
    UserId id_;
    std::string username_;
    UserRole role_ = UserRole::User;
};

} // namespace pfh::domain
