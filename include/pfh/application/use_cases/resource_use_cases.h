// Personal Finance Hub - Foundational Resource Use Cases

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error.h"
#include "pfh/application/persistence/i_unit_of_work.h"
#include "pfh/domain/repositories/i_account_repository.h"
#include "pfh/domain/repositories/i_audit_log_repository.h"
#include "pfh/domain/repositories/i_category_repository.h"
#include "pfh/domain/repositories/i_tag_repository.h"
#include "pfh/domain/repositories/i_user_preference_repository.h"

#include <optional>
#include <vector>

namespace pfh::application {

class CreateAccountUseCase {
public:
    CreateAccountUseCase(
        domain::IAccountRepository& accounts,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : accounts_(accounts), audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] Result<AccountDto> execute(const CreateAccountCommand& command);

private:
    domain::IAccountRepository& accounts_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class ArchiveAccountUseCase {
public:
    ArchiveAccountUseCase(
        domain::IAccountRepository& accounts,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : accounts_(accounts), audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] VoidResult execute(const ArchiveAccountCommand& command);

private:
    domain::IAccountRepository& accounts_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class UpdateAccountUseCase {
public:
    UpdateAccountUseCase(
        domain::IAccountRepository& accounts,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : accounts_(accounts),
          audit_logs_(audit_logs),
          uow_(uow) {}

    [[nodiscard]] Result<AccountDto> execute(const UpdateAccountCommand& command);

private:
    domain::IAccountRepository& accounts_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class RestoreAccountUseCase {
public:
    RestoreAccountUseCase(
        domain::IAccountRepository& accounts,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : accounts_(accounts), audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] VoidResult execute(const RestoreAccountCommand& command);

private:
    domain::IAccountRepository& accounts_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class ListCategoriesUseCase {
public:
    explicit ListCategoriesUseCase(domain::ICategoryRepository& categories)
        : categories_(categories) {}

    [[nodiscard]] Result<std::vector<CategoryTreeDto>> execute(
        domain::UserId user_id,
        std::optional<domain::CategoryBoard> board);

private:
    domain::ICategoryRepository& categories_;
};

class CreateCategoryUseCase {
public:
    CreateCategoryUseCase(
        domain::ICategoryRepository& categories,
        domain::IUserPreferenceRepository& preferences,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : categories_(categories),
          preferences_(preferences),
          audit_logs_(audit_logs),
          uow_(uow) {}

    [[nodiscard]] Result<CategoryDto> execute(const CreateCategoryCommand& command);

private:
    domain::ICategoryRepository& categories_;
    domain::IUserPreferenceRepository& preferences_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class DeleteCategoryUseCase {
public:
    DeleteCategoryUseCase(
        domain::ICategoryRepository& categories,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : categories_(categories), audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] VoidResult execute(const DeleteCategoryCommand& command);

private:
    domain::ICategoryRepository& categories_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class ListTagsUseCase {
public:
    explicit ListTagsUseCase(domain::ITagRepository& tags) : tags_(tags) {}
    [[nodiscard]] Result<std::vector<TagDto>> execute(domain::UserId user_id);

private:
    domain::ITagRepository& tags_;
};

class CreateTagUseCase {
public:
    CreateTagUseCase(
        domain::ITagRepository& tags,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : tags_(tags), audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] Result<TagDto> execute(const CreateTagCommand& command);

private:
    domain::ITagRepository& tags_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class DeleteTagUseCase {
public:
    DeleteTagUseCase(
        domain::ITagRepository& tags,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : tags_(tags), audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] VoidResult execute(const DeleteTagCommand& command);

private:
    domain::ITagRepository& tags_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class ReplaceTransactionTagsUseCase {
public:
    ReplaceTransactionTagsUseCase(
        domain::ITagRepository& tags,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : tags_(tags), audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] Result<std::vector<TagDto>> execute(
        const ReplaceTransactionTagsCommand& command);

private:
    domain::ITagRepository& tags_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class GetUserPreferenceUseCase {
public:
    explicit GetUserPreferenceUseCase(
        domain::IUserPreferenceRepository& preferences)
        : preferences_(preferences) {}

    [[nodiscard]] Result<UserPreferenceDto> execute(domain::UserId user_id);

private:
    domain::IUserPreferenceRepository& preferences_;
};

class UpdateUserPreferenceUseCase {
public:
    UpdateUserPreferenceUseCase(
        domain::IUserPreferenceRepository& preferences,
        domain::IAuditLogRepository& audit_logs,
        IUnitOfWork& uow)
        : preferences_(preferences), audit_logs_(audit_logs), uow_(uow) {}

    [[nodiscard]] Result<UserPreferenceDto> execute(
        const UpdateUserPreferenceCommand& command);

private:
    domain::IUserPreferenceRepository& preferences_;
    domain::IAuditLogRepository& audit_logs_;
    IUnitOfWork& uow_;
};

class ListCurrenciesUseCase {
public:
    [[nodiscard]] Result<std::vector<CurrencyMetadataDto>> execute() const;
};

} // namespace pfh::application
