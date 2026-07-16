// Personal Finance Hub - Foundational Resource Use Cases

#include "pfh/application/use_cases/resource_use_cases.h"

#include "pfh/application/error_mapping.h"
#include "pfh/application/input_constraints.h"
#include "pfh/domain/events/domain_events.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <utility>

namespace pfh::application {

namespace {

using TimePoint = std::chrono::system_clock::time_point;

[[nodiscard]] AccountDto account_dto(
    const domain::Account& account,
    std::optional<domain::AccountId> assigned_id = std::nullopt) {
    return AccountDto{
        assigned_id.value_or(account.id()),
        account.owner(),
        account.name(),
        account.type(),
        account.subtype(),
        account.category(),
        account.currency().code(),
        account.description(),
        account.is_archived(),
        account.archived_at(),
        account.created_at(),
        account.updated_at(),
        account.version()};
}

[[nodiscard]] CategoryDto category_dto(
    const domain::Category& category,
    std::optional<domain::CategoryId> assigned_id = std::nullopt) {
    return CategoryDto{
        assigned_id.value_or(category.id()),
        category.name(),
        category.board(),
        category.source(),
        category.parent_id(),
        category.template_id(),
        category.sort_order()};
}

[[nodiscard]] UserPreferenceDto preference_dto(
    const domain::UserPreference& preference) {
    return UserPreferenceDto{
        preference.base_currency().code(),
        preference.locale(),
        preference.timezone(),
        preference.date_format(),
        preference.number_format(),
        preference.theme(),
        preference.default_home_page(),
        preference.default_report_period()};
}

[[nodiscard]] std::string json_quoted(std::string_view value) {
    return domain::event_detail::json_string(value);
}

[[nodiscard]] domain::AuditLogEntry audit_entry(
    domain::UserId user_id,
    domain::AuditAction action,
    std::string resource_type,
    std::string resource_id,
    std::string before,
    std::string after,
    TimePoint occurred_at) {
    domain::AuditLogEntry entry;
    entry.operator_user_id = user_id;
    entry.action = action;
    entry.resource_type = std::move(resource_type);
    entry.resource_id = std::move(resource_id);
    entry.before_value_json = std::move(before);
    entry.after_value_json = std::move(after);
    entry.metadata_json = "{}";
    entry.occurred_at = occurred_at;
    return entry;
}

[[nodiscard]] bool valid_text(
    const std::string& value,
    std::size_t maximum,
    bool allow_empty = false) {
    return (allow_empty || !value.empty()) && value.size() <= maximum;
}

[[nodiscard]] bool valid_account_type(domain::AccountType value) noexcept {
    switch (value) {
    case domain::AccountType::Cash:
    case domain::AccountType::Savings:
    case domain::AccountType::Credit:
    case domain::AccountType::DigitalWallet:
    case domain::AccountType::Investment:
    case domain::AccountType::Crypto:
    case domain::AccountType::Other:
        return true;
    }
    return false;
}

[[nodiscard]] bool valid_category_board(domain::CategoryBoard value) noexcept {
    return value == domain::CategoryBoard::Income ||
           value == domain::CategoryBoard::Expense;
}

[[nodiscard]] bool valid_preference_enums(
    domain::ThemeMode theme,
    domain::HomePage home_page,
    domain::ReportPeriod report_period) noexcept {
    const bool valid_theme = theme == domain::ThemeMode::System ||
        theme == domain::ThemeMode::Light || theme == domain::ThemeMode::Dark;
    const bool valid_home = home_page == domain::HomePage::Dashboard ||
        home_page == domain::HomePage::Transactions ||
        home_page == domain::HomePage::Reports ||
        home_page == domain::HomePage::Accounts;
    const bool valid_period = report_period == domain::ReportPeriod::CurrentMonth ||
        report_period == domain::ReportPeriod::LastMonth ||
        report_period == domain::ReportPeriod::Last3Months ||
        report_period == domain::ReportPeriod::CurrentYear ||
        report_period == domain::ReportPeriod::Custom;
    return valid_theme && valid_home && valid_period;
}

} // namespace

Result<AccountDto> CreateAccountUseCase::execute(
    const CreateAccountCommand& command) {
    if (!command.user_id.is_valid() || !valid_account_type(command.type) ||
        !valid_text(command.name, 128) ||
        !valid_text(command.subtype, 64) ||
        !valid_text(command.description, 4096, true)) {
        return err(Error::validation("Account fields are invalid"));
    }
    auto currency = domain::Currency::create(command.currency_code);
    if (!currency) {
        return err(from_domain(currency.error()));
    }

    const auto now = std::chrono::system_clock::now();
    const domain::Account account(
        domain::AccountId{}, command.user_id, command.name, command.type,
        command.subtype, *currency, command.description, false, std::nullopt,
        now, now, 1, command.category);
    domain::AccountId persisted_id;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto saved = accounts_.save(tx, account);
            if (!saved) {
                return std::unexpected(saved.error());
            }
            persisted_id = *saved;
            return audit_logs_.append(
                tx,
                audit_entry(
                    command.user_id,
                    domain::AuditAction::Create,
                    "Account",
                    persisted_id.to_string(),
                    {},
                    "{\"name\":" + json_quoted(command.name) + "}",
                    now));
        });
    if (!write) {
        return err(from_repository(write.error()));
    }
    return account_dto(account, persisted_id);
}

