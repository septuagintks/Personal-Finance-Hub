// Personal Finance Hub - Foundational Resource Use Cases

#include "pfh/application/use_cases/resource_use_cases.h"

#include "pfh/application/error_mapping.h"
#include "pfh/application/input_constraints.h"
#include "pfh/application/persistence_time.h"
#include "pfh/domain/events/domain_events.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <exception>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <unordered_set>
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
        category.sort_order(),
        category.is_deleted(),
        category.deleted_at(),
        category.created_at(),
        category.updated_at()};
}

[[nodiscard]] TagDto tag_dto(const domain::Tag& tag) {
    return TagDto{
        tag.id(), tag.name(), tag.is_deleted(), tag.deleted_at(),
        tag.created_at(), tag.updated_at()};
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
        preference.default_report_period(),
        preference.custom_report_start_month(),
        preference.custom_report_end_month()};
}

[[nodiscard]] std::string json_quoted(std::string_view value) {
    return domain::event_detail::json_string(value);
}

[[nodiscard]] std::string_view account_type_text(
    domain::AccountType value) noexcept {
    switch (value) {
    case domain::AccountType::Cash: return "cash";
    case domain::AccountType::Savings: return "savings";
    case domain::AccountType::Credit: return "credit";
    case domain::AccountType::DigitalWallet: return "digital_wallet";
    case domain::AccountType::Investment: return "investment";
    case domain::AccountType::Crypto: return "crypto";
    case domain::AccountType::Other: return "other";
    }
    return "other";
}

[[nodiscard]] std::string_view account_category_text(
    domain::AccountCategory value) noexcept {
    return value == domain::AccountCategory::Asset ? "asset" : "liability";
}

[[nodiscard]] std::string account_audit_snapshot(
    const domain::Account& account) {
    return "{\"name\":" + json_quoted(account.name()) +
        ",\"type\":" + json_quoted(account_type_text(account.type())) +
        ",\"subtype\":" + json_quoted(account.subtype()) +
        ",\"category\":" + json_quoted(account_category_text(account.category())) +
        ",\"currencyCode\":" + json_quoted(account.currency().code()) +
        ",\"description\":" + json_quoted(account.description()) +
        ",\"isArchived\":" + (account.is_archived() ? "true" : "false") +
        "}";
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
    domain::ReportPeriod report_period,
    domain::NumberFormat number_format) noexcept {
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
    const bool valid_number_format =
        number_format == domain::NumberFormat::CommaDot ||
        number_format == domain::NumberFormat::DotComma ||
        number_format == domain::NumberFormat::SpaceComma;
    return valid_theme && valid_home && valid_period && valid_number_format;
}

[[nodiscard]] bool valid_custom_report_months(
    domain::ReportPeriod period,
    std::optional<domain::ReportMonth> start,
    std::optional<domain::ReportMonth> end) noexcept {
    if (period != domain::ReportPeriod::Custom) {
        return !start.has_value() && !end.has_value();
    }
    if (!start.has_value() || !end.has_value() || !start->ok() || !end->ok() ||
        int(start->year()) < 1 || int(end->year()) < 1 || *start > *end) {
        return false;
    }
    const auto ordinal = [](domain::ReportMonth value) {
        return static_cast<std::int64_t>(int(value.year())) * 12 +
            static_cast<std::int64_t>(unsigned(value.month())) - 1;
    };
    return ordinal(*end) - ordinal(*start) < 120;
}

[[nodiscard]] std::optional<std::string_view> required_value(
    const IdempotencyValues& values,
    std::string_view name) {
    return idempotency_value(values, name);
}

[[nodiscard]] std::string optional_id_value(
    const auto& value) {
    return value.has_value() ? value->to_string() : "null";
}

[[nodiscard]] std::string optional_integer_value(
    const std::optional<std::int64_t>& value) {
    return value.has_value() ? std::to_string(*value) : "null";
}

[[nodiscard]] std::string optional_time_value(
    const std::optional<TimePoint>& value) {
    return value.has_value() ? encode_idempotency_time(*value) : "null";
}

