// Personal Finance Hub - Account Query Use Cases
// Version: 1.0
// C++23

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/error_mapping.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include <vector>

namespace pfh::application {

class ListAccountsUseCase {
public:
    explicit ListAccountsUseCase(domain::IAccountRepository& accounts)
        : accounts_(accounts) {}

    [[nodiscard]] Result<std::vector<AccountDto>> execute(domain::UserId user_id) {
        if (!user_id.is_valid()) {
            return err(Error::validation("User id is invalid"));
        }
        auto accounts = accounts_.find_active_by_user(user_id);
        if (!accounts) {
            return err(from_repository(accounts.error()));
        }
        std::vector<AccountDto> result;
        result.reserve(accounts->size());
        for (const auto& a : *accounts) {
            result.push_back(to_dto(a));
        }
        return result;
    }

private:
    [[nodiscard]] static AccountDto to_dto(const domain::Account& a) {
        AccountDto dto;
        dto.id = a.id();
        dto.owner = a.owner();
        dto.name = a.name();
        dto.type = a.type();
        dto.subtype = a.subtype();
        dto.category = a.category();
        dto.currency_code = a.currency().code();
        dto.description = a.description();
        dto.is_archived = a.is_archived();
        dto.version = a.version();
        return dto;
    }

    domain::IAccountRepository& accounts_;
};

class GetAccountBalanceUseCase {
public:
    explicit GetAccountBalanceUseCase(domain::IAccountRepository& accounts)
        : accounts_(accounts) {}

    [[nodiscard]] Result<BalanceDto> execute(
        domain::UserId user_id,
        domain::AccountId account_id) {
        if (!user_id.is_valid() || !account_id.is_valid()) {
            return err(Error::validation("User and account ids must be valid"));
        }
        auto account = accounts_.find_by_id_for_user(account_id, user_id);
        if (!account) {
            return err(from_repository(account.error()));
        }
        auto snapshot = accounts_.balance_of(account_id);
        if (!snapshot) {
            return err(from_repository(snapshot.error()));
        }
        BalanceDto dto;
        dto.account_id = account_id;
        dto.currency_code = snapshot->balance.currency().code();
        dto.amount = snapshot->balance.amount().to_string();
        if (snapshot->last_transaction_id.is_valid()) {
            dto.last_transaction_id = snapshot->last_transaction_id;
        }
        dto.updated_at = snapshot->as_of;
        return dto;
    }

private:
    domain::IAccountRepository& accounts_;
};

} // namespace pfh::application