VoidResult ArchiveAccountUseCase::execute(
    const ArchiveAccountCommand& command) {
    if (!command.user_id.is_valid() || !command.account_id.is_valid() ||
        command.expected_version <= 0) {
        return err(Error::validation("Account id is invalid"));
    }
    const auto archived_at = command.archived_at.value_or(
        std::chrono::system_clock::now());
    std::optional<Error> app_error;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto account = accounts_.find_by_id_for_update(
                tx, command.account_id, command.user_id);
            if (!account) {
                return std::unexpected(account.error());
            }
            if (account->is_archived()) {
                app_error = Error::conflict("Account is already archived");
                return std::unexpected(domain::RepositoryError::conflict(
                    "Account is already archived"));
            }
            if (account->version() != command.expected_version) {
                app_error = Error::conflict("Account version conflict");
                return std::unexpected(domain::RepositoryError::conflict(
                    "Account version conflict"));
            }
            account->archive(archived_at);
            if (auto saved = accounts_.save(tx, *account); !saved) {
                return std::unexpected(saved.error());
            }
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id,
                        domain::AuditAction::Archive,
                        "Account",
                        command.account_id.to_string(),
                        "{\"isArchived\":false}",
                        "{\"isArchived\":true}",
                        archived_at));
                !audited) {
                return audited;
            }
            uow_.register_event(std::make_shared<domain::AccountArchivedEvent>(
                command.user_id, command.account_id, archived_at));
            return {};
        });
    if (!write) {
        return app_error.has_value() ? err(*app_error)
                                     : err(from_repository(write.error()));
    }
    return ok();
}

Result<AccountDto> UpdateAccountUseCase::execute(
    const UpdateAccountCommand& command) {
    const bool valid_category = command.category == domain::AccountCategory::Asset ||
        command.category == domain::AccountCategory::Liability;
    if (!command.user_id.is_valid() || !command.account_id.is_valid() ||
        command.expected_version <= 0 || !valid_account_type(command.type) ||
        !valid_category || !valid_text(command.name, 128) ||
        !valid_text(command.subtype, 64) ||
        !valid_text(command.description, 4096, true)) {
        return err(Error::validation("Account fields are invalid"));
    }
    auto currency = domain::Currency::create(command.currency_code);
    if (!currency) return err(from_domain(currency.error()));

    const auto now = std::chrono::system_clock::now();
    std::optional<Error> app_error;
    std::optional<domain::Account> updated;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto current = accounts_.find_by_id_for_update(
                tx, command.account_id, command.user_id);
            if (!current) return std::unexpected(current.error());
            if (current->version() != command.expected_version) {
                app_error = Error::conflict("Account version conflict");
                return std::unexpected(domain::RepositoryError::conflict(
                    "Account version conflict"));
            }
            if (current->currency().code() != currency->code()) {
                auto has_history = accounts_.has_transactions(tx, command.account_id);
                if (!has_history) return std::unexpected(has_history.error());
                if (*has_history) {
                    app_error = Error::domain_rule_violation(
                        "Account currency cannot change after transactions exist");
                    return std::unexpected(domain::RepositoryError::validation(
                        "Account currency is immutable after first transaction"));
                }
            }

            domain::Account next(
                current->id(), current->owner(), command.name, command.type,
                command.subtype, *currency, command.description,
                current->is_archived(), current->archived_at(),
                current->created_at(), now, current->version(), command.category);
            if (auto saved = accounts_.save(tx, next); !saved) {
                return std::unexpected(saved.error());
            }
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id, domain::AuditAction::Update, "Account",
                        command.account_id.to_string(),
                        "{\"name\":" + json_quoted(current->name()) +
                            ",\"currencyCode\":" +
                            json_quoted(current->currency().code()) + "}",
                        "{\"name\":" + json_quoted(command.name) +
                            ",\"currencyCode\":" +
                            json_quoted(currency->code()) + "}",
                        now));
                !audited) {
                return audited;
            }
            uow_.register_event(std::make_shared<domain::AccountUpdatedEvent>(
                command.user_id, command.account_id, now));
            updated.emplace(
                current->id(), current->owner(), command.name, command.type,
                command.subtype, *currency, command.description,
                current->is_archived(), current->archived_at(),
                current->created_at(), now, current->version() + 1, command.category);
            return {};
        });
    if (!write) {
        return app_error.has_value() ? err(*app_error)
                                     : err(from_repository(write.error()));
    }
    return account_dto(*updated);
}