[[nodiscard]] IdempotencyValues account_values(const AccountDto& value) {
    return {
        {"id", value.id.to_string()},
        {"owner", value.owner.to_string()},
        {"name", value.name},
        {"type", std::to_string(static_cast<int>(value.type))},
        {"subtype", value.subtype},
        {"category", std::to_string(static_cast<int>(value.category))},
        {"currency", value.currency_code},
        {"description", value.description},
        {"archived", value.is_archived ? "true" : "false"},
        {"archivedAt", optional_time_value(value.archived_at)},
        {"createdAt", encode_idempotency_time(value.created_at)},
        {"updatedAt", encode_idempotency_time(value.updated_at)},
        {"version", std::to_string(value.version)}};
}

[[nodiscard]] Result<AccountDto> account_from_values(
    const IdempotencyValues& values) {
    const auto id = required_value(values, "id");
    const auto owner = required_value(values, "owner");
    const auto name = required_value(values, "name");
    const auto type = required_value(values, "type");
    const auto subtype = required_value(values, "subtype");
    const auto category = required_value(values, "category");
    const auto currency = required_value(values, "currency");
    const auto description = required_value(values, "description");
    const auto archived = required_value(values, "archived");
    const auto archived_at = required_value(values, "archivedAt");
    const auto created_at = required_value(values, "createdAt");
    const auto updated_at = required_value(values, "updatedAt");
    const auto version = required_value(values, "version");
    if (!id || !owner || !name || !type || !subtype || !category ||
        !currency || !description || !archived || !archived_at || !created_at ||
        !updated_at || !version) {
        return err(Error::infrastructure_failure(
            "Stored account idempotency response is invalid"));
    }
    const auto id_value = parse_idempotency_integer(*id);
    const auto owner_value = parse_idempotency_integer(*owner);
    const auto type_value = parse_idempotency_integer(*type);
    const auto category_value = parse_idempotency_integer(*category);
    const auto version_value = parse_idempotency_integer(*version);
    const auto created_value = decode_idempotency_time(*created_at);
    const auto updated_value = decode_idempotency_time(*updated_at);
    std::optional<TimePoint> archived_value;
    if (*archived_at != "null") archived_value = decode_idempotency_time(*archived_at);
    if (!id_value || !owner_value || !type_value || !category_value ||
        !version_value || !created_value || !updated_value ||
        (*archived_at != "null" && !archived_value.has_value())) {
        return err(Error::infrastructure_failure(
            "Stored account idempotency response is invalid"));
    }
    const auto account_type = static_cast<domain::AccountType>(*type_value);
    const auto account_category =
        static_cast<domain::AccountCategory>(*category_value);
    if (!domain::AccountId(*id_value).is_valid() ||
        !domain::UserId(*owner_value).is_valid() ||
        !valid_account_type(account_type) ||
        (account_category != domain::AccountCategory::Asset &&
         account_category != domain::AccountCategory::Liability) ||
        (*archived != "true" && *archived != "false")) {
        return err(Error::infrastructure_failure(
            "Stored account idempotency response is invalid"));
    }
    return AccountDto{
        domain::AccountId(*id_value), domain::UserId(*owner_value),
        std::string(*name), account_type, std::string(*subtype),
        account_category, std::string(*currency), std::string(*description),
        *archived == "true", archived_value, *created_value, *updated_value,
        *version_value};
}

[[nodiscard]] IdempotencyValues category_values(const CategoryDto& value) {
    return {
        {"id", value.id.to_string()},
        {"name", value.name},
        {"board", std::to_string(static_cast<int>(value.board))},
        {"source", std::to_string(static_cast<int>(value.source))},
        {"parentId", optional_id_value(value.parent_id)},
        {"templateId", optional_integer_value(value.template_id)},
        {"sortOrder", std::to_string(value.sort_order)},
        {"deleted", value.is_deleted ? "true" : "false"},
        {"deletedAt", optional_time_value(value.deleted_at)},
        {"createdAt", encode_idempotency_time(value.created_at)},
        {"updatedAt", encode_idempotency_time(value.updated_at)}};
}

