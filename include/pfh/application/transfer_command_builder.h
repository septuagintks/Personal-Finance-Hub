// Personal Finance Hub - Shared transfer command validation and construction

#pragma once

#include "pfh/application/dto.h"
#include "pfh/application/error_mapping.h"
#include "pfh/application/input_constraints.h"
#include "pfh/application/persistence_time.h"
#include "pfh/domain/transfer_domain_service.h"

#include <chrono>
#include <optional>
#include <string>

namespace pfh::application {

[[nodiscard]] inline VoidResult validate_transfer_command_shape(
    const CreateTransferCommand& command) {
    if (!command.user_id.is_valid() ||
        !command.source_account_id.is_valid() ||
        !command.target_account_id.is_valid()) {
        return err(Error::validation(
            "user and transfer account ids must be valid"));
    }
    if (command.description.size() > kMaxDescriptionLength) {
        return err(Error::validation(
            "description exceeds the maximum length"));
    }
    if (command.source_account_id == command.target_account_id) {
        return err(Error::validation(
            "source and target accounts must differ"));
    }

    switch (command.mode) {
    case TransferInputMode::OutgoingAndRate:
        if (command.outgoing_amount.empty() || command.rate.empty()) {
            return err(Error::validation(
                "OutgoingAndRate requires outgoing_amount and rate"));
        }
        if (!command.incoming_amount.empty()) {
            return err(Error::validation(
                "OutgoingAndRate must not provide incoming_amount"));
        }
        break;
    case TransferInputMode::BothAmounts:
        if (command.outgoing_amount.empty() || command.incoming_amount.empty()) {
            return err(Error::validation(
                "BothAmounts requires outgoing_amount and incoming_amount"));
        }
        if (!command.rate.empty()) {
            return err(Error::validation(
                "BothAmounts must not provide rate"));
        }
        break;
    case TransferInputMode::IncomingAndRate:
        if (command.incoming_amount.empty() || command.rate.empty()) {
            return err(Error::validation(
                "IncomingAndRate requires incoming_amount and rate"));
        }
        if (!command.outgoing_amount.empty()) {
            return err(Error::validation(
                "IncomingAndRate must not provide outgoing_amount"));
        }
        break;
    default:
        return err(Error::validation("unsupported transfer mode"));
    }

    if (!command.fee_amount.has_value()) {
        if (command.fee_source.has_value() || command.fee_account_id.has_value()) {
            return err(Error::validation(
                "fee_source and fee_account_id require fee_amount"));
        }
        return ok();
    }
    if (!command.fee_source.has_value()) {
        return err(Error::validation("fee_amount requires fee_source"));
    }
    switch (*command.fee_source) {
    case domain::FeeSource::SourceAccount:
    case domain::FeeSource::TargetAccount:
        if (command.fee_account_id.has_value()) {
            return err(Error::validation(
                "fee_account_id is only valid for ThirdParty fees"));
        }
        break;
    case domain::FeeSource::ThirdParty:
        if (!command.fee_account_id.has_value() ||
            !command.fee_account_id->is_valid()) {
            return err(Error::validation(
                "ThirdParty fee requires a valid fee_account_id"));
        }
        if (*command.fee_account_id == command.source_account_id ||
            *command.fee_account_id == command.target_account_id) {
            return err(Error::validation(
                "ThirdParty fee account must differ from both transfer accounts"));
        }
        break;
    default:
        return err(Error::validation("unsupported fee source"));
    }
    return ok();
}

[[nodiscard]] inline Result<domain::Money> parse_transfer_money(
    const std::string& amount,
    const domain::Currency& currency) {
    if (!is_plain_decimal_string(amount, false)) {
        return err(Error::validation(
            "amount must be a positive plain decimal string"));
    }
    auto decimal = domain::Decimal::parse_numeric_20_8(amount);
    if (!decimal) return err(from_domain(decimal.error()));
    if (!decimal->is_positive()) {
        return err(Error::validation("amount must be positive"));
    }
    return domain::Money(*decimal, currency);
}

[[nodiscard]] inline Result<domain::TransferAggregate> build_transfer_aggregate(
    const CreateTransferCommand& command,
    const domain::Account& source,
    const domain::Account& target,
    const domain::Account* third_party_fee_account,
    std::chrono::system_clock::time_point now) {
    const auto occurred_at = normalize_persisted_time(
        command.occurred_at.value_or(now));

    std::optional<domain::TransferFee> fee;
    if (command.fee_amount.has_value()) {
        const domain::Account* selected_fee_account = nullptr;
        switch (*command.fee_source) {
        case domain::FeeSource::SourceAccount:
            selected_fee_account = &source;
            break;
        case domain::FeeSource::TargetAccount:
            selected_fee_account = &target;
            break;
        case domain::FeeSource::ThirdParty:
            selected_fee_account = third_party_fee_account;
            break;
        }
        if (selected_fee_account == nullptr) {
            return err(Error::validation("fee account could not be resolved"));
        }
        auto amount = parse_transfer_money(
            *command.fee_amount, selected_fee_account->currency());
        if (!amount) return err(amount.error());
        fee = domain::TransferFee{
            *command.fee_source,
            selected_fee_account->id(),
            std::move(*amount)};
    }

    const auto parse_rate = [&](const std::string& text)
        -> Result<domain::ExchangeRate> {
        if (!is_plain_decimal_string(text, false)) {
            return err(Error::validation(
                "rate must be a positive plain decimal string"));
        }
        auto decimal = domain::Decimal::parse_numeric_20_10(text);
        if (!decimal) return err(from_domain(decimal.error()));
        auto rate = domain::ExchangeRate::create(
            source.currency(), target.currency(), *decimal,
            occurred_at, "Manual");
        return rate
            ? Result<domain::ExchangeRate>(*rate)
            : Result<domain::ExchangeRate>(err(from_domain(rate.error())));
    };

    switch (command.mode) {
    case TransferInputMode::OutgoingAndRate: {
        auto outgoing = parse_transfer_money(
            command.outgoing_amount, source.currency());
        auto rate = parse_rate(command.rate);
        if (!outgoing) return err(outgoing.error());
        if (!rate) return err(rate.error());
        return map_domain(domain::TransferDomainService::build_from_outgoing_and_rate(
            *outgoing, source.id(), target.id(), *rate, command.user_id,
            occurred_at, command.description, domain::TransferGroupId{}, fee));
    }
    case TransferInputMode::BothAmounts: {
        auto outgoing = parse_transfer_money(
            command.outgoing_amount, source.currency());
        auto incoming = parse_transfer_money(
            command.incoming_amount, target.currency());
        if (!outgoing) return err(outgoing.error());
        if (!incoming) return err(incoming.error());
        return map_domain(domain::TransferDomainService::build_from_both_amounts(
            *outgoing, *incoming, source.id(), target.id(), command.user_id,
            occurred_at, command.description, domain::TransferGroupId{}, fee));
    }
    case TransferInputMode::IncomingAndRate: {
        auto incoming = parse_transfer_money(
            command.incoming_amount, target.currency());
        auto rate = parse_rate(command.rate);
        if (!incoming) return err(incoming.error());
        if (!rate) return err(rate.error());
        return map_domain(domain::TransferDomainService::build_from_incoming_and_rate(
            *incoming, source.id(), target.id(), *rate, command.user_id,
            occurred_at, command.description, domain::TransferGroupId{}, fee));
    }
    }
    return err(Error::validation("unsupported transfer mode"));
}

} // namespace pfh::application