VoidResult RestoreAccountUseCase::execute(
    const RestoreAccountCommand& command) {
    if (!command.user_id.is_valid() || !command.account_id.is_valid() ||
        command.expected_version <= 0) {
        return err(Error::validation("Account id or version is invalid"));
    }
    const auto restored_at = command.restored_at.value_or(
        std::chrono::system_clock::now());
    std::optional<Error> app_error;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto account = accounts_.find_by_id_for_update(
                tx, command.account_id, command.user_id);
            if (!account) return std::unexpected(account.error());
            if (!account->is_archived()) {
                app_error = Error::conflict("Account is not archived");
                return std::unexpected(domain::RepositoryError::conflict(
                    "Account is not archived"));
            }
            if (account->version() != command.expected_version) {
                app_error = Error::conflict("Account version conflict");
                return std::unexpected(domain::RepositoryError::conflict(
                    "Account version conflict"));
            }
            account->unarchive(restored_at);
            if (auto saved = accounts_.save(tx, *account); !saved) {
                return std::unexpected(saved.error());
            }
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id, domain::AuditAction::Update, "Account",
                        command.account_id.to_string(),
                        "{\"isArchived\":true}",
                        "{\"isArchived\":false}", restored_at));
                !audited) {
                return audited;
            }
            uow_.register_event(std::make_shared<domain::AccountRestoredEvent>(
                command.user_id, command.account_id, restored_at));
            return {};
        });
    if (!write) {
        return app_error.has_value() ? err(*app_error)
                                     : err(from_repository(write.error()));
    }
    return ok();
}

Result<std::vector<CategoryTreeDto>> ListCategoriesUseCase::execute(
    domain::UserId user_id,
    std::optional<domain::CategoryBoard> board) {
    if (!user_id.is_valid() ||
        (board.has_value() && !valid_category_board(*board))) {
        return err(Error::validation("User id or category board is invalid"));
    }
    auto categories = board.has_value()
        ? categories_.find_by_board(user_id, *board)
        : categories_.find_all_for_user(user_id);
    if (!categories) {
        return err(from_repository(categories.error()));
    }

    std::map<std::int64_t, const domain::Category*> by_id;
    for (const auto& category : *categories) {
        by_id.emplace(category.id().value(), &category);
    }
    for (const auto& category : *categories) {
        if (category.parent_id().has_value() &&
            !by_id.contains(category.parent_id()->value())) {
            return err(Error::infrastructure_failure(
                "Category tree contains an unavailable parent"));
        }
    }

    std::set<std::int64_t> active_path;
    std::set<std::int64_t> completed;
    std::function<Result<CategoryTreeDto>(const domain::Category&, int)> build =
        [&](const domain::Category& category, int depth)
            -> Result<CategoryTreeDto> {
        if (depth >= domain::kMaxCategoryTreeDepth ||
            !active_path.insert(category.id().value()).second) {
            return err(Error::infrastructure_failure(
                "Category tree contains a cycle"));
        }
        CategoryTreeDto node;
        static_cast<CategoryDto&>(node) = category_dto(category);
        for (const auto& candidate : *categories) {
            if (candidate.parent_id() ==
                std::optional<domain::CategoryId>(category.id())) {
                auto child = build(candidate, depth + 1);
                if (!child) {
                    active_path.erase(category.id().value());
                    return err(child.error());
                }
                node.children.push_back(std::move(*child));
            }
        }
        active_path.erase(category.id().value());
        completed.insert(category.id().value());
        return node;
    };

    std::vector<CategoryTreeDto> roots;
    for (const auto& category : *categories) {
        if (!category.parent_id().has_value()) {
            auto root = build(category, 0);
            if (!root) {
                return err(root.error());
            }
            roots.push_back(std::move(*root));
        }
    }
    if (completed.size() != categories->size()) {
        return err(Error::infrastructure_failure(
            "Category tree contains a cycle without a root"));
    }
    return roots;
}