[[nodiscard]] Result<CategoryDto> category_from_values(
    const IdempotencyValues& values) {
    const std::array names{
        "id", "name", "board", "source", "parentId", "templateId",
        "sortOrder", "deleted", "deletedAt", "createdAt", "updatedAt"};
    for (const auto* name : names) {
        if (!required_value(values, name)) {
            return err(Error::infrastructure_failure(
                "Stored category idempotency response is invalid"));
        }
    }
    const auto id = parse_idempotency_integer(*required_value(values, "id"));
    const auto board = parse_idempotency_integer(*required_value(values, "board"));
    const auto source = parse_idempotency_integer(*required_value(values, "source"));
    const auto sort = parse_idempotency_integer(*required_value(values, "sortOrder"));
    const auto created = decode_idempotency_time(
        *required_value(values, "createdAt"));
    const auto updated = decode_idempotency_time(
        *required_value(values, "updatedAt"));
    const auto parent_text = *required_value(values, "parentId");
    const auto template_text = *required_value(values, "templateId");
    const auto deleted_text = *required_value(values, "deleted");
    const auto deleted_at_text = *required_value(values, "deletedAt");
    std::optional<domain::CategoryId> parent;
    std::optional<std::int64_t> template_id;
    std::optional<TimePoint> deleted_at;
    if (parent_text != "null") {
        const auto parsed = parse_idempotency_integer(parent_text);
        if (parsed) parent = domain::CategoryId(*parsed);
    }
    if (template_text != "null") template_id = parse_idempotency_integer(template_text);
    if (deleted_at_text != "null") deleted_at = decode_idempotency_time(deleted_at_text);
    const auto board_value = board.has_value()
        ? static_cast<domain::CategoryBoard>(*board)
        : domain::CategoryBoard::Expense;
    const auto source_value = source.has_value()
        ? static_cast<domain::CategorySource>(*source)
        : domain::CategorySource::User;
    if (!id || !domain::CategoryId(*id).is_valid() || !board ||
        !valid_category_board(board_value) || !source ||
        (source_value != domain::CategorySource::System &&
         source_value != domain::CategorySource::User) || !sort || !created ||
        !updated || (parent_text != "null" && !parent.has_value()) ||
        (template_text != "null" && !template_id.has_value()) ||
        (deleted_text != "true" && deleted_text != "false") ||
        (deleted_at_text != "null" && !deleted_at.has_value())) {
        return err(Error::infrastructure_failure(
            "Stored category idempotency response is invalid"));
    }
    return CategoryDto{
        domain::CategoryId(*id),
        std::string(*required_value(values, "name")),
        board_value,
        source_value,
        parent,
        template_id,
        static_cast<int>(*sort),
        deleted_text == "true",
        deleted_at,
        *created,
        *updated};
}

[[nodiscard]] IdempotencyValues tag_values(const TagDto& value) {
    return {
        {"id", value.id.to_string()},
        {"name", value.name},
        {"deleted", value.is_deleted ? "true" : "false"},
        {"deletedAt", optional_time_value(value.deleted_at)},
        {"createdAt", encode_idempotency_time(value.created_at)},
        {"updatedAt", encode_idempotency_time(value.updated_at)}};
}

[[nodiscard]] Result<TagDto> tag_from_values(const IdempotencyValues& values) {
    const auto id = required_value(values, "id");
    const auto name = required_value(values, "name");
    const auto deleted = required_value(values, "deleted");
    const auto deleted_at = required_value(values, "deletedAt");
    const auto created = required_value(values, "createdAt");
    const auto updated = required_value(values, "updatedAt");
    if (!id || !name || !deleted || !deleted_at || !created || !updated) {
        return err(Error::infrastructure_failure(
            "Stored tag idempotency response is invalid"));
    }
    const auto id_value = parse_idempotency_integer(*id);
    const auto created_value = decode_idempotency_time(*created);
    const auto updated_value = decode_idempotency_time(*updated);
    std::optional<TimePoint> deleted_value;
    if (*deleted_at != "null") deleted_value = decode_idempotency_time(*deleted_at);
    if (!id_value || !domain::TagId(*id_value).is_valid() || !created_value ||
        !updated_value || (*deleted != "true" && *deleted != "false") ||
        (*deleted_at != "null" && !deleted_value.has_value())) {
        return err(Error::infrastructure_failure(
            "Stored tag idempotency response is invalid"));
    }
    return TagDto{
        domain::TagId(*id_value), std::string(*name), *deleted == "true",
        deleted_value, *created_value, *updated_value};
}

} // namespace

Result<AccountDto> CreateAccountUseCase::execute(
    const CreateAccountCommand& command) {
    return execute_impl(command, std::nullopt);
}

Result<AccountDto> CreateAccountUseCase::execute(
    const CreateAccountCommand& command,
    const IdempotencyRequest& idempotency) {
    return execute_impl(command, idempotency);
}

