// Personal Finance Hub - Money Value Object Implementation
// Version: 1.0
// C++23

#include "pfh/domain/money.h"

namespace pfh::domain {

namespace {

/// @brief Ensure two Money values share the same currency.
[[nodiscard]] DomainVoidResult require_same_currency(const Currency& a, const Currency& b) {
    if (!(a == b)) {
        return std::unexpected(DomainError::currency_mismatch(a.code(), b.code()));
    }
    return {};
}

} // namespace

DomainResult<Money> Money::add(const Money& other) const {
    if (auto check = require_same_currency(currency_, other.currency_); !check) {
        return std::unexpected(check.error());
    }
    auto sum = amount_.add(other.amount_);
    if (!sum) {
        return std::unexpected(sum.error());
    }
    return Money(*sum, currency_);
}

DomainResult<Money> Money::subtract(const Money& other) const {
    if (auto check = require_same_currency(currency_, other.currency_); !check) {
        return std::unexpected(check.error());
    }
    auto diff = amount_.subtract(other.amount_);
    if (!diff) {
        return std::unexpected(diff.error());
    }
    return Money(*diff, currency_);
}

DomainResult<Money> Money::multiply(const Decimal& factor) const {
    auto product = amount_.multiply(factor);
    if (!product) {
        return std::unexpected(product.error());
    }
    return Money(*product, currency_);
}

DomainResult<std::strong_ordering> Money::compare(const Money& other) const {
    if (auto check = require_same_currency(currency_, other.currency_); !check) {
        return std::unexpected(check.error());
    }
    return amount_ <=> other.amount_;
}

} // namespace pfh::domain