Result<CategoryDto> CreateCategoryUseCase::execute(
    const CreateCategoryCommand& command) {
    if (!command.user_id.is_valid()) {
        return err(Error::validation("User id is invalid"));
    }
    if (command.board.has_value() && !valid_category_board(*command.board)) {
        return err(Error::validation("Category board is invalid"));
    }

    std::string name;
    std::optional<domain::CategoryBoard> board = command.board;
    domain::CategorySource source = domain::CategorySource::User;
    int sort_order = 0;
    std::optional<domain::SystemCategoryTemplate> category_template;

    if (command.template_id.has_value()) {
        if (*command.template_id <= 0) {
            return err(Error::validation("templateId must be a positive integer"));
        }
        auto preference = preferences_.find_by_user(command.user_id);
        if (!preference) {
            return err(from_repository(preference.error()));
        }
        auto loaded = categories_.find_template_by_id(
            *command.template_id, preference->locale());
        if (!loaded) {
            return err(from_repository(loaded.error()));
        }
        if (!loaded->is_selectable || !loaded->default_board.has_value()) {
            return err(Error::domain_rule_violation(
                "The selected category template cannot be activated"));
        }
        if (command.name.has_value() && *command.name != loaded->name) {
            return err(Error::validation(
                "name must match the selected category template"));
        }
        if (board.has_value() && board != loaded->default_board) {
            return err(Error::validation(
                "board must match the selected category template"));
        }
        name = loaded->name;
        board = loaded->default_board;
        source = domain::CategorySource::System;
        sort_order = loaded->sort_order;
        category_template = std::move(*loaded);
    } else {
        if (!command.name.has_value() || !board.has_value()) {
            return err(Error::validation(
                "Custom categories require name and board"));
        }
        name = *command.name;
    }

    if (!valid_text(name, 128)) {
        return err(Error::validation("Category name is invalid"));
    }

    const auto now = std::chrono::system_clock::now();
    const domain::Category category(
        domain::CategoryId{}, command.user_id, name, *board,
        command.parent_id, source, command.template_id, sort_order,
        std::nullopt, now, now);
    domain::CategoryId persisted_id;
    std::optional<Error> app_error;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            if (category_template.has_value()) {
                const bool template_has_parent =
                    category_template->parent_id.has_value();
                if (template_has_parent != command.parent_id.has_value()) {
                    app_error = Error::validation(
                        template_has_parent
                            ? "A child template requires its activated parentId"
                            : "A root template cannot have parentId");
                    return std::unexpected(domain::RepositoryError::validation(
                        "Invalid template parent"));
                }
                if (command.parent_id.has_value()) {
                    auto parent = categories_.find_by_id_for_user_for_update(
                        tx, *command.parent_id, command.user_id);
                    if (!parent) {
                        return std::unexpected(parent.error());
                    }
                    if (parent->source() != domain::CategorySource::System ||
                        parent->template_id() != category_template->parent_id) {
                        app_error = Error::domain_rule_violation(
                            "Template parentId does not match the template hierarchy");
                        return std::unexpected(domain::RepositoryError::validation(
                            "Template parent mismatch"));
                    }
                }
            } else if (command.parent_id.has_value()) {
                auto parent = categories_.find_by_id_for_user_for_update(
                    tx, *command.parent_id, command.user_id);
                if (!parent) {
                    return std::unexpected(parent.error());
                }
                if (parent->board() != *board) {
                    app_error = Error::domain_rule_violation(
                        "Parent category must use the same board");
                    return std::unexpected(domain::RepositoryError::validation(
                        "Category board mismatch"));
                }
            }

            auto saved = categories_.save(tx, category);
            if (!saved) {
                return std::unexpected(saved.error());
            }
            persisted_id = *saved;
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id,
                        domain::AuditAction::Create,
                        "Category",
                        persisted_id.to_string(),
                        {},
                        "{\"name\":" + json_quoted(name) + "}",
                        now));
                !audited) {
                return audited;
            }
            uow_.register_event(std::make_shared<domain::CategoryCreatedEvent>(
                command.user_id, persisted_id, *board, now));
            return {};
        });
    if (!write) {
        return app_error.has_value() ? err(*app_error)
                                     : err(from_repository(write.error()));
    }
    return category_dto(category, persisted_id);
}