Result<AccountDto> CreateAccountUseCase::execute_impl(
    const CreateAccountCommand& command,
    const std::optional<IdempotencyRequest>& idempotency) {
    if (idempotency.has_value()) {
        if (idempotency_ == nullptr) {
            return err(Error::infrastructure_failure(
                "Account idempotency is unavailable"));
        }
        if (auto valid = validate_idempotency_input(
                idempotency->key, idempotency->request_fingerprint);
            !valid) {
            return err(valid.error());
        }
    }
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

    const auto now = normalize_persisted_time(
        std::chrono::system_clock::now());
    const domain::Account account(
        domain::AccountId{}, command.user_id, command.name, command.type,
        command.subtype, *currency, command.description, false, std::nullopt,
        now, now, 1, command.category);
    domain::AccountId persisted_id;
    std::optional<AccountDto> result_dto;
    std::optional<Error> app_error;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            if (idempotency.has_value()) {
                auto started = idempotency_->begin(
                    tx,
                    command.user_id,
                    "create_account",
                    idempotency->key,
                    idempotency->request_fingerprint,
                    idempotency->created_at,
                    idempotency->created_at + kIdempotencyLifetime);
                if (!started) return std::unexpected(started.error());
                if (started->replay) {
                    auto restored = account_from_values(started->response_values);
                    if (!restored) {
                        app_error = restored.error();
                        return std::unexpected(domain::RepositoryError::database(
                            "Stored account idempotency response is invalid"));
                    }
                    result_dto = std::move(*restored);
                    return {};
                }
            }
            auto saved = accounts_.save(tx, account);
            if (!saved) {
                return std::unexpected(saved.error());
            }
            persisted_id = *saved;
            result_dto = account_dto(account, persisted_id);
            if (auto audited = audit_logs_.append(
                tx,
                audit_entry(
                    command.user_id,
                    domain::AuditAction::Create,
                    "Account",
                    persisted_id.to_string(),
                    {},
                    account_audit_snapshot(account),
                    now));
                !audited) {
                return audited;
            }
            if (idempotency.has_value()) {
                return idempotency_->complete(
                    tx,
                    command.user_id,
                    "create_account",
                    idempotency->key,
                    account_values(*result_dto));
            }
            return {};
        });
    if (!write) {
        if (app_error.has_value()) return err(*app_error);
        return err(from_repository(write.error()));
    }
    if (!result_dto.has_value()) {
        return err(Error::infrastructure_failure(
            "Account result was not produced"));
    }
    return *result_dto;
}

