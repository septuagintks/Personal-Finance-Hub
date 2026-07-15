// Personal Finance Hub - External Exchange-Rate Providers

#include "pfh/infrastructure/external/exchange_rate_providers.h"

#include "pfh/domain/decimal.h"

#include <charconv>
#include <cstdint>
#include <limits>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace pfh::infrastructure {

namespace {

constexpr std::size_t kMaximumResponseBytes = 1024U * 1024U;
constexpr std::uint64_t kMaximumUnixTimestamp = 253402300799ULL;

enum class ResponseSchema {
    FreeCurrencyApi,
    TimestampedRates
};

enum class ObjectRole {
    Root,
    Rates,
    Other
};

struct ParsedResponse {
    std::string base;
    std::string timestamp;
    std::map<std::string, std::string> rates;
};

class RatePayloadSax final : public nlohmann::json_sax<nlohmann::json> {
public:
    using number_integer_t = nlohmann::json::number_integer_t;
    using number_unsigned_t = nlohmann::json::number_unsigned_t;
    using number_float_t = nlohmann::json::number_float_t;
    using string_t = nlohmann::json::string_t;
    using binary_t = nlohmann::json::binary_t;

    explicit RatePayloadSax(ResponseSchema schema) : schema_(schema) {}

    [[nodiscard]] bool null() override { return scalar_invalid(); }

    [[nodiscard]] bool boolean(bool) override { return scalar_invalid(); }

    [[nodiscard]] bool number_integer(number_integer_t value) override {
        return number(std::to_string(value), true);
    }

    [[nodiscard]] bool number_unsigned(number_unsigned_t value) override {
        return number(std::to_string(value), true);
    }

    [[nodiscard]] bool number_float(
        number_float_t,
        const string_t& token) override {
        return number(token, false);
    }

    [[nodiscard]] bool string(string_t& value) override {
        if (frames_.empty()) {
            return fail("root must be an object");
        }
        auto& frame = frames_.back();
        if (!frame.object) {
            return true;
        }
        if (schema_ == ResponseSchema::TimestampedRates &&
            frame.role == ObjectRole::Root && frame.key == "base") {
            response_.base = value;
            base_seen_ = true;
            frame.key.clear();
            return true;
        }
        if (is_required_root_key(frame) || frame.role == ObjectRole::Rates) {
            return fail("required field has the wrong type");
        }
        frame.key.clear();
        return true;
    }

    [[nodiscard]] bool binary(binary_t&) override {
        return scalar_invalid();
    }

    [[nodiscard]] bool start_object(std::size_t) override {
        return start_container(true);
    }

    [[nodiscard]] bool key(string_t& value) override {
        if (frames_.empty() || !frames_.back().object) {
            return fail("object key appeared outside an object");
        }
        auto& frame = frames_.back();
        if (!frame.keys.insert(value).second) {
            return fail("response contains a duplicate object key");
        }
        frame.key = value;
        return true;
    }

    [[nodiscard]] bool end_object() override { return end_container(true); }

    [[nodiscard]] bool start_array(std::size_t) override {
        return start_container(false);
    }

    [[nodiscard]] bool end_array() override { return end_container(false); }

    [[nodiscard]] bool parse_error(
        std::size_t,
        const std::string&,
        const nlohmann::detail::exception&) override {
        return fail("response is not valid JSON");
    }

    [[nodiscard]] bool complete() const noexcept {
        const bool timestamp_fields_complete =
            schema_ == ResponseSchema::FreeCurrencyApi ||
            (base_seen_ && timestamp_seen_);
        return error_.empty() && root_complete_ && rates_seen_ &&
               timestamp_fields_complete;
    }

    [[nodiscard]] const ParsedResponse& response() const noexcept {
        return response_;
    }

private:
    struct Frame {
        bool object = false;
        ObjectRole role = ObjectRole::Other;
        std::string key;
        std::set<std::string> keys;
    };

    [[nodiscard]] std::string_view rates_key() const noexcept {
        return schema_ == ResponseSchema::FreeCurrencyApi ? "data" : "rates";
    }

    [[nodiscard]] bool is_required_root_key(const Frame& frame) const {
        if (frame.role != ObjectRole::Root) {
            return false;
        }
        if (frame.key == rates_key()) {
            return true;
        }
        return schema_ == ResponseSchema::TimestampedRates &&
               (frame.key == "base" || frame.key == "timestamp");
    }