VoidResult DeleteCategoryUseCase::execute(
    const DeleteCategoryCommand& command) {
    if (!command.user_id.is_valid() || !command.category_id.is_valid()) {
        return err(Error::validation("Category id is invalid"));
    }
    const auto deleted_at = command.deleted_at.value_or(
        std::chrono::system_clock::now());
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto category = categories_.find_by_id_for_user_for_update(
                tx, command.category_id, command.user_id);
            if (!category) {
                return std::unexpected(category.error());
            }
            if (auto deleted = categories_.soft_delete(
                    tx, command.category_id, command.user_id, deleted_at);
                !deleted) {
                return deleted;
            }
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id,
                        domain::AuditAction::Delete,
                        "Category",
                        command.category_id.to_string(),
                        "{\"name\":" + json_quoted(category->name()) + "}",
                        "{\"deleted\":true}",
                        deleted_at));
                !audited) {
                return audited;
            }
            uow_.register_event(std::make_shared<domain::CategoryDeletedEvent>(
                command.user_id, command.category_id, category->board(), deleted_at));
            return {};
        });
    return write ? ok() : err(from_repository(write.error()));
}

Result<std::vector<TagDto>> ListTagsUseCase::execute(domain::UserId user_id) {
    if (!user_id.is_valid()) {
        return err(Error::validation("User id is invalid"));
    }
    auto tags = tags_.find_by_user(user_id, false);
    if (!tags) {
        return err(from_repository(tags.error()));
    }
    std::vector<TagDto> result;
    result.reserve(tags->size());
    for (const auto& tag : *tags) {
        result.push_back(TagDto{tag.id(), tag.name()});
    }
    return result;
}

Result<TagDto> CreateTagUseCase::execute(const CreateTagCommand& command) {
    if (!command.user_id.is_valid() || !valid_text(command.name, 64)) {
        return err(Error::validation("Tag name is invalid"));
    }
    const auto now = std::chrono::system_clock::now();
    const domain::Tag tag(
        domain::TagId{}, command.user_id, command.name,
        std::nullopt, now, now);
    domain::TagId persisted_id;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto saved = tags_.save(tx, tag);
            if (!saved) {
                return std::unexpected(saved.error());
            }
            persisted_id = *saved;
            return audit_logs_.append(
                tx,
                audit_entry(
                    command.user_id,
                    domain::AuditAction::Create,
                    "Tag",
                    persisted_id.to_string(),
                    {},
                    "{\"name\":" + json_quoted(command.name) + "}",
                    now));
        });
    if (!write) {
        return err(from_repository(write.error()));
    }
    return TagDto{persisted_id, command.name};
}

VoidResult DeleteTagUseCase::execute(const DeleteTagCommand& command) {
    if (!command.user_id.is_valid() || !command.tag_id.is_valid()) {
        return err(Error::validation("Tag id is invalid"));
    }
    const auto deleted_at = command.deleted_at.value_or(
        std::chrono::system_clock::now());
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto tag = tags_.find_by_id_for_user_for_update(
                tx, command.tag_id, command.user_id);
            if (!tag) {
                return std::unexpected(tag.error());
            }
            if (auto deleted = tags_.soft_delete(
                    tx, command.tag_id, command.user_id, deleted_at);
                !deleted) {
                return deleted;
            }
            return audit_logs_.append(
                tx,
                audit_entry(
                    command.user_id,
                    domain::AuditAction::Delete,
                    "Tag",
                    command.tag_id.to_string(),
                    "{\"name\":" + json_quoted(tag->name()) + "}",
                    "{\"deleted\":true}",
                    deleted_at));
        });
    return write ? ok() : err(from_repository(write.error()));
}