VoidResult ArchiveAccountUseCase::execute(
    const ArchiveAccountCommand& command) {
    if (!command.user_id.is_valid() || !command.account_id.is_valid() ||
        command.expected_version <= 0) {
        return err(Error::validation("Account id is invalid"));
    }
    const auto archived_at = normalize_persisted_time(
        command.archived_at.value_or(std::chrono::system_clock::now()));
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

    const auto now = normalize_persisted_time(
        std::chrono::system_clock::now());
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
                        account_audit_snapshot(*current),
                        account_audit_snapshot(next),
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
    const auto restored_at = normalize_persisted_time(
        command.restored_at.value_or(std::chrono::system_clock::now()));
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
    std::optional<domain::CategoryBoard> board,
    MetadataListStatus status) {
    if (!user_id.is_valid() ||
        (board.has_value() && !valid_category_board(*board))) {
        return err(Error::validation("User id or category board is invalid"));
    }
    auto categories = status == MetadataListStatus::Active
        ? (board.has_value()
              ? categories_.find_by_board(user_id, *board)
              : categories_.find_all_for_user(user_id))
        : categories_.find_all_for_user_including_deleted(user_id);
    if (!categories) {
        return err(from_repository(categories.error()));
    }
    std::erase_if(*categories, [&](const domain::Category& category) {
        if (board.has_value() && category.board() != *board) return true;
        return status == MetadataListStatus::Deleted && !category.is_deleted();
    });

    std::unordered_map<std::int64_t, const domain::Category*> by_id;
    std::unordered_map<
        std::int64_t, std::vector<const domain::Category*>> children_by_parent;
    by_id.reserve(categories->size());
    children_by_parent.reserve(categories->size());
    for (const auto& category : *categories) {
        by_id.emplace(category.id().value(), &category);
        if (category.parent_id().has_value()) {
            children_by_parent[category.parent_id()->value()].push_back(&category);
        }
    }
    std::unordered_set<std::int64_t> active_path;
    std::unordered_set<std::int64_t> completed;
    active_path.reserve(categories->size());
    completed.reserve(categories->size());
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
        if (const auto children = children_by_parent.find(category.id().value());
            children != children_by_parent.end()) {
            for (const auto* candidate : children->second) {
                auto child = build(*candidate, depth + 1);
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

    // A deleted-only view may omit an active parent. Such a node is a display
    // root while retaining parentId for historical explanation.
    std::vector<CategoryTreeDto> roots;
    for (const auto& category : *categories) {
        if (!category.parent_id().has_value() ||
            !by_id.contains(category.parent_id()->value())) {
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
    return execute_impl(command, std::nullopt);
}

Result<CategoryDto> CreateCategoryUseCase::execute(
    const CreateCategoryCommand& command,
    const IdempotencyRequest& idempotency) {
    return execute_impl(command, idempotency);
}

Result<CategoryDto> CreateCategoryUseCase::execute_impl(
    const CreateCategoryCommand& command,
    const std::optional<IdempotencyRequest>& idempotency) {
    if (idempotency.has_value()) {
        if (idempotency_ == nullptr) {
            return err(Error::infrastructure_failure(
                "Category idempotency is unavailable"));
        }
        if (auto valid = validate_idempotency_input(
                idempotency->key, idempotency->request_fingerprint);
            !valid) {
            return err(valid.error());
        }
    }
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
    const domain::Category requested_category(
        domain::CategoryId{}, command.user_id, name, *board,
        command.parent_id, source, command.template_id, sort_order,
        std::nullopt, now, now);
    domain::CategoryId persisted_id;
    domain::Category persisted_category = requested_category;
    bool restored = false;
    std::optional<Error> app_error;
    std::optional<CategoryDto> result_dto;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            if (idempotency.has_value()) {
                auto started = idempotency_->begin(
                    tx,
                    command.user_id,
                    "create_category",
                    idempotency->key,
                    idempotency->request_fingerprint,
                    idempotency->created_at,
                    idempotency->created_at + kIdempotencyLifetime);
                if (!started) return std::unexpected(started.error());
                if (started->replay) {
                    auto replayed = category_from_values(started->response_values);
                    if (!replayed) {
                        app_error = replayed.error();
                        return std::unexpected(domain::RepositoryError::database(
                            "Stored category idempotency response is invalid"));
                    }
                    result_dto = std::move(*replayed);
                    return {};
                }
            }
            auto existing = categories_.find_identity_for_update(
                tx, command.user_id, *board, command.parent_id, name,
                command.template_id);
            if (existing) {
                if (!existing->is_deleted()) {
                    return std::unexpected(domain::RepositoryError::conflict(
                        "Category already exists under the same parent"));
                }
                persisted_category = domain::Category(
                    existing->id(), existing->owner(), existing->name(),
                    existing->board(), existing->parent_id(), existing->source(),
                    existing->template_id(), existing->sort_order(), std::nullopt,
                    existing->created_at(), now);
                restored = true;
            } else if (existing.error().status != domain::RepositoryStatus::NotFound) {
                return std::unexpected(existing.error());
            }

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

            auto saved = categories_.save(tx, persisted_category);
            if (!saved) {
                return std::unexpected(saved.error());
            }
            persisted_id = *saved;
            result_dto = category_dto(persisted_category, persisted_id);
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id,
                        restored ? domain::AuditAction::Update
                                 : domain::AuditAction::Create,
                        "Category",
                        persisted_id.to_string(),
                        restored ? "{\"deleted\":true}" : std::string{},
                        "{\"name\":" + json_quoted(persisted_category.name()) + "}",
                        now));
                !audited) {
                return audited;
            }
            if (restored) {
                uow_.register_event(std::make_shared<domain::CategoryRestoredEvent>(
                    command.user_id, persisted_id, *board, now));
            } else {
                uow_.register_event(std::make_shared<domain::CategoryCreatedEvent>(
                    command.user_id, persisted_id, *board, now));
            }
            if (idempotency.has_value()) {
                return idempotency_->complete(
                    tx,
                    command.user_id,
                    "create_category",
                    idempotency->key,
                    category_values(*result_dto));
            }
            return {};
        });
    if (!write) {
        return app_error.has_value() ? err(*app_error)
                                     : err(from_repository(write.error()));
    }
    if (!result_dto.has_value()) {
        return err(Error::infrastructure_failure(
            "Category result was not produced"));
    }
    return *result_dto;
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