    [[nodiscard]] bool start_container(bool object) {
        if (frames_.empty()) {
            if (!object || root_started_) {
                return fail("root must be one object");
            }
            root_started_ = true;
            frames_.push_back(Frame{true, ObjectRole::Root, {}, {}});
            return true;
        }

        auto& parent = frames_.back();
        ObjectRole role = ObjectRole::Other;
        if (parent.object) {
            if (parent.role == ObjectRole::Root &&
                parent.key == rates_key()) {
                if (!object) {
                    return fail("rates must be an object");
                }
                rates_seen_ = true;
                role = ObjectRole::Rates;
            } else if (is_required_root_key(parent) ||
                       parent.role == ObjectRole::Rates) {
                return fail("required field has the wrong type");
            }
        }
        frames_.push_back(Frame{object, role, {}, {}});
        return true;
    }

    [[nodiscard]] bool end_container(bool object) {
        if (frames_.empty() || frames_.back().object != object) {
            return fail("response container is malformed");
        }
        const auto role = frames_.back().role;
        frames_.pop_back();
        if (role == ObjectRole::Root) {
            root_complete_ = true;
        }
        if (!frames_.empty() && frames_.back().object) {
            frames_.back().key.clear();
        }
        return true;
    }

    [[nodiscard]] bool scalar_invalid() {
        if (frames_.empty()) {
            return fail("root must be an object");
        }
        auto& frame = frames_.back();
        if (!frame.object) {
            return true;
        }
        if (is_required_root_key(frame) || frame.role == ObjectRole::Rates) {
            return fail("required field has the wrong type");
        }
        frame.key.clear();
        return true;
    }

    [[nodiscard]] bool number(std::string token, bool integer) {
        if (frames_.empty()) {
            return fail("root must be an object");
        }
        auto& frame = frames_.back();
        if (!frame.object) {
            return true;
        }
        if (schema_ == ResponseSchema::TimestampedRates &&
            frame.role == ObjectRole::Root && frame.key == "timestamp") {
            if (!integer) {
                return fail("timestamp must be an integer");
            }
            response_.timestamp = std::move(token);
            timestamp_seen_ = true;
            frame.key.clear();
            return true;
        }
        if (frame.role == ObjectRole::Rates) {
            if (frame.key.empty() || token.empty() || token.size() > 128U) {
                return fail("rate token is invalid");
            }
            response_.rates.emplace(frame.key, std::move(token));
            frame.key.clear();
            return true;
        }
        if (is_required_root_key(frame)) {
            return fail("required field has the wrong type");
        }
        frame.key.clear();
        return true;
    }

    [[nodiscard]] bool fail(std::string message) {
        if (error_.empty()) {
            error_ = std::move(message);
        }
        return false;
    }