Result<std::vector<TagDto>> ReplaceTransactionTagsUseCase::execute(
    const ReplaceTransactionTagsCommand& command) {
    if (!command.user_id.is_valid() || !command.transaction_id.is_valid()) {
        return err(Error::validation("Transaction id is invalid"));
    }
    if (command.tag_ids.size() > domain::kMaxTagsPerTransaction) {
        return err(Error::validation("tagIds must contain at most 64 items"));
    }
    std::set<std::int64_t> unique;
    for (const auto id : command.tag_ids) {
        if (!id.is_valid() || !unique.insert(id.value()).second) {
            return err(Error::validation("tagIds must contain unique positive integers"));
        }
    }

    const auto now = std::chrono::system_clock::now();
    std::vector<domain::Tag> persisted_tags;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto replaced = tags_.replace_transaction_tags(
                tx, command.transaction_id, command.user_id, command.tag_ids);
            if (!replaced) {
                return std::unexpected(replaced.error());
            }
            persisted_tags = std::move(*replaced);
            return audit_logs_.append(
                tx,
                audit_entry(
                    command.user_id,
                    domain::AuditAction::Update,
                    "TransactionTags",
                    command.transaction_id.to_string(),
                    {},
                    "{\"tagCount\":" + std::to_string(command.tag_ids.size()) + "}",
                    now));
        });
    if (!write) {
        return err(from_repository(write.error()));
    }
    std::vector<TagDto> result;
    result.reserve(persisted_tags.size());
    for (const auto& tag : persisted_tags) {
        result.push_back(TagDto{tag.id(), tag.name()});
    }
    return result;
}

Result<UserPreferenceDto> GetUserPreferenceUseCase::execute(
    domain::UserId user_id) {
    if (!user_id.is_valid()) {
        return err(Error::validation("User id is invalid"));
    }
    auto preference = preferences_.find_by_user(user_id);
    return preference ? Result<UserPreferenceDto>(preference_dto(*preference))
                      : Result<UserPreferenceDto>(
                            std::unexpected(from_repository(preference.error())));
}

Result<UserPreferenceDto> UpdateUserPreferenceUseCase::execute(
    const UpdateUserPreferenceCommand& command) {
    if (!command.user_id.is_valid() || !is_locale_tag(command.locale) ||
        !valid_text(command.timezone, 64) ||
        !valid_text(command.date_format, 32) ||
        !valid_text(command.number_format, 32) ||
        !valid_preference_enums(
            command.theme,
            command.default_home_page,
            command.default_report_period)) {
        return err(Error::validation("User preference fields are invalid"));
    }
    auto currency = domain::Currency::create(command.base_currency);
    if (!currency) {
        return err(from_domain(currency.error()));
    }
    try {
        (void)std::chrono::locate_zone(command.timezone);
    } catch (const std::exception&) {
        return err(Error::validation("timezone is not available in the IANA database"));
    }

    const domain::UserPreference preference(
        command.user_id, *currency, command.locale, command.timezone,
        command.date_format, command.number_format, command.theme,
        command.default_home_page, command.default_report_period);
    const auto now = std::chrono::system_clock::now();
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto before = preferences_.find_by_user(tx, command.user_id);
            if (!before) {
                return std::unexpected(before.error());
            }
            if (auto saved = preferences_.save(tx, preference); !saved) {
                return saved;
            }
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id,
                        domain::AuditAction::Update,
                        "UserPreference",
                        command.user_id.to_string(),
                        "{\"baseCurrency\":" +
                            json_quoted(before->base_currency().code()) + "}",
                        "{\"baseCurrency\":" +
                            json_quoted(preference.base_currency().code()) + "}",
                        now));
                !audited) {
                return audited;
            }
            uow_.register_event(
                std::make_shared<domain::UserPreferenceUpdatedEvent>(
                    command.user_id, now));
            return {};
        });
    if (!write) {
        return err(from_repository(write.error()));
    }
    return preference_dto(preference);
}

Result<std::vector<CurrencyMetadataDto>> ListCurrenciesUseCase::execute() const {
    std::vector<CurrencyMetadataDto> result;
    result.reserve(domain::Currency::catalog().size());
    for (const auto& metadata : domain::Currency::catalog()) {
        result.push_back(CurrencyMetadataDto{
            std::string(metadata.code),
            std::string(metadata.symbol),
            static_cast<int>(metadata.precision),
            std::string(metadata.display_name),
            metadata.is_crypto});
    }
    return result;
}

} // namespace pfh::application