Result<CategoryDto> UpdateCategoryUseCase::execute(
    const UpdateCategoryCommand& command) {
    if (!command.user_id.is_valid() || !command.category_id.is_valid() ||
        !valid_text(command.name, 128)) {
        return err(Error::validation("Category update fields are invalid"));
    }
    const auto now = std::chrono::system_clock::now();
    std::optional<domain::Category> updated;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto current = categories_.find_by_id_for_user_for_update(
                tx, command.category_id, command.user_id);
            if (!current) return std::unexpected(current.error());
            updated.emplace(
                current->id(), current->owner(), command.name, current->board(),
                current->parent_id(), current->source(), current->template_id(),
                command.sort_order, std::nullopt, current->created_at(), now);
            auto saved = categories_.save(tx, *updated);
            if (!saved) return std::unexpected(saved.error());
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id, domain::AuditAction::Update, "Category",
                        command.category_id.to_string(),
                        "{\"name\":" + json_quoted(current->name()) +
                            ",\"sortOrder\":" +
                            std::to_string(current->sort_order()) + "}",
                        "{\"name\":" + json_quoted(command.name) +
                            ",\"sortOrder\":" +
                            std::to_string(command.sort_order) + "}",
                        now));
                !audited) {
                return audited;
            }
            uow_.register_event(std::make_shared<domain::CategoryUpdatedEvent>(
                command.user_id, command.category_id, current->board(), now));
            return {};
        });
    if (!write) return err(from_repository(write.error()));
    return category_dto(*updated);
}

Result<CategoryDto> RestoreCategoryUseCase::execute(
    const RestoreCategoryCommand& command) {
    if (!command.user_id.is_valid() || !command.category_id.is_valid()) {
        return err(Error::validation("Category id is invalid"));
    }
    const auto now = std::chrono::system_clock::now();
    std::optional<domain::Category> restored;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto current = categories_.find_by_id_for_user_including_deleted_for_update(
                tx, command.category_id, command.user_id);
            if (!current) return std::unexpected(current.error());
            if (!current->is_deleted()) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Category is already active"));
            }
            if (current->parent_id().has_value()) {
                auto parent = categories_.find_by_id_for_user_for_update(
                    tx, *current->parent_id(), command.user_id);
                if (!parent) return std::unexpected(parent.error());
                if (parent->board() != current->board()) {
                    return std::unexpected(domain::RepositoryError::validation(
                        "Category parent board is invalid"));
                }
            }
            restored.emplace(
                current->id(), current->owner(), current->name(), current->board(),
                current->parent_id(), current->source(), current->template_id(),
                current->sort_order(), std::nullopt, current->created_at(), now);
            auto saved = categories_.save(tx, *restored);
            if (!saved) return std::unexpected(saved.error());
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id, domain::AuditAction::Update, "Category",
                        command.category_id.to_string(), "{\"deleted\":true}",
                        "{\"deleted\":false}", now));
                !audited) {
                return audited;
            }
            uow_.register_event(std::make_shared<domain::CategoryRestoredEvent>(
                command.user_id, command.category_id, current->board(), now));
            return {};
        });
    if (!write) return err(from_repository(write.error()));
    return category_dto(*restored);
}

Result<std::vector<TagDto>> ListTagsUseCase::execute(
    domain::UserId user_id,
    MetadataListStatus status) {
    if (!user_id.is_valid()) {
        return err(Error::validation("User id is invalid"));
    }
    auto tags = tags_.find_by_user(user_id, status != MetadataListStatus::Active);
    if (!tags) {
        return err(from_repository(tags.error()));
    }
    std::vector<TagDto> result;
    result.reserve(tags->size());
    for (const auto& tag : *tags) {
        if (status == MetadataListStatus::Deleted && !tag.is_deleted()) continue;
        result.push_back(tag_dto(tag));
    }
    return result;
}

Result<TagDto> CreateTagUseCase::execute(const CreateTagCommand& command) {
    return execute_impl(command, std::nullopt);
}

Result<TagDto> CreateTagUseCase::execute(
    const CreateTagCommand& command,
    const IdempotencyRequest& idempotency) {
    return execute_impl(command, idempotency);
}

