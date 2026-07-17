// Personal Finance Hub - Ledger transaction queries

#include "pfh/application/use_cases/transaction_query_use_cases.h"

#include "pfh/application/error_mapping.h"
#include "pfh/application/transaction_dto_mapper.h"

#include <array>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <limits>
#include <string>
#include <string_view>

namespace pfh::application {

namespace {

constexpr std::size_t kMaximumKeywordLength = 128;
constexpr auto kMaximumLedgerRange = std::chrono::days(366);
constexpr std::string_view kBase64UrlAlphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

[[nodiscard]] std::string base64url_encode(std::string_view input) {
    std::string result;
    result.reserve((input.size() * 4U + 2U) / 3U);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char raw : input) {
        const auto byte = static_cast<unsigned char>(raw);
        buffer = (buffer << 8U) | byte;
        bits += 8;
        while (bits >= 6) {
            bits -= 6;
            result.push_back(kBase64UrlAlphabet[
                static_cast<std::size_t>((buffer >> bits) & 0x3fU)]);
        }
    }
    if (bits > 0) {
        result.push_back(kBase64UrlAlphabet[
            static_cast<std::size_t>((buffer << (6 - bits)) & 0x3fU)]);
    }
    return result;
}

[[nodiscard]] Result<std::string> base64url_decode(std::string_view input) {
    if (input.empty() || input.size() > 512U || input.size() % 4U == 1U) {
        return err(Error::validation("cursor is invalid"));
    }
    std::array<int, 256> values{};
    values.fill(-1);
    for (std::size_t index = 0; index < kBase64UrlAlphabet.size(); ++index) {
        values[static_cast<unsigned char>(kBase64UrlAlphabet[index])] =
            static_cast<int>(index);
    }
    std::string result;
    result.reserve(input.size() * 3U / 4U);
    std::uint32_t buffer = 0;
    int bits = 0;
    for (const char raw : input) {
        const auto byte = static_cast<unsigned char>(raw);
        const auto value = values[byte];
        if (value < 0) return err(Error::validation("cursor is invalid"));
        buffer = (buffer << 6U) | static_cast<std::uint32_t>(value);
        bits += 6;
        if (bits >= 8) {
            bits -= 8;
            result.push_back(static_cast<char>((buffer >> bits) & 0xffU));
        }
    }
    if (bits > 0 && (buffer & ((1U << bits) - 1U)) != 0U) {
        return err(Error::validation("cursor is invalid"));
    }
    return result;
}

[[nodiscard]] std::string encode_cursor(
    const domain::Transaction& transaction) {
    const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(
        transaction.occurred_at().time_since_epoch()).count();
    return base64url_encode(
        "1:" + std::to_string(micros) + ":" + transaction.id().to_string());
}

[[nodiscard]] Result<domain::TransactionPageCursor> decode_cursor(
    std::string_view encoded) {
    auto decoded = base64url_decode(encoded);
    if (!decoded) return err(decoded.error());
    const auto first = decoded->find(':');
    const auto second = first == std::string::npos
        ? std::string::npos : decoded->find(':', first + 1U);
    if (first != 1U || (*decoded)[0] != '1' || second == std::string::npos ||
        decoded->find(':', second + 1U) != std::string::npos) {
        return err(Error::validation("cursor is invalid"));
    }
    std::int64_t micros = 0;
    std::int64_t id = 0;
    const auto micros_text = std::string_view(*decoded).substr(
        first + 1U, second - first - 1U);
    const auto id_text = std::string_view(*decoded).substr(second + 1U);
    const auto micros_result = std::from_chars(
        micros_text.data(), micros_text.data() + micros_text.size(), micros);
    const auto id_result = std::from_chars(
        id_text.data(), id_text.data() + id_text.size(), id);
    if (micros_text.empty() || id_text.empty() ||
        micros_result.ec != std::errc{} ||
        micros_result.ptr != micros_text.data() + micros_text.size() ||
        id_result.ec != std::errc{} ||
        id_result.ptr != id_text.data() + id_text.size() || id <= 0) {
        return err(Error::validation("cursor is invalid"));
    }
    return domain::TransactionPageCursor{
        std::chrono::system_clock::time_point(std::chrono::microseconds(micros)),
        domain::TransactionId(id)};
}

[[nodiscard]] bool valid_type(domain::TransactionType type) {
    switch (type) {
    case domain::TransactionType::Income:
    case domain::TransactionType::Expense:
    case domain::TransactionType::Transfer:
    case domain::TransactionType::Adjustment:
        return true;
    }
    return false;
}

} // namespace

Result<CursorPage<TransactionDto>> ListTransactionsUseCase::execute(
    const TransactionListQuery& query) {
    if (!query.user_id.is_valid() ||
        (query.account_id.has_value() && !query.account_id->is_valid()) ||
        (query.category_id.has_value() && !query.category_id->is_valid()) ||
        (query.tag_id.has_value() && !query.tag_id->is_valid()) ||
        (query.type.has_value() && !valid_type(*query.type))) {
        return err(Error::validation("Transaction filters are invalid"));
    }
    if (auto page = query.page.validate(); !page) return err(page.error());
    if (query.keyword.size() > kMaximumKeywordLength) {
        return err(Error::validation("keyword exceeds 128 characters"));
    }
    if (query.occurred_from.has_value() && query.occurred_to.has_value()) {
        if (*query.occurred_from >= *query.occurred_to) {
            return err(Error::validation("from must be earlier than to"));
        }
        if (*query.occurred_to - *query.occurred_from > kMaximumLedgerRange) {
            return err(Error::validation(
                "Transaction date range cannot exceed 366 days"));
        }
    }

    domain::TransactionPageQuery repository_query;
    repository_query.user_id = query.user_id;
    repository_query.account_id = query.account_id;
    repository_query.type = query.type;
    repository_query.category_id = query.category_id;
    repository_query.tag_id = query.tag_id;
    repository_query.occurred_from = query.occurred_from;
    repository_query.occurred_to = query.occurred_to;
    repository_query.keyword = query.keyword;
    repository_query.limit = query.page.page_size;
    if (query.page.cursor.has_value()) {
        auto cursor = decode_cursor(*query.page.cursor);
        if (!cursor) return err(cursor.error());
        repository_query.before = *cursor;
    }

    auto page = transactions_.find_page(repository_query);
    if (!page) return err(from_repository(page.error()));
    CursorPage<TransactionDto> result;
    result.items.reserve(page->items.size());
    for (const auto& item : page->items) {
        result.items.push_back(to_transaction_dto(item));
    }
    if (page->has_more && !page->items.empty()) {
        result.next_cursor = encode_cursor(page->items.back().transaction);
    }
    return result;
}

Result<TransactionDto> GetTransactionUseCase::execute(
    domain::UserId user_id,
    domain::TransactionId transaction_id) {
    if (!user_id.is_valid() || !transaction_id.is_valid()) {
        return err(Error::validation(
            "User and transaction ids must be valid"));
    }
    auto transaction = transactions_.find_detail(
        transaction_id, user_id, true);
    return transaction
        ? Result<TransactionDto>(to_transaction_dto(*transaction))
        : Result<TransactionDto>(err(from_repository(transaction.error())));
}

} // namespace pfh::application
