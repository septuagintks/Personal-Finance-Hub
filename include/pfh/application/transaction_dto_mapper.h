// Personal Finance Hub - Transaction DTO mapping and idempotency codec

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/idempotency.h"
#include "pfh/domain/repositories/i_transaction_repository.h"

#include <cstddef>
#include <string>

namespace pfh::application {

[[nodiscard]] inline TransactionDto to_transaction_dto(
    const domain::Transaction& transaction) {
    TransactionDto result;
    result.id = transaction.id();
    result.user_id = transaction.user_id();
    result.account_id = transaction.account_id();
    result.currency_code = transaction.amount().currency().code();
    result.amount = transaction.amount().amount().to_string();
    result.type = transaction.type();
    result.description = transaction.description();
    result.category_id = transaction.category_id();
    result.transfer_group_id = transaction.transfer_group_id();
    result.occurred_at = transaction.occurred_at();
    result.created_at = transaction.created_at();
    result.deleted_at = transaction.deleted_at();
    return result;
}

[[nodiscard]] inline TransactionDto to_transaction_dto(
    const domain::TransactionReadModel& model) {
    auto result = to_transaction_dto(model.transaction);
    result.category_name = model.category_name;
    result.category_deleted = model.category_deleted;
    result.corrects_transaction_id = model.corrects_transaction_id;
    result.corrected_by_transaction_id = model.corrected_by_transaction_id;
    result.tags.reserve(model.tags.size());
    for (const auto& tag : model.tags) {
        result.tags.push_back(TransactionTagDto{
            tag.id(), tag.name(), tag.is_deleted()});
    }
    return result;
}

[[nodiscard]] inline IdempotencyValues transaction_to_idempotency_values(
    const TransactionDto& value) {
    IdempotencyValues result{
        {"id", value.id.to_string()},
        {"user_id", value.user_id.to_string()},
        {"account_id", value.account_id.to_string()},
        {"currency_code", value.currency_code},
        {"amount", value.amount},
        {"type", std::to_string(static_cast<int>(value.type))},
        {"description", value.description},
        {"category_id", value.category_id.has_value()
            ? value.category_id->to_string() : ""},
        {"transfer_group_id", value.transfer_group_id.has_value()
            ? value.transfer_group_id->to_string() : ""},
        {"occurred_at", encode_idempotency_time(value.occurred_at)},
        {"created_at", encode_idempotency_time(value.created_at)},
        {"deleted_at", value.deleted_at.has_value()
            ? encode_idempotency_time(*value.deleted_at) : ""},
        {"category_name", value.category_name.value_or("")},
        {"category_deleted", value.category_deleted ? "1" : "0"},
        {"corrects_transaction_id", value.corrects_transaction_id.has_value()
            ? value.corrects_transaction_id->to_string() : ""},
        {"corrected_by_transaction_id", value.corrected_by_transaction_id.has_value()
            ? value.corrected_by_transaction_id->to_string() : ""},
        {"tag_count", std::to_string(value.tags.size())}};
    for (std::size_t index = 0; index < value.tags.size(); ++index) {
        const auto prefix = "tag_" + std::to_string(index) + "_";
        result.emplace(prefix + "id", value.tags[index].id.to_string());
        result.emplace(prefix + "name", value.tags[index].name);
        result.emplace(
            prefix + "deleted", value.tags[index].is_deleted ? "1" : "0");
    }
    return result;
}

[[nodiscard]] inline Result<TransactionDto> transaction_from_idempotency_values(
    const IdempotencyValues& values) {
    const auto id = idempotency_value(values, "id");
    const auto user_id = idempotency_value(values, "user_id");
    const auto account_id = idempotency_value(values, "account_id");
    const auto currency = idempotency_value(values, "currency_code");
    const auto amount = idempotency_value(values, "amount");
    const auto type = idempotency_value(values, "type");
    const auto description = idempotency_value(values, "description");
    const auto category = idempotency_value(values, "category_id");
    const auto transfer = idempotency_value(values, "transfer_group_id");
    const auto occurred_at = idempotency_value(values, "occurred_at");
    if (!id || !user_id || !account_id || !currency || !amount || !type ||
        !description || !category || !transfer || !occurred_at) {
        return err(Error::infrastructure_failure(
            "Stored transaction response is incomplete"));
    }
    const auto id_value = parse_idempotency_integer(*id);
    const auto user_value = parse_idempotency_integer(*user_id);
    const auto account_value = parse_idempotency_integer(*account_id);
    const auto type_value = parse_idempotency_integer(*type);
    const auto time_value = decode_idempotency_time(*occurred_at);
    if (!id_value || !user_value || !account_value || !type_value ||
        !time_value || *id_value <= 0 || *user_value <= 0 ||
        *account_value <= 0 || *type_value < 0 || *type_value > 3) {
        return err(Error::infrastructure_failure(
            "Stored transaction response is invalid"));
    }

    TransactionDto result;
    result.id = domain::TransactionId(*id_value);
    result.user_id = domain::UserId(*user_value);
    result.account_id = domain::AccountId(*account_value);
    result.currency_code = std::string(*currency);
    result.amount = std::string(*amount);
    result.type = static_cast<domain::TransactionType>(*type_value);
    result.description = std::string(*description);
    result.occurred_at = *time_value;
    result.created_at = *time_value;
    if (const auto created = idempotency_value(values, "created_at");
        created.has_value()) {
        auto parsed = decode_idempotency_time(*created);
        if (!parsed) {
            return err(Error::infrastructure_failure(
                "Stored transaction creation time is invalid"));
        }
        result.created_at = *parsed;
    }
    if (!category->empty()) {
        const auto value = parse_idempotency_integer(*category);
        if (!value || *value <= 0) {
            return err(Error::infrastructure_failure(
                "Stored transaction category is invalid"));
        }
        result.category_id = domain::CategoryId(*value);
    }
    if (!transfer->empty()) {
        const auto value = parse_idempotency_integer(*transfer);
        if (!value || *value <= 0) {
            return err(Error::infrastructure_failure(
                "Stored transaction transfer group is invalid"));
        }
        result.transfer_group_id = domain::TransferGroupId(*value);
    }
    if (const auto deleted = idempotency_value(values, "deleted_at");
        deleted.has_value() && !deleted->empty()) {
        auto parsed = decode_idempotency_time(*deleted);
        if (!parsed) {
            return err(Error::infrastructure_failure(
                "Stored transaction deletion time is invalid"));
        }
        result.deleted_at = *parsed;
    }
    if (const auto name = idempotency_value(values, "category_name");
        name.has_value() && !name->empty()) {
        result.category_name = std::string(*name);
    }
    result.category_deleted =
        idempotency_value(values, "category_deleted").value_or("0") == "1";

    const auto parse_optional_id = [&](std::string_view key)
        -> Result<std::optional<domain::TransactionId>> {
        const auto encoded = idempotency_value(values, key);
        if (!encoded.has_value() || encoded->empty()) {
            return std::optional<domain::TransactionId>{};
        }
        const auto parsed = parse_idempotency_integer(*encoded);
        if (!parsed || *parsed <= 0) {
            return err(Error::infrastructure_failure(
                "Stored transaction correction link is invalid"));
        }
        return std::optional<domain::TransactionId>(
            domain::TransactionId(*parsed));
    };
    auto corrects = parse_optional_id("corrects_transaction_id");
    auto corrected_by = parse_optional_id("corrected_by_transaction_id");
    if (!corrects) return err(corrects.error());
    if (!corrected_by) return err(corrected_by.error());
    result.corrects_transaction_id = *corrects;
    result.corrected_by_transaction_id = *corrected_by;

    const auto count_text = idempotency_value(values, "tag_count").value_or("0");
    const auto count = parse_idempotency_integer(count_text);
    if (!count || *count < 0 ||
        static_cast<std::size_t>(*count) > domain::kMaxTagsPerTransaction) {
        return err(Error::infrastructure_failure(
            "Stored transaction tags are invalid"));
    }
    result.tags.reserve(static_cast<std::size_t>(*count));
    for (std::int64_t index = 0; index < *count; ++index) {
        const auto prefix = "tag_" + std::to_string(index) + "_";
        const auto tag_id = idempotency_value(values, prefix + "id");
        const auto tag_name = idempotency_value(values, prefix + "name");
        if (!tag_id || !tag_name) {
            return err(Error::infrastructure_failure(
                "Stored transaction tags are incomplete"));
        }
        const auto parsed_id = parse_idempotency_integer(*tag_id);
        if (!parsed_id || *parsed_id <= 0) {
            return err(Error::infrastructure_failure(
                "Stored transaction tag identifier is invalid"));
        }
        result.tags.push_back(TransactionTagDto{
            domain::TagId(*parsed_id), std::string(*tag_name),
            idempotency_value(values, prefix + "deleted").value_or("0") == "1"});
    }
    return result;
}

} // namespace pfh::application