Result<TagDto> CreateTagUseCase::execute_impl(
    const CreateTagCommand& command,
    const std::optional<IdempotencyRequest>& idempotency) {
    if (idempotency.has_value()) {
        if (idempotency_ == nullptr) {
            return err(Error::infrastructure_failure(
                "Tag idempotency is unavailable"));
        }
        if (auto valid = validate_idempotency_input(
                idempotency->key, idempotency->request_fingerprint);
            !valid) {
            return err(valid.error());
        }
    }
    if (!command.user_id.is_valid() || !valid_text(command.name, 64)) {
        return err(Error::validation("Tag name is invalid"));
    }
    const auto now = std::chrono::system_clock::now();
    const domain::Tag requested_tag(
        domain::TagId{}, command.user_id, command.name,
        std::nullopt, now, now);
    domain::TagId persisted_id;
    domain::Tag persisted_tag = requested_tag;
    bool restored = false;
    std::optional<TagDto> result_dto;
    std::optional<Error> app_error;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            if (idempotency.has_value()) {
                auto started = idempotency_->begin(
                    tx,
                    command.user_id,
                    "create_tag",
                    idempotency->key,
                    idempotency->request_fingerprint,
                    idempotency->created_at,
                    idempotency->created_at + kIdempotencyLifetime);
                if (!started) return std::unexpected(started.error());
                if (started->replay) {
                    auto replayed = tag_from_values(started->response_values);
                    if (!replayed) {
                        app_error = replayed.error();
                        return std::unexpected(domain::RepositoryError::database(
                            "Stored tag idempotency response is invalid"));
                    }
                    result_dto = std::move(*replayed);
                    return {};
                }
            }
            auto existing = tags_.find_by_name_for_update(
                tx, command.user_id, command.name);
            if (existing) {
                if (!existing->is_deleted()) {
                    return std::unexpected(domain::RepositoryError::conflict(
                        "Tag name already exists for user"));
                }
                persisted_tag = domain::Tag(
                    existing->id(), existing->owner(), existing->name(),
                    std::nullopt, existing->created_at(), now);
                restored = true;
            } else if (existing.error().status != domain::RepositoryStatus::NotFound) {
                return std::unexpected(existing.error());
            }
            auto saved = tags_.save(tx, persisted_tag);
            if (!saved) {
                return std::unexpected(saved.error());
            }
            persisted_id = *saved;
            result_dto = tag_dto(persisted_tag.id().is_valid()
                ? persisted_tag
                : domain::Tag(
                      persisted_id, command.user_id, command.name,
                      std::nullopt, now, now));
            auto audited = audit_logs_.append(
                tx,
                audit_entry(
                    command.user_id,
                    restored ? domain::AuditAction::Update
                             : domain::AuditAction::Create,
                    "Tag",
                    persisted_id.to_string(),
                    restored ? "{\"deleted\":true}" : std::string{},
                    "{\"name\":" + json_quoted(command.name) + "}",
                    now));
            if (!audited) return audited;
            if (restored) {
                uow_.register_event(std::make_shared<domain::TagRestoredEvent>(
                    command.user_id, persisted_id, now));
            } else {
                uow_.register_event(std::make_shared<domain::TagCreatedEvent>(
                    command.user_id, persisted_id, now));
            }
            if (idempotency.has_value()) {
                return idempotency_->complete(
                    tx,
                    command.user_id,
                    "create_tag",
                    idempotency->key,
                    tag_values(*result_dto));
            }
            return {};
        });
    if (!write) {
        if (app_error.has_value()) return err(*app_error);
        return err(from_repository(write.error()));
    }
    if (!result_dto.has_value()) {
        return err(Error::infrastructure_failure("Tag result was not produced"));
    }
    return *result_dto;
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
            auto audited = audit_logs_.append(
                tx,
                audit_entry(
                    command.user_id,
                    domain::AuditAction::Delete,
                    "Tag",
                    command.tag_id.to_string(),
                    "{\"name\":" + json_quoted(tag->name()) + "}",
                    "{\"deleted\":true}",
                    deleted_at));
            if (!audited) return audited;
            uow_.register_event(std::make_shared<domain::TagDeletedEvent>(
                command.user_id, command.tag_id, deleted_at));
            return {};
        });
    return write ? ok() : err(from_repository(write.error()));
}