    ResponseSchema schema_;
    std::vector<Frame> frames_;
    ParsedResponse response_;
    std::string error_;
    bool root_started_ = false;
    bool root_complete_ = false;
    bool base_seen_ = false;
    bool timestamp_seen_ = false;
    bool rates_seen_ = false;
};

[[nodiscard]] std::string percent_encode(std::string_view value) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string encoded;
    encoded.reserve(value.size());
    for (const char raw : value) {
        const auto c = static_cast<unsigned char>(raw);
        const bool unreserved =
            (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
            (c >= '0' && c <= '9') || c == '-' || c == '_' ||
            c == '.' || c == '~';
        if (unreserved) {
            encoded.push_back(raw);
            continue;
        }
        encoded.push_back('%');
        encoded.push_back(kHex[(c >> 4U) & 0x0fU]);
        encoded.push_back(kHex[c & 0x0fU]);
    }
    return encoded;
}

[[nodiscard]] domain::RepositoryError invalid_response(
    std::string_view provider_name) {
    return domain::RepositoryError::validation(
        std::string(provider_name) + " returned an invalid response");
}

[[nodiscard]] domain::RepositoryResult<
    std::map<std::string, domain::Currency>>
collect_requested(
    const domain::Currency& base,
    const std::vector<domain::Currency>& targets,
    std::string_view provider_name) {
    if (base.code() != "USD") {
        return std::unexpected(domain::RepositoryError::validation(
            std::string(provider_name) + " only supports USD as the base"));
    }
    std::map<std::string, domain::Currency> requested;
    for (const auto& target : targets) {
        if (target == base) {
            return std::unexpected(domain::RepositoryError::validation(
                std::string(provider_name) +
                " target must differ from the base"));
        }
        requested.emplace(target.code(), target);
    }
    return requested;
}

[[nodiscard]] std::string make_symbols(
    const std::map<std::string, domain::Currency>& requested) {
    std::string symbols;
    for (const auto& [code, ignored] : requested) {
        (void)ignored;
        if (!symbols.empty()) {
            symbols.push_back(',');
        }
        symbols += code;
    }
    return symbols;
}

[[nodiscard]] domain::RepositoryResult<ParsedResponse> parse_response(
    std::string_view body,
    ResponseSchema schema,
    std::string_view provider_name) {
    if (body.empty() || body.size() > kMaximumResponseBytes) {
        return std::unexpected(invalid_response(provider_name));
    }
    RatePayloadSax sax(schema);
    const bool parsed = nlohmann::json::sax_parse(body, &sax);
    if (!parsed || !sax.complete()) {
        return std::unexpected(invalid_response(provider_name));
    }
    return sax.response();
}

[[nodiscard]] domain::RepositoryResult<
    std::chrono::system_clock::time_point>
parse_timestamp(
    std::string_view token,
    const application::IClock& clock,
    std::string_view provider_name) {
    std::uint64_t timestamp = 0;
    const auto* begin = token.data();
    const auto* end = begin + token.size();
    const auto converted = std::from_chars(begin, end, timestamp);
    const auto maximum_clock_seconds =
        std::chrono::duration_cast<std::chrono::seconds>(
            std::chrono::system_clock::duration::max())
            .count();
    if (converted.ec != std::errc{} || converted.ptr != end ||
        timestamp == 0 || timestamp > kMaximumUnixTimestamp ||
        maximum_clock_seconds <= 0 ||
        timestamp > static_cast<std::uint64_t>(maximum_clock_seconds) ||
        timestamp > static_cast<std::uint64_t>(
                        std::numeric_limits<std::int64_t>::max())) {
        return std::unexpected(invalid_response(provider_name));
    }
    const auto fetched_at = std::chrono::system_clock::time_point{
        std::chrono::duration_cast<std::chrono::system_clock::duration>(
            std::chrono::seconds(static_cast<std::int64_t>(timestamp)))};
    if (fetched_at > clock.now() + std::chrono::minutes(5)) {
        return std::unexpected(invalid_response(provider_name));
    }
    return fetched_at;
}

[[nodiscard]] domain::RepositoryResult<std::vector<domain::ExchangeRate>>
make_rates(
    const domain::Currency& base,
    const std::map<std::string, domain::Currency>& requested,
    const std::map<std::string, std::string>& tokens,
    std::chrono::system_clock::time_point fetched_at,
    std::string_view provider_name,
    bool require_exact_set) {
    if (require_exact_set && tokens.size() != requested.size()) {
        return std::unexpected(invalid_response(provider_name));
    }

    std::vector<domain::ExchangeRate> result;
    result.reserve(requested.size());
    for (const auto& [code, target] : requested) {
        const auto found = tokens.find(code);
        if (found == tokens.end()) {
            return std::unexpected(invalid_response(provider_name));
        }
        // Provider feeds may carry more fractional digits than PFH persists.
        // Normalize explicitly with Decimal's Half-Even parser before checking
        // the NUMERIC(20,10) boundary; user-supplied rates remain strict at the
        // application boundary and do not use this adapter-only path.
        auto decimal = domain::Decimal::parse(found->second);
        if (!decimal || !decimal->is_positive() ||
            !decimal->fits_numeric_20_10()) {
            return std::unexpected(invalid_response(provider_name));
        }
        auto rate = domain::ExchangeRate::create(
            base,
            target,
            *decimal,
            fetched_at,
            std::string(provider_name));
        if (!rate) {
            return std::unexpected(invalid_response(provider_name));
        }
        result.push_back(std::move(*rate));
    }
    return result;
}

[[nodiscard]] domain::RepositoryError transport_error(
    std::string_view provider_name,
    HttpTransportErrorKind kind) {
    return domain::RepositoryError::database(
        std::string(provider_name) +
        (kind == HttpTransportErrorKind::Timeout
             ? " request timed out"
             : " request failed"));
}

[[nodiscard]] domain::RepositoryError http_status_error(
    std::string_view provider_name) {
    return domain::RepositoryError::database(
        std::string(provider_name) + " returned a non-success status");
}

} // namespace

