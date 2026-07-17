// Personal Finance Hub - Transfer aggregate DTO mapping and idempotency codec

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/idempotency.h"
#include "pfh/domain/repositories/i_transaction_repository.h"

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string>

namespace pfh::application {

[[nodiscard]] inline TransferInputMode transfer_input_mode(int mode) {
    switch (mode) {
    case 1: return TransferInputMode::OutgoingAndRate;
    case 2: return TransferInputMode::BothAmounts;
    case 3: return TransferInputMode::IncomingAndRate;
    default: return TransferInputMode::BothAmounts;
    }
}

[[nodiscard]] inline domain::FeeSource infer_fee_source(
    domain::AccountId fee_account,
    domain::AccountId source_account,
    domain::AccountId target_account) {
    if (fee_account == source_account) return domain::FeeSource::SourceAccount;
    if (fee_account == target_account) return domain::FeeSource::TargetAccount;
    return domain::FeeSource::ThirdParty;
}

[[nodiscard]] inline TransferResultDto to_transfer_dto(
    const domain::TransferPersistResult& persisted,
    const domain::TransferAggregate& aggregate) {
    TransferResultDto result;
    result.transfer_group_id = persisted.group_id;
    result.mode = transfer_input_mode(static_cast<int>(aggregate.mode()));
    result.source_account_id = aggregate.outgoing().account_id();
    result.target_account_id = aggregate.incoming().account_id();
    result.outgoing_transaction_id = persisted.outgoing_id;
    result.incoming_transaction_id = persisted.incoming_id;
    result.adjustment_transaction_ids = persisted.adjustment_ids;
    result.outgoing_amount = aggregate.outgoing().amount().amount().to_string();
    result.incoming_amount = aggregate.incoming().amount().amount().to_string();
    result.source_currency_code =
        aggregate.outgoing().amount().currency().code();
    result.target_currency_code =
        aggregate.incoming().amount().currency().code();
    if (aggregate.rate().has_value()) {
        result.rate = aggregate.rate()->rate().to_string();
    }
    if (!aggregate.adjustments().empty()) {
        const auto& fee = aggregate.adjustments().front();
        result.fee_amount = fee.amount().amount().abs().to_string();
        result.fee_account_id = fee.account_id();
        result.fee_currency_code = fee.amount().currency().code();
        result.fee_source = infer_fee_source(
            fee.account_id(), result.source_account_id,
            result.target_account_id);
    }
    result.description = aggregate.outgoing().description();
    result.occurred_at = aggregate.outgoing().occurred_at();
    result.created_at = aggregate.outgoing().created_at();
    return result;
}

[[nodiscard]] inline Result<TransferResultDto> to_transfer_dto(
    const domain::TransferSnapshot& snapshot) {
    if (snapshot.transfer_mode < 1 || snapshot.transfer_mode > 3) {
        return err(Error::infrastructure_failure(
            "Persisted transfer mode is invalid"));
    }
    const domain::Transaction* outgoing = nullptr;
    const domain::Transaction* incoming = nullptr;
    const domain::Transaction* fee = nullptr;
    TransferResultDto result;
    result.transfer_group_id = snapshot.group_id;
    result.mode = transfer_input_mode(snapshot.transfer_mode);
    result.description = snapshot.note;
    result.occurred_at = snapshot.occurred_at;
    result.created_at = snapshot.created_at;
    result.deleted_at = snapshot.deleted_at;
    result.corrects_transfer_group_id = snapshot.corrects_group_id;
    result.corrected_by_transfer_group_id = snapshot.corrected_by_group_id;

    for (const auto& transaction : snapshot.transactions) {
        if (transaction.type() == domain::TransactionType::Transfer) {
            if (transaction.amount().is_negative()) {
                if (outgoing != nullptr) {
                    return err(Error::infrastructure_failure(
                        "Persisted transfer has multiple outgoing legs"));
                }
                outgoing = &transaction;
            } else if (transaction.amount().is_positive()) {
                if (incoming != nullptr) {
                    return err(Error::infrastructure_failure(
                        "Persisted transfer has multiple incoming legs"));
                }
                incoming = &transaction;
            }
        } else if (transaction.type() == domain::TransactionType::Adjustment) {
            result.adjustment_transaction_ids.push_back(transaction.id());
            if (transaction.amount().is_negative()) {
                if (fee != nullptr) {
                    return err(Error::infrastructure_failure(
                        "Persisted transfer has multiple fee adjustments"));
                }
                fee = &transaction;
            }
        }
    }
    if (outgoing == nullptr || incoming == nullptr) {
        return err(Error::infrastructure_failure(
            "Persisted transfer aggregate is incomplete"));
    }
    result.source_account_id = outgoing->account_id();
    result.target_account_id = incoming->account_id();
    result.outgoing_transaction_id = outgoing->id();
    result.incoming_transaction_id = incoming->id();
    result.outgoing_amount = outgoing->amount().amount().abs().to_string();
    result.incoming_amount = incoming->amount().amount().abs().to_string();
    result.source_currency_code = outgoing->amount().currency().code();
    result.target_currency_code = incoming->amount().currency().code();
    if (result.description.empty()) result.description = outgoing->description();
    if (result.occurred_at.time_since_epoch().count() == 0) {
        result.occurred_at = outgoing->occurred_at();
    }
    if (result.created_at.time_since_epoch().count() == 0) {
        result.created_at = outgoing->created_at();
    }
    if (snapshot.exchange_rate.has_value()) {
        result.rate = snapshot.exchange_rate->to_string();
    }
    if (fee != nullptr) {
        result.fee_amount = fee->amount().amount().abs().to_string();
        result.fee_account_id = fee->account_id();
        result.fee_currency_code = fee->amount().currency().code();
        result.fee_source = infer_fee_source(
            fee->account_id(), result.source_account_id,
            result.target_account_id);
    }
    return result;
}

[[nodiscard]] inline IdempotencyValues transfer_to_idempotency_values(
    const TransferResultDto& value) {
    IdempotencyValues result{
        {"transfer_group_id", value.transfer_group_id.to_string()},
        {"mode", std::to_string(static_cast<int>(value.mode))},
        {"source_account_id", value.source_account_id.to_string()},
        {"target_account_id", value.target_account_id.to_string()},
        {"outgoing_transaction_id", value.outgoing_transaction_id.to_string()},
        {"incoming_transaction_id", value.incoming_transaction_id.to_string()},
        {"outgoing_amount", value.outgoing_amount},
        {"incoming_amount", value.incoming_amount},
        {"source_currency", value.source_currency_code},
        {"target_currency", value.target_currency_code},
        {"rate", value.rate.value_or("")},
        {"fee_amount", value.fee_amount.value_or("")},
        {"fee_source", value.fee_source.has_value()
            ? std::to_string(static_cast<int>(*value.fee_source)) : ""},
        {"fee_account_id", value.fee_account_id.has_value()
            ? value.fee_account_id->to_string() : ""},
        {"fee_currency", value.fee_currency_code.value_or("")},
        {"description", value.description},
        {"occurred_at", encode_idempotency_time(value.occurred_at)},
        {"created_at", encode_idempotency_time(value.created_at)},
        {"deleted_at", value.deleted_at.has_value()
            ? encode_idempotency_time(*value.deleted_at) : ""},
        {"corrects_group_id", value.corrects_transfer_group_id.has_value()
            ? value.corrects_transfer_group_id->to_string() : ""},
        {"corrected_by_group_id", value.corrected_by_transfer_group_id.has_value()
            ? value.corrected_by_transfer_group_id->to_string() : ""},
        {"adjustment_count", std::to_string(value.adjustment_transaction_ids.size())}};
    for (std::size_t index = 0;
         index < value.adjustment_transaction_ids.size(); ++index) {
        result.emplace(
            "adjustment_" + std::to_string(index),
            value.adjustment_transaction_ids[index].to_string());
    }
    return result;
}

[[nodiscard]] inline Result<TransferResultDto> transfer_from_idempotency_values(
    const IdempotencyValues& values) {
    const auto required_id = [&](std::string_view key)
        -> Result<std::int64_t> {
        const auto text = idempotency_value(values, key);
        if (!text.has_value()) {
            return err(Error::infrastructure_failure(
                "Stored transfer response is incomplete"));
        }
        const auto value = parse_idempotency_integer(*text);
        if (!value || *value <= 0) {
            return err(Error::infrastructure_failure(
                "Stored transfer response is invalid"));
        }
        return *value;
    };
    auto group = required_id("transfer_group_id");
    auto source = required_id("source_account_id");
    auto target = required_id("target_account_id");
    auto outgoing_id = required_id("outgoing_transaction_id");
    auto incoming_id = required_id("incoming_transaction_id");
    if (!group || !source || !target || !outgoing_id || !incoming_id) {
        return err(!group ? group.error() : !source ? source.error() :
            !target ? target.error() : !outgoing_id ? outgoing_id.error() :
            incoming_id.error());
    }
    const auto mode_text = idempotency_value(values, "mode");
    const auto outgoing = idempotency_value(values, "outgoing_amount");
    const auto incoming = idempotency_value(values, "incoming_amount");
    const auto source_currency = idempotency_value(values, "source_currency");
    const auto target_currency = idempotency_value(values, "target_currency");
    const auto description = idempotency_value(values, "description");
    const auto occurred = idempotency_value(values, "occurred_at");
    const auto created = idempotency_value(values, "created_at");
    if (!mode_text || !outgoing || !incoming || !source_currency ||
        !target_currency || !description || !occurred || !created) {
        return err(Error::infrastructure_failure(
            "Stored transfer response is incomplete"));
    }
    const auto mode = parse_idempotency_integer(*mode_text);
    auto occurred_at = decode_idempotency_time(*occurred);
    auto created_at = decode_idempotency_time(*created);
    if (!mode || *mode < 0 || *mode > 2 || !occurred_at || !created_at) {
        return err(Error::infrastructure_failure(
            "Stored transfer response is invalid"));
    }

    TransferResultDto result;
    result.transfer_group_id = domain::TransferGroupId(*group);
    result.mode = static_cast<TransferInputMode>(*mode);
    result.source_account_id = domain::AccountId(*source);
    result.target_account_id = domain::AccountId(*target);
    result.outgoing_transaction_id = domain::TransactionId(*outgoing_id);
    result.incoming_transaction_id = domain::TransactionId(*incoming_id);
    result.outgoing_amount = std::string(*outgoing);
    result.incoming_amount = std::string(*incoming);
    result.source_currency_code = std::string(*source_currency);
    result.target_currency_code = std::string(*target_currency);
    result.description = std::string(*description);
    result.occurred_at = *occurred_at;
    result.created_at = *created_at;
    if (const auto value = idempotency_value(values, "rate");
        value.has_value() && !value->empty()) result.rate = std::string(*value);
    if (const auto value = idempotency_value(values, "fee_amount");
        value.has_value() && !value->empty()) result.fee_amount = std::string(*value);
    if (const auto value = idempotency_value(values, "fee_currency");
        value.has_value() && !value->empty()) {
        result.fee_currency_code = std::string(*value);
    }
    if (const auto value = idempotency_value(values, "fee_source");
        value.has_value() && !value->empty()) {
        const auto parsed = parse_idempotency_integer(*value);
        if (!parsed || *parsed < 0 || *parsed > 2) {
            return err(Error::infrastructure_failure(
                "Stored transfer fee source is invalid"));
        }
        result.fee_source = static_cast<domain::FeeSource>(*parsed);
    }
    if (const auto value = idempotency_value(values, "fee_account_id");
        value.has_value() && !value->empty()) {
        const auto parsed = parse_idempotency_integer(*value);
        if (!parsed || *parsed <= 0) {
            return err(Error::infrastructure_failure(
                "Stored transfer fee account is invalid"));
        }
        result.fee_account_id = domain::AccountId(*parsed);
    }
    const auto optional_time = [&](std::string_view key)
        -> Result<std::optional<std::chrono::system_clock::time_point>> {
        const auto value = idempotency_value(values, key);
        if (!value.has_value() || value->empty()) {
            return std::optional<std::chrono::system_clock::time_point>{};
        }
        auto parsed = decode_idempotency_time(*value);
        if (!parsed) {
            return err(Error::infrastructure_failure(
                "Stored transfer time is invalid"));
        }
        return std::optional<std::chrono::system_clock::time_point>(*parsed);
    };
    auto deleted = optional_time("deleted_at");
    if (!deleted) return err(deleted.error());
    result.deleted_at = *deleted;

    const auto optional_group = [&](std::string_view key)
        -> Result<std::optional<domain::TransferGroupId>> {
        const auto value = idempotency_value(values, key);
        if (!value.has_value() || value->empty()) {
            return std::optional<domain::TransferGroupId>{};
        }
        const auto parsed = parse_idempotency_integer(*value);
        if (!parsed || *parsed <= 0) {
            return err(Error::infrastructure_failure(
                "Stored transfer correction link is invalid"));
        }
        return std::optional<domain::TransferGroupId>(
            domain::TransferGroupId(*parsed));
    };
    auto corrects = optional_group("corrects_group_id");
    auto corrected_by = optional_group("corrected_by_group_id");
    if (!corrects || !corrected_by) {
        return err(!corrects ? corrects.error() : corrected_by.error());
    }
    result.corrects_transfer_group_id = *corrects;
    result.corrected_by_transfer_group_id = *corrected_by;

    const auto count_text = idempotency_value(
        values, "adjustment_count").value_or("0");
    const auto count = parse_idempotency_integer(count_text);
    if (!count || *count < 0 || *count > 64) {
        return err(Error::infrastructure_failure(
            "Stored transfer adjustment list is invalid"));
    }
    for (std::int64_t index = 0; index < *count; ++index) {
        auto adjustment = required_id(
            "adjustment_" + std::to_string(index));
        if (!adjustment) return err(adjustment.error());
        result.adjustment_transaction_ids.emplace_back(*adjustment);
    }
    return result;
}

} // namespace pfh::application