Result<TagDto> UpdateTagUseCase::execute(const UpdateTagCommand& command) {
    if (!command.user_id.is_valid() || !command.tag_id.is_valid() ||
        !valid_text(command.name, 64)) {
        return err(Error::validation("Tag update fields are invalid"));
    }
    const auto now = std::chrono::system_clock::now();
    std::optional<domain::Tag> updated;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto current = tags_.find_by_id_for_user_for_update(
                tx, command.tag_id, command.user_id);
            if (!current) return std::unexpected(current.error());
            updated.emplace(
                current->id(), current->owner(), command.name, std::nullopt,
                current->created_at(), now);
            auto saved = tags_.save(tx, *updated);
            if (!saved) return std::unexpected(saved.error());
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id, domain::AuditAction::Update, "Tag",
                        command.tag_id.to_string(),
                        "{\"name\":" + json_quoted(current->name()) + "}",
                        "{\"name\":" + json_quoted(command.name) + "}", now));
                !audited) {
                return audited;
            }
            uow_.register_event(std::make_shared<domain::TagUpdatedEvent>(
                command.user_id, command.tag_id, now));
            return {};
        });
    if (!write) return err(from_repository(write.error()));
    return tag_dto(*updated);
}

Result<TagDto> RestoreTagUseCase::execute(const RestoreTagCommand& command) {
    if (!command.user_id.is_valid() || !command.tag_id.is_valid()) {
        return err(Error::validation("Tag id is invalid"));
    }
    const auto now = std::chrono::system_clock::now();
    std::optional<domain::Tag> restored;
    auto write = uow_.execute_in_transaction(
        [&](domain::ITransactionContext& tx) -> domain::RepositoryVoidResult {
            auto current = tags_.find_by_id_for_user_including_deleted_for_update(
                tx, command.tag_id, command.user_id);
            if (!current) return std::unexpected(current.error());
            if (!current->is_deleted()) {
                return std::unexpected(domain::RepositoryError::conflict(
                    "Tag is already active"));
            }
            restored.emplace(
                current->id(), current->owner(), current->name(), std::nullopt,
                current->created_at(), now);
            auto saved = tags_.save(tx, *restored);
            if (!saved) return std::unexpected(saved.error());
            if (auto audited = audit_logs_.append(
                    tx,
                    audit_entry(
                        command.user_id, domain::AuditAction::Update, "Tag",
                        command.tag_id.to_string(), "{\"deleted\":true}",
                        "{\"deleted\":false}", now));
                !audited) {
                return audited;
            }
            uow_.register_event(std::make_shared<domain::TagRestoredEvent>(
                command.user_id, command.tag_id, now));
            return {};
        });
    if (!write) return err(from_repository(write.error()));
    return tag_dto(*restored);
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
        result.push_back(tag_dto(tag));
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
    const bool supported_locale =
        command.locale == "zh-CN" || command.locale == "en-US";
    if (!command.user_id.is_valid() || !is_locale_tag(command.locale) ||
        !supported_locale ||
        !valid_text(command.timezone, 64) ||
        !valid_text(command.date_format, 32) ||
        !valid_preference_enums(
            command.theme,
            command.default_home_page,
            command.default_report_period,
            command.number_format) ||
        !valid_custom_report_months(
            command.default_report_period,
            command.custom_report_start_month,
            command.custom_report_end_month)) {
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
        command.default_home_page, command.default_report_period,
        command.custom_report_start_month, command.custom_report_end_month);
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

Result<std::vector<TimeZoneMetadataDto>> ListTimeZonesUseCase::execute() const {
    try {
        const auto& database = std::chrono::get_tzdb();
        std::vector<TimeZoneMetadataDto> result;
        result.reserve(database.zones.size() + database.links.size());
        for (const auto& zone : database.zones) {
            const auto name = std::string(zone.name());
            result.push_back(TimeZoneMetadataDto{name, name, false});
        }
        for (const auto& link : database.links) {
            const auto id = std::string(link.name());
            const auto canonical = std::string(database.locate_zone(id)->name());
            result.push_back(TimeZoneMetadataDto{id, canonical, true});
        }
        std::ranges::sort(result, {}, &TimeZoneMetadataDto::id);
        return result;
    } catch (const std::runtime_error&) {
        return err(Error::infrastructure_failure(
            "IANA timezone database is unavailable"));
    }
}

} // namespace pfh::application
