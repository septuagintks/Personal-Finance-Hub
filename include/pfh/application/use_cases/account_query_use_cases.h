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

[[nodiscard]] inline AccountDto account_to_dto(const domain::Account& account) {
    AccountDto dto;
    dto.id = account.id();
    dto.owner = account.owner();
    dto.name = account.name();
    dto.type = account.type();
    dto.subtype = account.subtype();
    dto.category = account.category();
    dto.currency_code = account.currency().code();
    dto.description = account.description();
    dto.is_archived = account.is_archived();
    dto.archived_at = account.archived_at();
    dto.created_at = account.created_at();
    dto.updated_at = account.updated_at();
    dto.version = account.version();
    return dto;
}

class ListAccountsUseCase {
public:
    explicit ListAccountsUseCase(domain::IAccountRepository& accounts)
        : accounts_(accounts) {}

    [[nodiscard]] Result<std::vector<AccountDto>> execute(
        domain::UserId user_id,
        AccountListStatus status = AccountListStatus::Active) {
        if (!user_id.is_valid()) {
            return err(Error::validation("User id is invalid"));
        }
        std::optional<bool> archived = false;
        if (status == AccountListStatus::Archived) archived = true;
        if (status == AccountListStatus::All) archived = std::nullopt;
        auto accounts = accounts_.find_by_user(user_id, archived);
        if (!accounts) {
            return err(from_repository(accounts.error()));
        }
        std::vector<AccountDto> result;
        result.reserve(accounts->size());
        for (const auto& a : *accounts) {
            result.push_back(account_to_dto(a));
        }
        return result;
    }

private:
    domain::IAccountRepository& accounts_;
};

class GetAccountUseCase {
public:
    explicit GetAccountUseCase(domain::IAccountRepository& accounts)
        : accounts_(accounts) {}

    [[nodiscard]] Result<AccountDto> execute(
        domain::UserId user_id,
        domain::AccountId account_id) {
        if (!user_id.is_valid() || !account_id.is_valid()) {
            return err(Error::validation("User and account ids must be valid"));
        }
        auto account = accounts_.find_by_id_for_user(account_id, user_id);
        if (!account) return err(from_repository(account.error()));
        return account_to_dto(*account);
    }

private:
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