FreeCurrencyApiProvider::~FreeCurrencyApiProvider() {
    auto* bytes = static_cast<volatile char*>(api_key_.data());
    for (std::size_t index = 0; index < api_key_.size(); ++index) {
        bytes[index] = '\0';
    }
}

domain::RepositoryResult<std::vector<domain::ExchangeRate>>
FreeCurrencyApiProvider::fetch_latest(
    const domain::Currency& base,
    const std::vector<domain::Currency>& targets) {
    if (api_key_.empty() || api_key_.starts_with("REPLACE_WITH_") ||
        api_key_.size() > 512U ||
        timeout_ <= std::chrono::milliseconds::zero()) {
        return std::unexpected(domain::RepositoryError::validation(
            "FreeCurrencyAPI provider configuration is invalid"));
    }
    auto requested = collect_requested(base, targets, provider_name());
    if (!requested) {
        return std::unexpected(requested.error());
    }
    if (requested->empty()) {
        return std::vector<domain::ExchangeRate>{};
    }

    const std::string path =
        "/v1/latest?apikey=" + percent_encode(api_key_) +
        "&base_currency=" + percent_encode(base.code()) +
        "&currencies=" + percent_encode(make_symbols(*requested));
    auto response = transport_.get(path, timeout_);
    if (!response) {
        return std::unexpected(
            transport_error(provider_name(), response.error().kind));
    }
    if (response->status_code != 200) {
        return std::unexpected(http_status_error(provider_name()));
    }
    auto payload = parse_response(
        response->body, ResponseSchema::FreeCurrencyApi, provider_name());
    if (!payload) {
        return std::unexpected(payload.error());
    }
    return make_rates(
        base,
        *requested,
        payload->rates,
        clock_.now(),
        provider_name(),
        true);
}

domain::RepositoryResult<std::vector<domain::ExchangeRate>>
ExchangeRateFunProvider::fetch_latest(
    const domain::Currency& base,
    const std::vector<domain::Currency>& targets) {
    if (timeout_ <= std::chrono::milliseconds::zero()) {
        return std::unexpected(domain::RepositoryError::validation(
            "exchangerate.fun provider configuration is invalid"));
    }
    auto requested = collect_requested(base, targets, provider_name());
    if (!requested) {
        return std::unexpected(requested.error());
    }
    if (requested->empty()) {
        return std::vector<domain::ExchangeRate>{};
    }

    const std::string path =
        "/latest?base=" + percent_encode(base.code()) +
        "&symbols=" + percent_encode(make_symbols(*requested));
    auto response = transport_.get(path, timeout_);
    if (!response) {
        return std::unexpected(
            transport_error(provider_name(), response.error().kind));
    }
    if (response->status_code != 200) {
        return std::unexpected(http_status_error(provider_name()));
    }
    auto payload = parse_response(
        response->body, ResponseSchema::TimestampedRates, provider_name());
    if (!payload || payload->base != base.code()) {
        return std::unexpected(invalid_response(provider_name()));
    }
    auto fetched_at = parse_timestamp(
        payload->timestamp, clock_, provider_name());
    if (!fetched_at) {
        return std::unexpected(fetched_at.error());
    }
    // exchangerate.fun currently ignores symbols and returns a superset. The
    // requested set must still be complete and valid; unrelated rates are
    // deliberately ignored instead of coupling PFH to the full catalog.
    return make_rates(
        base,
        *requested,
        payload->rates,
        *fetched_at,
        provider_name(),
        false);
}

domain::RepositoryResult<std::vector<domain::ExchangeRate>>
FailoverExchangeRateProvider::fetch_latest(
    const domain::Currency& base,
    const std::vector<domain::Currency>& targets) {
    if (base.code() != "USD") {
        return std::unexpected(domain::RepositoryError::validation(
            "Exchange-rate failover only supports USD as the base"));
    }
    for (const auto& target : targets) {
        if (target == base) {
            return std::unexpected(domain::RepositoryError::validation(
                "Exchange-rate target must differ from the base"));
        }
    }

    auto primary = primary_.fetch_latest(base, targets);
    if (primary) {
        return primary;
    }
    auto fallback = fallback_.fetch_latest(base, targets);
    if (fallback) {
        return fallback;
    }
    return std::unexpected(domain::RepositoryError::database(
        "All configured exchange-rate providers failed"));
}

} // namespace pfh::infrastructure
